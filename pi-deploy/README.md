# Déploiement Kiosk — Mixxx sur Raspberry Pi 4 + écran 1024×600

Configure le Raspberry Pi pour qu'il démarre **directement dans Mixxx en plein écran**
sur l'écran tactile **LAFVIN 10.1″ 1024×600**, comme un lecteur standalone.

> ⚠️ Ces scripts se lancent **sur le Raspberry Pi**, pas sur la machine de dev.
> Pré-requis : Mixxx déjà compilé/installé (voir `../build-raspi.sh`) et
> **Raspberry Pi OS Bookworm 64-bit** recommandé.

## 🚀 Démarrage rapide

```bash
# Sur le Pi, depuis le dépôt :
cd pi-deploy
chmod +x *.sh
./setup-kiosk.sh
sudo reboot
```

## Ce que fait `setup-kiosk.sh`

| # | Action | Fichier touché |
|---|--------|----------------|
| 1 | Force la résolution **1024×600** (pilote KMS) | `cmdline.txt` → `video=HDMI-A-1:1024x600M@60` |
| 2 | Détection HDMI + désactive l'overscan | `config.txt` (bloc balisé) |
| 3 | Installe le lanceur kiosk | `~/.local/bin/mixxx-kiosk-launch.sh` |
| 4 | Lance Mixxx plein écran au boot | autostart X11 **et/ou** wayfire **et/ou** labwc |
| 5 | Désactive la veille écran | `raspi-config do_blanking` |
| 6 | Boot → bureau + autologin | `raspi-config do_boot_behaviour B4` |
| 7 | Cache le curseur souris | `unclutter` |

Tout est **idempotent** (relançable) et fait un **backup** des fichiers de boot.

## 🔊 Audio — passe par le DDJ-FLX6 (important)

Le DDJ-FLX6 **est une carte son USB**. Le son ne doit PAS sortir par le Pi ni les
HP de l'écran : route tout par le FLX6 (sorties Master RCA + Booth + casque).

Dans Mixxx → **Préférences → Sound Hardware** :
- **Output → Master** : `DDJ-FLX6` (canaux 1-2)
- **Output → Headphones** : `DDJ-FLX6` (canaux 3-4)
- Buffer (latence) : commence à **23 ms**, descends si le Pi suit.

## 🎛️ Mapping contrôleur

Mixxx → **Préférences → Controllers** → active le **DDJ-FLX6** et charge le preset
Pioneer DDJ-FLX6 (ou le mapping communautaire *fixxiefixx* si tu l'utilises).

## 👆 Écran tactile

Tactile **USB HID** = plug-and-play sous Linux, pas de pilote.
Si le toucher est décalé/inversé par rapport à l'affichage :
- **X11** : `xinput` + matrice de transformation (`Coordinate Transformation Matrix`).
- **Wayland** : mappe le touch sur la bonne sortie via la config du compositeur
  (`libinput` → `map-to-output`).

## 🌀 Ventilation du Pi

L'écran LAFVIN a un **port FAN** capable de piloter un ventilateur : branche le
ventilo du Pi dessus (refroidi sans config logicielle). Sinon, ventilo sur GPIO :
ajoute dans `config.txt` `dtoverlay=gpio-fan,gpiopin=14,temp=60000` (ON à 60 °C)
ou utilise le contrôleur PWM officiel.

## ⚡ Perf sur Pi 4 (2GB)

- **Garde le KMS** (`dtoverlay=vc4-kms-v3d`, défaut) : nécessaire au rendu GL des
  waveforms (tes waveforms custom Rekordbox).
- Si ça rame : Préférences → Waveforms → baisse la qualité / framerate.
- Le swap est déjà géré par `build-raspi.sh`.

## 🛡️ Durcissement kiosk (optionnel)

- **Système de fichiers en lecture seule** (`raspi-config` → Performance → Overlay FS) :
  débrancher la prise ne corrompt plus la carte SD.
- Désactive l'économie d'énergie Wi-Fi : `iw dev wlan0 set power_save off`.

## 🩺 Dépannage

| Symptôme | Piste |
|----------|-------|
| **Écran noir** au boot | Force le mode : `...1024x600M@60e` (suffixe `e`) dans `cmdline.txt`, ou bascule sur le fallback **legacy** (bloc commenté dans `config.txt` + `vc4-fkms-v3d`). |
| **Image coupée / bords** | `disable_overscan=1` (déjà mis) ; sinon ajuste `overscan_*`. |
| **Mauvaise résolution** | Vérifie les modes : `kmsprint` ou `modetest`. Le connecteur peut être `HDMI-A-2` (autre micro-HDMI) → adapte le `video=`. |
| **Mixxx pas en plein écran** | Vérifie `~/.local/bin/mixxx-kiosk-launch.sh` ; teste `mixxx --fullScreen` à la main. |
| **Toucher décalé** | voir section Écran tactile. |

## Fichiers

- `setup-kiosk.sh` — installateur (à lancer sur le Pi).
- `mixxx-kiosk-launch.sh` — lanceur plein écran + anti-veille (installé dans `~/.local/bin`).
