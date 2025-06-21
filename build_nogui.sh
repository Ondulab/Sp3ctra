#!/bin/bash

# Script pour compiler et exécuter la version sans GUI de CISYNTH
# Spécifiquement conçu pour la branche jetson

# Arrêter en cas d'erreur
set -e

# Traitement des arguments de ligne de commande
RUN_AFTER_BUILD=0
ENABLE_CLI_MODE=0

while [[ $# -gt 0 ]]; do
  case $1 in
    --run)
      RUN_AFTER_BUILD=1
      shift
      ;;
    --cli)
      ENABLE_CLI_MODE=1
      shift
      ;;
    *)
      echo "Option non reconnue: $1"
      echo "Usage: $0 [--run] [--cli]"
      echo "  --run    Exécute le programme après la compilation"
      echo "  --cli    Active le mode CLI (sans interface graphique)"
      exit 1
      ;;
  esac
done

# Afficher les commandes exécutées
set -x

# Créer le répertoire de build si nécessaire
mkdir -p build_nogui

# Define additional qmake variables if needed
QMAKE_OPTIONS=""
if [ "$ENABLE_CLI_MODE" -eq 1 ]; then
  QMAKE_OPTIONS="CONFIG+=cli_mode CONFIG+=release QMAKE_CXXFLAGS+=-Ofast QMAKE_LFLAGS+=-s"
  echo "CLI mode enabled with Ofast optimization"
fi

# Générer le Makefile avec qmake
qmake -o build_nogui/Makefile CISYNTH_noGUI.pro $QMAKE_OPTIONS

# Compiler le projet
cd build_nogui && make -j$(nproc 2>/dev/null || echo 2)

# Revenir au répertoire principal
cd ..

echo "Compilation terminée avec succès!"
echo "L'exécutable se trouve dans build_nogui/CISYNTH_noGUI"

# Exécuter le programme si demandé
if [ "$RUN_AFTER_BUILD" -eq 1 ]; then
  echo "Lancement de l'application..."
  if [ "$ENABLE_CLI_MODE" -eq 1 ]; then
    ./build_nogui/CISYNTH_noGUI --cli
  else
    ./build_nogui/CISYNTH_noGUI
  fi
fi
