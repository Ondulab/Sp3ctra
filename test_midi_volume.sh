#!/bin/bash

# Script de test pour la fonctionnalité de contrôle du volume MIDI
# Créé le 15 mai 2025

echo "Test de la fonctionnalité de contrôle du volume MIDI avec Launchkey Mini"
echo "-------------------------------------------------------------------------"

# Vérifier si rtmidi est installé
if ! brew list | grep -q rtmidi; then
    echo "Installation de rtmidi..."
    brew install rtmidi
fi

# Compiler le projet avec les dernières modifications
echo "Compilation du projet..."
qmake CISYNTH_noGUI.pro
make -j4

# Exécuter le programme
echo ""
echo "Lancement de CISYNTH avec le mode DEBUG_MIDI activé"
echo "Veuillez connecter votre Launchkey Mini et utiliser la molette de modulation (CC#7)"
echo "Appuyez sur Ctrl+C pour quitter"
echo ""

# Exécuter avec priorité élevée pour une meilleure stabilité audio
# Sur macOS, l'exécutable est placé dans le bundle .app
if [ -f "./CISYNTH_noGUI.app/Contents/MacOS/CISYNTH_noGUI" ]; then
    echo "Lancement de l'application depuis le bundle macOS"
    ./CISYNTH_noGUI.app/Contents/MacOS/CISYNTH_noGUI
elif [ -f "./CISYNTH_noGUI" ]; then
    echo "Lancement de l'application directement"
    ./CISYNTH_noGUI
else
    echo "ERREUR: Impossible de trouver l'exécutable CISYNTH_noGUI"
    echo "Vérifiez le chemin de l'exécutable dans le projet"
    exit 1
fi

echo ""
echo "Test terminé"