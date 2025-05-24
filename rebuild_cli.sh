#!/bin/bash

# Script pour nettoyer et reconstruire l'application CISYNTH en mode CLI
# Ce script nettoie le build précédent pour s'assurer que les changements 
# dans la configuration sont pris en compte

echo "Nettoyage des fichiers de build précédents..."
# rm -rf build_nogui # Commenté pour ne plus supprimer le répertoire build_nogui

echo "Compilation du projet en mode CLI..."
./build_nogui.sh --cli

echo "Vérification du résultat..."
if [ -f "build_nogui/CISYNTH_noGUI" ]; then
    echo "✅ Compilation réussie! Un exécutable en ligne de commande standard a été créé."
    echo "Pour exécuter l'application en mode CLI:"
    echo "  ./build_nogui/CISYNTH_noGUI --cli"
    echo ""
    echo "Options DMX disponibles:"
    echo "  --no-dmx            Désactive complètement le DMX"
    echo "  --dmx-port=<port>   Spécifie le port DMX à utiliser"
    echo "  --silent-dmx        Supprime les messages d'erreur DMX"
elif [ -d "build_nogui/CISYNTH_noGUI.app" ]; then
    echo "⚠️ L'application a été compilée comme un bundle macOS (.app)."
    echo "Pour exécuter l'application:"
    echo "  ./build_nogui/CISYNTH_noGUI.app/Contents/MacOS/CISYNTH_noGUI --cli"
    echo ""
    echo "Options DMX disponibles:"
    echo "  --no-dmx            Désactive complètement le DMX"
    echo "  --dmx-port=<port>   Spécifie le port DMX à utiliser"
    echo "  --silent-dmx        Supprime les messages d'erreur DMX"
else
    echo "❌ Erreur: Impossible de trouver l'exécutable compilé."
    exit 1
fi

echo ""
echo "Pour tester directement:"
echo "./build_nogui.sh --cli --run"
echo ""
echo "Exemples d'utilisation avec les options DMX:"
echo "./build_nogui/CISYNTH_noGUI --cli --no-dmx"
echo "./build_nogui/CISYNTH_noGUI --cli --dmx-port=/dev/tty.usbserial-XXX"
echo "./build_nogui/CISYNTH_noGUI --cli --silent-dmx"
echo ""
echo "Pour quitter l'application: Appuyez sur Ctrl+C"

echo ""
echo "-----------------------------------------------------"
echo "Listing available audio devices for CISYNTH_noGUI:"
echo "-----------------------------------------------------"
./build_nogui/CISYNTH_noGUI --cli --no-dmx --list-audio-devices
echo "-----------------------------------------------------"

exit 0
