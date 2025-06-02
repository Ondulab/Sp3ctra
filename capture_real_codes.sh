#!/bin/bash

echo "Script pour capturer les vrais codes IR de votre télécommande Sony HT-X8500"
echo "================================================================"
echo ""
echo "Assurez-vous d'avoir un récepteur IR connecté au GPIO du Pi"
echo ""

# Arrêter lircd s'il tourne
echo "Arrêt de lircd..."
sudo systemctl stop lircd

echo ""
echo "1. Test de réception IR..."
echo "Appuyez sur le bouton POWER de votre télécommande (5 secondes)"
timeout 5 mode2 -d /dev/lirc0

echo ""
echo "2. Apprentissage du code Power avec irrecord..."
echo "Suivez les instructions et appuyez sur POWER quand demandé"

# Créer le fichier de configuration
irrecord -d /dev/lirc0 ~/sony_ht_x8500_learned.lircd.conf

echo ""
echo "3. Capture raw du signal Power..."
echo "Appuyez sur POWER maintenant (10 secondes)..."
timeout 10 mode2 -d /dev/lirc0 -m > ~/power_raw_capture.txt

echo ""
echo "Fichiers créés:"
echo "- ~/sony_ht_x8500_learned.lircd.conf (configuration apprise)"
echo "- ~/power_raw_capture.txt (signal brut)"
echo ""
echo "Redémarrage de lircd..."
sudo systemctl start lircd
