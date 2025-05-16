#!/bin/bash

# Script pour compiler et exécuter CISYNTH sur Jetson Nano
# Ce script compile le projet en mode CLI et l'exécute

# Vérification de la plateforme
if [[ $(uname -m) == "aarch64" ]]; then
  echo "Plateforme ARM détectée (probablement Jetson Nano)"
else
  echo "ATTENTION: Ce script est conçu pour la Jetson Nano (architecture ARM)"
  echo "Votre plateforme actuelle: $(uname -m) - $(uname -s)"
  read -p "Voulez-vous continuer quand même? (o/n) " -n 1 -r
  echo
  if [[ ! $REPLY =~ ^[Oo]$ ]]; then
    exit 1
  fi
fi

# Vérifier que les dépendances sont installées
MISSING_DEPS=0
DEPENDENCIES=("qmake" "fftw3-dev" "libsndfile-dev" "libsfml-dev" "libcsfml-dev")

for dep in "${DEPENDENCIES[@]}"; do
  if ! dpkg -l | grep -q "$dep"; then
    echo "Dépendance manquante: $dep"
    MISSING_DEPS=1
  fi
done

if [ $MISSING_DEPS -eq 1 ]; then
  echo "Certaines dépendances sont manquantes. Vous pouvez les installer avec:"
  echo "sudo apt-get update && sudo apt-get install qmake libfftw3-dev libsndfile-dev libsfml-dev libcsfml-dev"
  read -p "Voulez-vous continuer quand même? (o/n) " -n 1 -r
  echo
  if [[ ! $REPLY =~ ^[Oo]$ ]]; then
    exit 1
  fi
fi

# Configuration DMX pour Jetson Nano (à ajuster selon votre configuration)
if [ -f src/core/config.h ]; then
  echo "Vérification de la configuration DMX..."
  
  # Adapter le port DMX pour Linux
  if grep -q "DMX_PORT.*usbserial" src/core/config.h; then
    echo "Le port DMX est configuré pour macOS. Voulez-vous l'adapter pour Linux?"
    echo "Sur Linux/Jetson, le port DMX est généralement quelque chose comme /dev/ttyUSB0"
    read -p "Modifier le port DMX? (o/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Oo]$ ]]; then
      # Sauvegarde du fichier original
      cp src/core/config.h src/core/config.h.bak
      
      # Remplacer le port DMX
      sed -i 's|#define DMX_PORT.*|#define DMX_PORT                                "/dev/ttyUSB0"|g' src/core/config.h
      
      echo "Port DMX modifié pour Linux (/dev/ttyUSB0)"
    fi
  fi
fi

# Compiler et exécuter l'application en mode CLI
echo "Lancement de la compilation en mode CLI..."
bash ./build_nogui.sh --cli --run

# Si l'exécution a été interrompue par Ctrl+C, afficher un message approprié
if [ $? -eq 130 ]; then
  echo "Application terminée par l'utilisateur (Ctrl+C)"
elif [ $? -ne 0 ]; then
  echo "Erreur lors de l'exécution de l'application"
  exit 1
fi

exit 0
