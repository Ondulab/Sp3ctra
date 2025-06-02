#!/bin/bash

echo "Test de configuration IR après redémarrage"
echo "=========================================="
echo ""

echo "1. Vérification des devices LIRC..."
ls -la /dev/lirc*
echo ""

echo "2. Vérification des GPIO dans dmesg..."
dmesg | grep -i lirc | tail -5
echo ""

echo "3. Test du récepteur IR (GPIO18)..."
echo "Appuyez sur une touche de votre télécommande Sony maintenant..."
echo "Vous devriez voir des 'pulse' et 'space'. Ctrl+C pour arrêter."
echo ""
timeout 10 mode2 -d /dev/lirc0

echo ""
echo "4. Test de l'émetteur IR (GPIO19)..."
echo "Test d'émission IR (LED devrait clignoter)..."
ir-ctl -d /dev/lirc1 --carrier=40000 --duty-cycle=33 --send=sony_power_new.ir

echo ""
echo "Si le récepteur fonctionne, lancez maintenant :"
echo "./capture_real_codes.sh"
