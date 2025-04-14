#!/bin/bash

# Script pour compiler et exécuter la version sans GUI de CISYNTH
# Spécifiquement conçu pour la branche jetson

# Arrêter en cas d'erreur
set -e

# Afficher les commandes exécutées
set -x

# Créer le répertoire de build si nécessaire
mkdir -p build_nogui

# Générer le Makefile avec qmake
qmake -o build_nogui/Makefile CISYNTH_noGUI.pro

# Compiler le projet
cd build_nogui && make -j$(nproc 2>/dev/null || echo 2)

# Revenir au répertoire principal
cd ..

echo "Compilation terminée avec succès!"
echo "L'exécutable se trouve dans build_nogui/CISYNTH_noGUI"

# Exécuter le programme en mode terminal
# Décommenter la ligne suivante pour exécuter automatiquement après la compilation
# ./build_nogui/CISYNTH_noGUI
