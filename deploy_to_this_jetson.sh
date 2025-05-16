#!/bin/bash

# Script de déploiement spécifique pour cette Jetson Nano
# Basé sur les informations réseau fournies

# Configuration spécifique à cette Jetson
JETSON_USER="ondulab"
JETSON_IP="192.168.55.1"
JETSON_DIR="/home/$JETSON_USER/cisynth_cli"
DMX_PORT="/dev/ttyUSB0"  # Port DMX typique sur Linux

# Couleurs pour les messages
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Déploiement de CISYNTH CLI sur cette Jetson Nano ===${NC}"
echo -e "${BLUE}IP: ${JETSON_IP}${NC}"
echo -e "${BLUE}Utilisateur: ${JETSON_USER}${NC}"
echo -e "${BLUE}Répertoire cible: ${JETSON_DIR}${NC}"

# Créer un répertoire temporaire pour préparer les fichiers
echo -e "${YELLOW}Préparation des fichiers...${NC}"
TMP_DIR=$(mktemp -d)
mkdir -p $TMP_DIR/src/core

# Copier les fichiers nécessaires
cp CISYNTH_noGUI.pro $TMP_DIR/
cp build_nogui.sh $TMP_DIR/
cp run_on_jetson.sh $TMP_DIR/
cp src/core/*.c $TMP_DIR/src/core/
cp src/core/*.h $TMP_DIR/src/core/

# Modifier le port DMX dans config.h pour Linux
echo -e "${YELLOW}Adaptation du port DMX pour Linux...${NC}"
sed 's|#define DMX_PORT.*|#define DMX_PORT "'$DMX_PORT'"|g' src/core/config.h > $TMP_DIR/src/core/config.h

# Créer le répertoire distant
echo -e "${YELLOW}Création du répertoire sur la Jetson Nano...${NC}"
ssh $JETSON_USER@$JETSON_IP "mkdir -p $JETSON_DIR/src/core"

# Transférer les fichiers
echo -e "${YELLOW}Transfert des fichiers vers la Jetson Nano...${NC}"
scp -r $TMP_DIR/* $JETSON_USER@$JETSON_IP:$JETSON_DIR/

# Nettoyer le répertoire temporaire
rm -rf $TMP_DIR

# Rendre les scripts exécutables
echo -e "${YELLOW}Configuration des permissions...${NC}"
ssh $JETSON_USER@$JETSON_IP "chmod +x $JETSON_DIR/build_nogui.sh $JETSON_DIR/run_on_jetson.sh"

# Installer les dépendances nécessaires
echo -e "${YELLOW}Vérification des dépendances...${NC}"
ssh $JETSON_USER@$JETSON_IP "sudo apt-get update && sudo apt-get install -y build-essential qmake libfftw3-dev libsndfile-dev libsfml-dev libcsfml-dev"

# Ajouter l'utilisateur au groupe dialout pour accéder au port DMX
echo -e "${YELLOW}Configuration des droits pour le port DMX...${NC}"
ssh $JETSON_USER@$JETSON_IP "sudo usermod -a -G dialout $JETSON_USER"

echo -e "${GREEN}Déploiement terminé!${NC}"
echo -e "${YELLOW}Pour exécuter CISYNTH sur la Jetson Nano:${NC}"
echo -e "${BLUE}1. Connectez-vous à la Jetson: ${NC}ssh $JETSON_USER@$JETSON_IP"
echo -e "${BLUE}2. Naviguez vers le répertoire: ${NC}cd $JETSON_DIR"
echo -e "${BLUE}3. Exécutez le script: ${NC}./run_on_jetson.sh"

# Option pour exécuter directement
read -p "Voulez-vous exécuter CISYNTH maintenant? (o/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Oo]$ ]]; then
    echo -e "${YELLOW}Exécution de CISYNTH sur la Jetson Nano...${NC}"
    echo -e "${YELLOW}(Utilisez Ctrl+C pour arrêter l'application)${NC}"
    ssh -t $JETSON_USER@$JETSON_IP "cd $JETSON_DIR && ./run_on_jetson.sh"
fi

echo -e "${GREEN}Terminé!${NC}"
