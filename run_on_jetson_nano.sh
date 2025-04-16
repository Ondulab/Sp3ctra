#!/bin/bash

# Script optimisé pour lancer CISYNTH sur Jetson Nano avec des paramètres audio optimisés
# Ce script doit être exécuté sur le Jetson Nano lui-même

echo "Lancement de CISYNTH optimisé pour Jetson Nano..."

# Vérifier si l'exécutable existe
if [ ! -f "build_nogui/CISYNTH_noGUI" ]; then
    echo "Erreur: L'exécutable n'existe pas. Veuillez d'abord compiler avec ./rebuild_cli.sh"
    exit 1
fi

# Liste des périphériques audio disponibles
echo "Liste des périphériques audio disponibles:"
./build_nogui/CISYNTH_noGUI --list-audio-devices

# Exécution avec les paramètres optimisés
# --cli : Mode ligne de commande (sans interface graphique)
# --no-dmx : Désactiver le DMX si vous n'utilisez pas de contrôleur DMX
# --audio-device=11 : Remplacez par le numéro de périphérique approprié (celui qui fonctionne)

echo ""
echo "Lancement de l'application avec paramètres optimisés..."
echo "Appuyez sur Ctrl+C pour quitter."
echo ""

./build_nogui/CISYNTH_noGUI --cli --no-dmx --audio-device=11

exit 0
