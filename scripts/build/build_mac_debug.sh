#!/bin/bash

# Script pour compiler une version de DÉBOGAGE de Sp3ctra sur macOS.

# Arrêter en cas d'erreur
set -e

# Afficher les commandes exécutées
set -x

# Nettoyer les anciens fichiers puis compiler le projet en mode débogage
make clean
make CXXFLAGS="-std=c++17 -g -Wall -Wextra -fPIC -DUSE_RTAUDIO -DPRINT_FPS -I/opt/homebrew/include" \
     CFLAGS="-g -Wall -Wextra -fPIC -DUSE_RTAUDIO -DPRINT_FPS -I/opt/homebrew/include"

echo "Compilation de débogage terminée avec succès!"
echo "L'exécutable se trouve dans build/Sp3ctra"
echo "Vous pouvez maintenant le lancer avec le débogueur de VS Code."
