#!/bin/bash

echo "Test de réverbération pour CISYNTH avec contrôleurs MIDI"
echo "========================================================"
echo ""
echo "Ce script va tester les fonctionnalités de réverbération ajoutées à CISYNTH."
echo "Assurez-vous que votre nanoKONTROL2 est connecté avant de continuer."
echo ""
echo "Mapping des contrôleurs MIDI :"
echo "  - Slider/Knob CC 20: Mix (Dry/Wet)"
echo "  - Slider/Knob CC 21: Taille de la pièce"
echo "  - Slider/Knob CC 22: Amortissement"
echo "  - Slider/Knob CC 23: Largeur stéréo"
echo ""
echo "Appuyez sur CTRL+C pour arrêter le test."
echo ""

# Vérifier si l'exécutable existe
if [ ! -f "./build_nogui/CISYNTH_noGUI" ]; then
    echo "Compilation de l'application..."
    ./rebuild_cli.sh
fi

# Exécuter CISYNTH sans interface graphique
echo "Lancement de CISYNTH avec réverbération activée..."
./build_nogui/CISYNTH_noGUI