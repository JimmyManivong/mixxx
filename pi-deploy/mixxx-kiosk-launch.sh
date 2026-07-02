#!/bin/bash
# =============================================================================
#  Lanceur KIOSK Mixxx : plein ecran + anti-veille + curseur cache.
#  Appele automatiquement par l'autostart de la session graphique
#  (X11 / wayfire / labwc). Installe par setup-kiosk.sh dans ~/.local/bin.
# =============================================================================
set -u
log(){ echo "[mixxx-kiosk] $*"; }

# Evite les double-lancements (plusieurs mecanismes d'autostart possibles)
if pgrep -x mixxx >/dev/null 2>&1; then
    log "Mixxx deja en cours -> on ne relance pas."
    exit 0
fi

# Anti-veille / curseur selon le type de session
case "${XDG_SESSION_TYPE:-}" in
  x11)
    log "Session X11 : desactivation veille/DPMS + curseur cache"
    xset s off       || true
    xset s noblank   || true
    xset -dpms       || true
    command -v unclutter >/dev/null 2>&1 && unclutter -idle 1 -root &
    ;;
  *)
    log "Session Wayland (ou inconnue) : veille geree par le compositeur"
    ;;
esac

# (Optionnel) attendre la carte son du DDJ-FLX6 avant de lancer Mixxx.
# Decommente si Mixxx demarre parfois avant que l'USB audio soit pret :
# for i in $(seq 1 15); do
#   aplay -l 2>/dev/null | grep -qi FLX6 && { log "DDJ-FLX6 detecte"; break; }
#   log "attente DDJ-FLX6 ($i/15)"; sleep 1
# done

log "Demarrage de Mixxx --fullScreen"
exec mixxx --fullScreen
