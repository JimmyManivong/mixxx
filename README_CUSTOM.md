# Mixxx Custom - Rekordbox Style

## 🎛️ Qu'est-ce que c'est ?

Fork custom de **Mixxx 2.5.0** qui reproduit l'apparence visuelle de **Rekordbox**, optimisé pour :
- **Raspberry Pi 4** (2GB RAM)
- **Écran 10"** (1024x600)
- **Pioneer DDJ-FLX6** controller

---

## ✨ Modifications principales

| Modification | Statut | Description |
|-------------|--------|-------------|
| **Waveform fixe (pré-EQ)** | ✅ | La waveform NE BOUGE PLUS quand on touche aux EQ |
| **Downbeats rouges épais** | ✅ | Barres rouges tous les 4 temps (comme Rekordbox) |
| **Couleurs 3Band Rekordbox** | ✅ | Bleu (Low), Vert/Jaune (Mid), Orange/Rouge (High) |
| **Support BeatHighlightColor** | ✅ | Tag XML maintenant fonctionnel dans le code C++ |

---

## 🚀 Quick Start

### Sur Raspberry Pi 4

```bash
# 1. Cloner le repo
git clone --branch custom-rekordbox-style https://github.com/VOTRE_USERNAME/mixxx.git
cd mixxx

# 2. Lancer le script de compilation
./build-raspi.sh

# 3. Profiter !
mixxx
```

Le script `build-raspi.sh` gère **automatiquement** :
- ✅ Vérification de la RAM et du swap
- ✅ Installation des dépendances
- ✅ Configuration CMake optimisée
- ✅ Compilation avec `-j1` (évite les crashes mémoire)

---

## 📸 Avant / Après

### Waveform standard Mixxx
- Bouge quand on touche aux EQ ❌
- Tous les beats identiques (blanc fin) ❌
- Couleurs par défaut ❌

### Waveform custom (ce fork)
- Waveform FIXE (pré-EQ) ✅
- Downbeats rouges épais tous les 4 temps ✅
- Couleurs Rekordbox (Bleu/Vert-Jaune/Orange-Rouge) ✅

---

## 📖 Documentation complète

- **[CUSTOM_MODIFICATIONS.md](./CUSTOM_MODIFICATIONS.md)** : Documentation technique détaillée
- Fichiers modifiés, logique de code, compilation manuelle, etc.

---

## ⚙️ Configuration recommandée

### Matériel
- **Raspberry Pi 4** (2GB minimum, 4GB+ recommandé)
- **Écran** : 10" tactile 1024x600 (ou plus grand)
- **Contrôleur** : Pioneer DDJ-FLX6 (mapping fixxiefixx)
- **Stockage** : Carte SD 32GB+ (classe 10 ou UHS-I)
- **Alimentation** : 5V 3A minimum

### Système
- **OS** : Debian Trixie (ou Raspberry Pi OS)
- **Swap** : 4GB (configuré automatiquement par le script)
- **Desktop** : LXDE ou XFCE (léger)

---

## 🛠️ Compilation manuelle

Si vous ne voulez pas utiliser le script automatique :

```bash
# Installer les dépendances
sudo apt update
sudo apt install -y build-essential cmake git \
  qtbase6-dev qtdeclarative6-dev libportaudio-dev \
  libportmidi-dev libsndfile1-dev libfftw3-dev \
  # ... (voir CUSTOM_MODIFICATIONS.md pour la liste complète)

# Configurer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Compiler (sur Raspberry Pi 4 2GB : utiliser -j1)
cmake --build build -j1

# Installer
sudo cmake --install build
```

**⚠️ Attention** : La compilation prend **2-4 heures** sur Raspberry Pi 4.

---

## 📋 Checklist post-installation

Après installation, vérifiez que tout fonctionne :

- [ ] Lancer Mixxx : `mixxx`
- [ ] Charger un track
- [ ] Bouger les EQ → **la waveform ne doit PAS bouger**
- [ ] Vérifier les **barres rouges épaisses tous les 4 temps**
- [ ] Vérifier les **couleurs 3Band** (Bleu/Vert/Orange)
- [ ] Connecter le DDJ-FLX6 → détection automatique
- [ ] Tester le mapping (jog wheels, pads, etc.)

---

## 🐛 Problèmes connus

| Problème | Solution |
|----------|----------|
| Compilation crash (out of memory) | Augmenter le swap à 4GB avec le script |
| Waveform lente sur Raspberry Pi 4 | Baisser la résolution de waveform dans Préférences |
| Downbeats décalés | Normal si signature temporelle ≠ 4/4 |

---

## 🤝 Contribution

Ce fork est un projet personnel, mais les suggestions sont les bienvenues !

**Contact** : jimmy.manivong1@gmail.com

---

## 📜 Licence

Mixxx est distribué sous **GPL v2+**. Ce fork suit la même licence.

**Basé sur** : [Mixxx 2.5.0](https://github.com/mixxxdj/mixxx)

---

## 🙏 Remerciements

- **Mixxx Team** pour ce superbe logiciel open-source
- **fixxiefixx** pour le mapping DDJ-FLX6
- **Pioneer** pour le contrôleur

---

## 🎵 Enjoy mixing!

**Happy DJing on your Raspberry Pi 4! 🎉🎛️🔊**
