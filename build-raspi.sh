#!/bin/bash
# Script de compilation Mixxx pour Raspberry Pi 4 (2GB RAM)
# Usage: ./build-raspi.sh

set -e  # Exit on error

echo "========================================"
echo "Mixxx Custom - Compilation Raspberry Pi"
echo "========================================"
echo ""

# Couleurs pour l'output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Vérifier si on est sur Raspberry Pi
if [[ ! -f /proc/device-tree/model ]] || ! grep -q "Raspberry Pi" /proc/device-tree/model 2>/dev/null; then
    echo -e "${YELLOW}⚠️  Attention: Ce script est optimisé pour Raspberry Pi 4${NC}"
    echo "Continuer quand même ? (y/n)"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        exit 0
    fi
fi

# Vérifier la RAM disponible
TOTAL_RAM=$(free -m | awk '/^Mem:/{print $2}')
echo -e "${GREEN}✓${NC} RAM totale détectée: ${TOTAL_RAM}MB"

if [[ $TOTAL_RAM -lt 2048 ]]; then
    echo -e "${RED}✗${NC} RAM insuffisante (< 2GB). Compilation impossible."
    exit 1
fi

# Vérifier le swap
SWAP_SIZE=$(free -m | awk '/^Swap:/{print $2}')
echo -e "${GREEN}✓${NC} Swap actuel: ${SWAP_SIZE}MB"

if [[ $SWAP_SIZE -lt 4096 ]]; then
    echo -e "${YELLOW}⚠️  Swap trop petit (recommandé: 4GB)${NC}"
    echo "Voulez-vous augmenter le swap à 4GB ? (y/n)"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        echo "Augmentation du swap..."
        sudo dphys-swapfile swapoff
        sudo sed -i 's/^CONF_SWAPSIZE=.*/CONF_SWAPSIZE=4096/' /etc/dphys-swapfile
        sudo dphys-swapfile setup
        sudo dphys-swapfile swapon
        echo -e "${GREEN}✓${NC} Swap augmenté à 4GB"
    else
        echo -e "${YELLOW}⚠️  La compilation pourrait échouer par manque de mémoire${NC}"
    fi
fi

# Vérifier les dépendances
echo ""
echo "Vérification des dépendances..."

DEPS=(
    "cmake" "git" "gcc" "g++"
    "qtbase6-dev" "qtdeclarative6-dev"
    "libportaudio2" "libportmidi-dev"
    "libsndfile1-dev" "libfftw3-dev"
    "libid3tag0-dev" "libogg-dev" "libvorbis-dev"
)

MISSING_DEPS=()

for dep in "${DEPS[@]}"; do
    if ! dpkg -l | grep -q "^ii  $dep"; then
        MISSING_DEPS+=("$dep")
    fi
done

if [[ ${#MISSING_DEPS[@]} -gt 0 ]]; then
    echo -e "${YELLOW}⚠️  Dépendances manquantes:${NC}"
    printf '%s\n' "${MISSING_DEPS[@]}"
    echo ""
    echo "Voulez-vous les installer maintenant ? (y/n)"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        sudo apt update
        sudo apt install -y "${MISSING_DEPS[@]}"
        echo -e "${GREEN}✓${NC} Dépendances installées"
    else
        echo -e "${RED}✗${NC} Installation annulée"
        exit 1
    fi
else
    echo -e "${GREEN}✓${NC} Toutes les dépendances sont installées"
fi

# Configuration CMake
echo ""
echo "Configuration CMake..."

if [[ -d build ]]; then
    echo -e "${YELLOW}⚠️  Le répertoire build existe déjà${NC}"
    echo "Voulez-vous le supprimer et recommencer ? (y/n)"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        rm -rf build
        echo -e "${GREEN}✓${NC} Répertoire build supprimé"
    fi
fi

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

echo -e "${GREEN}✓${NC} Configuration CMake terminée"

# Compilation
echo ""
echo "========================================"
echo "Compilation en cours..."
echo "ATTENTION: Cela peut prendre 2-4 heures"
echo "========================================"
echo ""

START_TIME=$(date +%s)

# Utiliser -j1 pour éviter les crashes mémoire sur Raspberry Pi 4 (2GB)
cmake --build build -j1

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
HOURS=$((DURATION / 3600))
MINUTES=$(((DURATION % 3600) / 60))

echo ""
echo -e "${GREEN}✓${NC} Compilation terminée en ${HOURS}h ${MINUTES}min"

# Installation (optionnelle)
echo ""
echo "Voulez-vous installer Mixxx maintenant ? (y/n)"
read -r response
if [[ "$response" =~ ^[Yy]$ ]]; then
    sudo cmake --install build
    echo -e "${GREEN}✓${NC} Installation terminée"
    echo ""
    echo "Vous pouvez maintenant lancer Mixxx avec la commande: mixxx"
else
    echo ""
    echo "Pour installer plus tard: sudo cmake --install build"
    echo "L'exécutable est disponible dans: build/mixxx"
fi

echo ""
echo "========================================"
echo -e "${GREEN}✓ BUILD TERMINÉ !${NC}"
echo "========================================"
echo ""
echo "Modifications custom actives:"
echo "  ✓ Waveform fixe (pré-EQ)"
echo "  ✓ Downbeats rouges épais (tous les 4 temps)"
echo "  ✓ Couleurs 3Band style Rekordbox"
echo ""
echo "Consultez CUSTOM_MODIFICATIONS.md pour plus d'infos"
