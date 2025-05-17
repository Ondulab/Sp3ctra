#!/bin/bash
# Script de test pour vérifier les envois de réverbération individuels pour chaque synthétiseur

echo "Compilation du projet..."
cd "$(dirname "$0")"
make -C build_nogui/ -j4

if [ $? -ne 0 ]; then
  echo "Erreur de compilation"
  exit 1
fi

echo "Exécution du test de réverbération individuelle..."
echo "----------------------------------------------------------------"
echo "Contrôleurs MIDI:"
echo "CC 2: Niveau de mix synth IFFT"
echo "CC 3: Niveau de mix synth FFT"
echo "CC 4: Niveau d'envoi réverb pour synth FFT"
echo "CC 5: Niveau d'envoi réverb pour synth IFFT"
echo "CC 20: Mix global réverbération"
echo "----------------------------------------------------------------"
echo "Démarrage de l'application. Envoyez des messages MIDI CC pour tester."
echo "Appuyez sur Ctrl+C pour terminer le test."
echo ""

# Exécuter l'application avec tous les logs d'entrée activés
./build_nogui/cisynth_nogui

echo "Test terminé."
