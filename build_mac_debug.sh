#!/bin/bash

# Script pour compiler une version de DÉBOGAGE de CISYNTH sur macOS avec SFML.

# Arrêter en cas d'erreur
set -e

# Afficher les commandes exécutées
set -x

# Créer le répertoire de build si nécessaire
mkdir -p build_nogui

# Définir les options qmake pour le mode DÉBOGAGE avec SFML
# On utilise CONFIG+=debug pour inclure les symboles de débogage et désactiver les optimisations.
QMAKE_OPTIONS="CONFIG+=cli_mode CONFIG+=use_sfml CONFIG+=debug"
echo "SFML window mode enabled for macOS DEBUG build"

# Générer le Makefile avec qmake
qmake -o build_nogui/Makefile CISYNTH_noGUI.pro $QMAKE_OPTIONS

# Nettoyer les anciens fichiers puis compiler le projet
cd build_nogui && make clean && make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Revenir au répertoire principal
cd ..

echo "Compilation de débogage terminée avec succès!"
echo "L'exécutable se trouve dans build_nogui/CISYNTH_noGUI"
echo "Vous pouvez maintenant le lancer avec le débogueur de VS Code."
