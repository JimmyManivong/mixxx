# Mixxx Custom - Rekordbox Style Modifications

## Vue d'ensemble

Ce fork de Mixxx 2.5.0 implémente des modifications visuelles pour reproduire le style de Rekordbox, optimisé pour une utilisation sur Raspberry Pi 4 (2GB RAM) avec un écran 10" (1200x600) et un contrôleur Pioneer DDJ-FLX6.

## Modifications implémentées

### ✅ 1. Waveform fixe (pré-EQ) - PRIORITÉ HAUTE

**Problème**: Dans Mixxx standard, les waveforms bougent quand on touche aux EQ (Low/Mid/High).

**Solution**: Le signal est maintenant affiché AVANT les EQ, comme Rekordbox.

**Fichier modifié**:
- `src/waveform/renderers/waveformrenderersignalbase.cpp` (lignes 184-216)

**Changement**: Commenté tout le code qui appliquait les gains EQ au rendu visuel. Les gains low/mid/high sont forcés à 1.0.

---

### ✅ 2. Barres de downbeat rouges épaisses - PRIORITÉ HAUTE

**Problème**: Mixxx affiche toutes les lignes de beats avec la même épaisseur et couleur.

**Solution**: Les downbeats (1er temps de chaque mesure 4/4) sont affichés en rouge épais (3px), les autres beats en blanc fin (1px).

**Fichiers modifiés**:
- `src/waveform/renderers/waveformrenderbeat.h` (ajout de `m_beatHighlightColor` et `m_downbeats`)
- `src/waveform/renderers/waveformrenderbeat.cpp` (logique de séparation beats/downbeats)
- `src/waveform/renderers/allshader/waveformrenderbeat.h` (version OpenGL)
- `src/waveform/renderers/allshader/waveformrenderbeat.cpp` (version OpenGL)

**Logique**: Un compteur `beatIndexInBar` détecte les downbeats avec `beatIndexInBar % 4 == 0`.

---

### ✅ 3. Support du BeatHighlightColor dans le code C++

**Problème**: Le tag `<BeatHighlightColor>` existait dans le XML mais n'était pas lu par le code C++.

**Solution**: Implémentation de la lecture du tag depuis le XML avec fallback vers `#FF0000` (rouge) si non spécifié.

**Fichiers modifiés**:
- Tous les fichiers du point #2 ci-dessus
- `res/skins/LateNight/waveform.xml` (ligne 32 : activation du BeatHighlightColor à `#FF0000`)

---

### ✅ 4. Couleurs 3Band style Rekordbox

**Problème**: Les couleurs par défaut de Mixxx ne ressemblent pas à Rekordbox.

**Solution**: Application de la palette Rekordbox :
- **Basses (Low)**: Bleu `#0088FF`
- **Médiums (Mid)**: Jaune/Vert `#88FF00`
- **Aigus (High)**: Orange/Rouge `#FF5500`

**Fichier modifié**:
- `res/skins/LateNight/waveform.xml` (lignes 24-29)

---

## Compilation

### Prérequis

#### Sur macOS (développement)
```bash
brew install cmake qt@6 portaudio libsndfile protobuf ffmpeg chromaprint fftw lame libid3tag libogg libshout libvorbis opus opusfile portmidi soundtouch sqlite taglib wavpack
```

#### Sur Raspberry Pi 4 (Debian Trixie)
```bash
sudo apt update
sudo apt install -y build-essential cmake git \
  qtbase6-dev qtdeclarative6-dev qtscript5-dev qtsvg6-dev qttools6-dev \
  libportaudio2 libportaudio-dev libportmidi-dev \
  libsndfile1-dev libfftw3-dev libmad0-dev \
  libid3tag0-dev libogg-dev libvorbis-dev \
  libopus-dev libopusfile-dev libshout3-dev \
  libsqlite3-dev libtag1-dev libwavpack-dev \
  libchromaprint-dev libmp3lame-dev libebur128-dev \
  libavcodec-dev libavformat-dev libavutil-dev \
  libsoundtouch-dev libupower-glib-dev \
  libgl1-mesa-dev libglu1-mesa-dev
```

**Important pour Raspberry Pi 4 (2GB RAM)**: Augmenter le swap avant de compiler.

```bash
# Arrêter le swap existant
sudo dphys-swapfile swapoff

# Éditer /etc/dphys-swapfile et changer CONF_SWAPSIZE à 4096
sudo nano /etc/dphys-swapfile
# CONF_SWAPSIZE=4096

# Recréer et activer le swap
sudo dphys-swapfile setup
sudo dphys-swapfile swapon
```

### Commandes de compilation

```bash
# Clone (si pas déjà fait)
git clone --branch custom-rekordbox-style https://github.com/VOTRE_FORK/mixxx.git
cd mixxx

# Configuration CMake
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBATTERY=ON \
  -DBROADCAST=ON \
  -DBULK=ON \
  -DHID=ON \
  -DLILV=OFF \
  -DMAD=ON \
  -DMODPLUG=ON \
  -DOPUS=ON \
  -DQTKEYCHAIN=OFF \
  -DVINYLCONTROL=ON

# Compilation (utiliser -j1 sur Raspberry Pi pour éviter les crashes mémoire)
# Sur Raspberry Pi 4 (2GB):
cmake --build build -j1

# Sur macOS ou machine puissante:
cmake --build build -j$(nproc)
```

**Note**: La compilation sur Raspberry Pi 4 avec 2GB de RAM prend environ **2-4 heures** avec `-j1`.

### Installation

```bash
# Sur Linux
sudo cmake --install build

# Sur macOS
# Le .app sera dans build/
open build/Mixxx.app
```

---

## Tests

Après compilation, lancez Mixxx et vérifiez :

1. **Waveform fixe** : Chargez un track, bougez les EQ → la waveform NE DOIT PAS bouger
2. **Downbeats rouges** : Les barres rouges épaisses doivent apparaître tous les 4 temps
3. **Couleurs 3Band** : La waveform doit afficher Bleu (bas), Vert/Jaune (mid), Orange/Rouge (haut)

---

## Configuration pour DDJ-FLX6

Le mapping Pioneer DDJ-FLX6 devrait être détecté automatiquement. Si nécessaire, utilisez le mapping de **fixxiefixx** disponible dans Mixxx.

Pour l'écran 10" (1200x600), vous pouvez ajuster la taille de l'interface dans :
- **Options → Préférences → Interface → Scaling**

---

## Fichiers modifiés (résumé)

```
src/waveform/renderers/waveformrenderersignalbase.cpp  (EQ gains forcés à 1.0)
src/waveform/renderers/waveformrenderbeat.h            (ajout m_beatHighlightColor)
src/waveform/renderers/waveformrenderbeat.cpp          (logique downbeats)
src/waveform/renderers/allshader/waveformrenderbeat.h  (version OpenGL)
src/waveform/renderers/allshader/waveformrenderbeat.cpp (version OpenGL)
res/skins/LateNight/waveform.xml                       (couleurs Rekordbox)
```

---

## Problèmes connus / TODO

- ❌ La détection de downbeat suppose une signature 4/4. Les tracks en 3/4, 6/8, etc. ne seront pas corrects.
- ❌ Pas de détection automatique de la signature temporelle (nécessiterait analyse plus poussée).
- ⚠️ Performance sur Raspberry Pi 4 : à tester avec waveforms haute résolution.

---

## Améliorations futures possibles

1. **Détection de time signature** : Lire la signature temporelle depuis les tags ID3 ou analyser le track
2. **Optimisations GPU** : Utiliser le GPU VideoCore VI de la Raspberry Pi 4 pour le rendu
3. **Interface compacte** : Créer un skin custom optimisé pour l'écran 10" (1200x600)
4. **Couleurs configurables** : Exposer les couleurs Rekordbox dans les préférences GUI

---

## Auteur

Modifications custom par Jimmy Manivong (jimmy.manivong1@gmail.com)

Basé sur Mixxx 2.5.0 : https://github.com/mixxxdj/mixxx

---

## Licence

Mixxx est distribué sous licence GPL v2+. Ces modifications suivent la même licence.
