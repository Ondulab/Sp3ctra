#!/bin/bash

# Script pour tester la correction du problème audio sur Raspberry Pi

echo "=========================================="
echo "Test de la correction audio device"
echo "=========================================="

# Construire le projet
echo "1. Compilation du projet..."
./clean_and_rebuild.sh

if [ $? -ne 0 ]; then
    echo "❌ Erreur lors de la compilation"
    exit 1
fi

echo "✅ Compilation réussie"

# Déployer sur la Pi
echo "2. Déploiement sur la Pi..."
./deploy_to_pi.sh

if [ $? -ne 0 ]; then
    echo "❌ Erreur lors du déploiement"
    exit 1
fi

echo "✅ Déploiement réussi"

# Tester avec périphérique USB SPDIF
echo "3. Test avec périphérique audio USB SPDIF (ID 3)..."
echo "Connexion SSH et test de l'application..."

ssh sp3ctra@192.168.1.111 << 'EOF'
cd ~/Sp3ctra_Application

echo "Listage des périphériques audio disponibles:"
./build_nogui/CISYNTH_noGUI --list-audio-devices

echo ""
echo "Test avec --audio-device 3:"
timeout 10s ./build_nogui/CISYNTH_noGUI --audio-device 3 --no-dmx || true

echo ""
echo "Test avec --audio-device 4:"
timeout 10s ./build_nogui/CISYNTH_noGUI --audio-device 4 --no-dmx || true
EOF

echo "✅ Tests terminés"
echo "=========================================="
echo "Vérifiez les logs ci-dessus pour voir si:"
echo "1. Les périphériques USB SPDIF sont détectés (ID 3 et 4)"
echo "2. L'application utilise le bon périphérique"
echo "3. Il n'y a plus d'erreur 'RtAudio error'"
echo "=========================================="
