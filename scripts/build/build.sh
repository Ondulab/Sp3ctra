#!/bin/bash

# Script pour compiler et exécuter la version sans GUI de Sp3ctra

# Arrêter en cas d'erreur
set -e

# Traitement des arguments de ligne de commande
RUN_AFTER_BUILD=0

while [[ $# -gt 0 ]]; do
  case $1 in
    --run)
      RUN_AFTER_BUILD=1
      shift
      ;;
    *)
      echo "Option non reconnue: $1"
      echo "Usage: $0 [--run]"
      echo "  --run    Exécute le programme après la compilation"
      exit 1
      ;;
  esac
done

# Afficher les commandes exécutées
set -x

# Compiler le projet avec le Makefile
make -j$(nproc 2>/dev/null || echo 2)

echo "Compilation terminée avec succès!"
echo "L'exécutable se trouve dans build/Sp3ctra"

# Exécuter le programme si demandé
if [ "$RUN_AFTER_BUILD" -eq 1 ]; then
  echo "Lancement de l'application..."
  ./build/Sp3ctra
fi
