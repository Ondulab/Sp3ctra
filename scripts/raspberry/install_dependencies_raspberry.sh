#!/bin/bash
# Script d'installation des dépendances pour CISYNTH sur Raspberry Pi

echo "Installation des dépendances pour CISYNTH sur Raspberry Pi..."
echo "Ce script nécessite des droits sudo."

# Mettre à jour les dépôts
echo "Mise à jour des dépôts..."
sudo apt update

# Installer les dépendances requises
echo "Installation des bibliothèques de développement..."
sudo apt install -y build-essential libqt5gui5 libqt5core5a qtbase5-dev qttools5-dev-tools qt5-qmake qtchooser pkg-config
sudo apt install -y libfftw3-dev libsndfile1-dev
sudo apt install -y libasound2-dev librtaudio-dev librtmidi-dev
sudo apt install -y libpthread-stubs0-dev

# Optionnel: SFML (si disponible)
echo "Tentative d'installation de SFML (optionnel)..."
if apt-cache search libsfml-dev >/dev/null 2>&1; then
  sudo apt install -y libsfml-dev libcsfml-dev
  echo "SFML installé avec succès."
else
  echo "SFML non disponible dans les dépôts. L'application fonctionnera sans interface graphique si SFML est requis pour celle-ci."
fi

echo "Installation des dépendances terminée!"
echo "Vous pouvez maintenant compiler CISYNTH."
