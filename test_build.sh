#!/bin/bash
# Script de test pour compiler et exécuter notre solution

echo "=== Compilation du projet ==="
cd "$(dirname "$0")"
./rebuild_cli.sh

echo ""
echo "=== Test de l'application ==="
# Exécuter pendant 3 secondes puis arrêter
timeout 3 ./build_nogui/CISYNTH_noGUI --cli --no-dmx || echo "Application terminée"
