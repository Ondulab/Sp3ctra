#!/bin/bash

echo "Test de réverbération pour CISYNTH avec contrôleurs MIDI"
echo "========================================================"
echo ""
echo "Ce script va tester les fonctionnalités de réverbération ajoutées à CISYNTH."
echo "Assurez-vous que votre Launchkey Mini MK3 est connecté avant de continuer."
echo ""
echo "Mapping des contrôleurs MIDI :"
echo "  - Bouton CC 21: Mix (Dry/Wet)"
echo "  - Bouton CC 22: Taille de la pièce" 
echo "  - Bouton CC 23: Amortissement"
echo "  - Bouton CC 24: Largeur stéréo"
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
