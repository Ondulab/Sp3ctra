#!/bin/bash
# Script d'installation des dépendances pour CISYNTH sur Jetson Nano

echo "Installation des dépendances pour CISYNTH sur Jetson Nano..."
echo "Ce script nécessite des droits sudo."

# Mettre à jour les dépôts
echo "Mise à jour des dépôts..."
sudo apt update

# Installer les dépendances requises
echo "Installation des bibliothèques de développement..."
sudo apt install -y build-essential qt5-default libqt5gui5 libqt5core5a
sudo apt install -y libfftw3-dev libsndfile1-dev
sudo apt install -y libasound2-dev librtaudio-dev
sudo apt install -y libpthread-stubs0-dev

# Optionnel: SFML (si disponible)
echo "Tentative d'installation de SFML (optionnel)..."
if apt-cache search libsfml-dev >/dev/null 2>&1; then
  sudo apt install -y libsfml-dev libcsfml-dev
  echo "SFML installé avec succès."
else
  echo "SFML non disponible dans les dépôts. L'application fonctionnera sans interface graphique."
fi

echo "Installation des dépendances terminée!"
echo "Vous pouvez maintenant compiler CISYNTH avec './rebuild_cli.sh'"
