#!/bin/bash
# =============================================================================
#  CONFIGURATION KIOSK — Mixxx plein ecran sur Raspberry Pi 4 + ecran 1024x600
# -----------------------------------------------------------------------------
#  A LANCER **SUR LE RASPBERRY PI** (pas sur ta machine de dev) :
#       cd pi-deploy && chmod +x *.sh && ./setup-kiosk.sh
#       sudo reboot
#  Idempotent : relancable sans rien casser (marqueurs de garde + backups).
# =============================================================================
set -euo pipefail
log(){  printf '\033[1;36m[setup-kiosk]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[setup-kiosk]\033[0m %s\n' "$*"; }

HERE="$(cd "$(dirname "$0")" && pwd)"
VIDEO="video=HDMI-A-1:1024x600M@60"   # micro-HDMI 0 (le plus proche de l'USB-C)
MARK="# >>> mixxx-kiosk >>>"
MARKEND="# <<< mixxx-kiosk <<<"

# --- 1. Localiser les fichiers de boot (Bookworm vs anciens) -----------------
if [ -f /boot/firmware/config.txt ]; then BOOT=/boot/firmware; else BOOT=/boot; fi
CONFIG="$BOOT/config.txt"
CMDLINE="$BOOT/cmdline.txt"
log "Fichiers boot : $CONFIG  +  $CMDLINE"

# --- 2. Resolution 1024x600 via la ligne noyau (pilote KMS, defaut Bookworm) -
if grep -q "video=HDMI-A-1:1024x600" "$CMDLINE"; then
    log "Resolution deja presente dans cmdline.txt"
else
    log "Ajout de '$VIDEO' a cmdline.txt"
    sudo cp "$CMDLINE" "$CMDLINE.bak.$(date +%s)"
    sudo sed -i "s/\$/ $VIDEO/" "$CMDLINE"   # cmdline.txt = une seule ligne
fi

# --- 3. Reglages ecran dans config.txt (detection HDMI + pas d'overscan) -----
if grep -q "$MARK" "$CONFIG"; then
    log "Bloc ecran deja present dans config.txt"
else
    log "Ajout du bloc ecran dans config.txt"
    sudo cp "$CONFIG" "$CONFIG.bak.$(date +%s)"
    sudo tee -a "$CONFIG" >/dev/null <<EOF

$MARK
# Ecran HDMI LAFVIN 10.1" 1024x600
hdmi_force_hotplug=1
disable_overscan=1
# Sous KMS (defaut) la resolution vient du 'video=' dans cmdline.txt.
# FALLBACK LEGACY (ecran noir ?) : decommente ci-dessous ET remplace
# 'dtoverlay=vc4-kms-v3d' par 'dtoverlay=vc4-fkms-v3d' plus haut dans ce fichier.
#hdmi_group=2
#hdmi_mode=87
#hdmi_cvt=1024 600 60 6 0 0 0
#hdmi_drive=1
$MARKEND
EOF
fi

# --- 4. Installer le lanceur kiosk -------------------------------------------
BIN="$HOME/.local/bin"; mkdir -p "$BIN"
install -m 0755 "$HERE/mixxx-kiosk-launch.sh" "$BIN/mixxx-kiosk-launch.sh"
LAUNCH="$BIN/mixxx-kiosk-launch.sh"
log "Lanceur installe : $LAUNCH"

# --- 5. Autostart (on couvre X11 + wayfire + labwc ; le lanceur a un garde
#        anti-double-demarrage, donc aucun risque de lancer Mixxx 2 fois) -----
mkdir -p "$HOME/.config/autostart"
cat > "$HOME/.config/autostart/mixxx-kiosk.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Mixxx Kiosk
Exec=$LAUNCH
X-GNOME-Autostart-enabled=true
EOF
log "Autostart XDG : ~/.config/autostart/mixxx-kiosk.desktop"

if [ -f "$HOME/.config/wayfire.ini" ] && ! grep -q "mixxx-kiosk-launch" "$HOME/.config/wayfire.ini"; then
    log "Autostart wayfire.ini"
    if grep -q "^\[autostart\]" "$HOME/.config/wayfire.ini"; then
        sed -i "/^\[autostart\]/a mixxx = $LAUNCH" "$HOME/.config/wayfire.ini"
    else
        printf '\n[autostart]\nmixxx = %s\n' "$LAUNCH" >> "$HOME/.config/wayfire.ini"
    fi
fi

if command -v labwc >/dev/null 2>&1 || [ -d "$HOME/.config/labwc" ]; then
    mkdir -p "$HOME/.config/labwc"
    A="$HOME/.config/labwc/autostart"; touch "$A"; chmod +x "$A"
    grep -q "mixxx-kiosk-launch" "$A" || { echo "$LAUNCH &" >> "$A"; log "Autostart labwc"; }
fi

# --- 6. Desactiver la veille ecran (1 = desactive) ---------------------------
if command -v raspi-config >/dev/null 2>&1; then
    log "Desactivation de la veille ecran"
    sudo raspi-config nonint do_blanking 1 || warn "do_blanking a echoue (a faire a la main)"
fi

# --- 7. Boot -> bureau avec autologin (B4) -----------------------------------
if command -v raspi-config >/dev/null 2>&1; then
    log "Boot -> bureau + autologin"
    sudo raspi-config nonint do_boot_behaviour B4 || warn "autologin a echoue"
fi

# --- 8. Curseur cache (unclutter) --------------------------------------------
if ! command -v unclutter >/dev/null 2>&1; then
    log "Installation de unclutter"
    sudo apt-get update -qq && sudo apt-get install -y unclutter || warn "unclutter non installe"
fi

# --- 9. Config Mixxx : notation de cle en Lancelot (= Camelot / 1A-12B) ------
MIXXX_CFG="$HOME/.mixxx/mixxx.cfg"
mkdir -p "$HOME/.mixxx"
if grep -q "^KeyNotation " "$MIXXX_CFG" 2>/dev/null; then
    sed -i 's/^KeyNotation .*/KeyNotation Lancelot/' "$MIXXX_CFG"
    log "Config KeyNotation mis a jour -> Lancelot"
elif grep -q "^\[Key\]" "$MIXXX_CFG" 2>/dev/null; then
    sed -i '/^\[Key\]/a KeyNotation Lancelot' "$MIXXX_CFG"
    log "Config KeyNotation ajoute dans [Key] -> Lancelot"
else
    printf '\n[Key]\nKeyNotation Lancelot\n' >> "$MIXXX_CFG"
    log "Config [Key] + KeyNotation cree -> Lancelot"
fi

echo
log "TERMINE."
log "Verifie/ajuste l'audio + le mapping FLX6 dans Mixxx (voir README.md)."
log "Puis :  sudo reboot"
