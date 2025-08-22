#!/bin/bash

# Script pour compiler une version de RELEASE de Sp3ctra sur macOS.

# Arrêter en cas d'erreur
set -e

# Afficher les commandes exécutées
set -x

# Nettoyer les anciens fichiers puis compiler le projet en mode release
make clean
make

echo "Compilation de release terminée avec succès!"
echo "L'exécutable se trouve dans build/Sp3ctra"
