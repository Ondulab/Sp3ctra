#!/bin/bash

echo "Script de correction de la configuration GPIO pour IR"
echo "====================================================="
echo ""

# Sauvegarde du fichier config.txt
echo "Sauvegarde de /boot/config.txt..."
sudo cp /boot/config.txt /boot/config.txt.backup

# Supprimer les anciennes lignes LIRC s'il y en a
echo "Nettoyage des anciennes configurations IR..."
sudo sed -i '/dtoverlay=gpio-ir/d' /boot/config.txt
sudo sed -i '/dtoverlay=gpio-ir-tx/d' /boot/config.txt
sudo sed -i '/dtparam=gpio_in_pin/d' /boot/config.txt
sudo sed -i '/dtparam=gpio_out_pin/d' /boot/config.txt

# Ajouter la nouvelle configuration
echo "Ajout de la nouvelle configuration IR..."
echo "" | sudo tee -a /boot/config.txt
echo "# Configuration LIRC - Récepteur et Émetteur IR" | sudo tee -a /boot/config.txt
echo "dtoverlay=gpio-ir,gpio_pin=18" | sudo tee -a /boot/config.txt
echo "dtoverlay=gpio-ir-tx,gpio_pin=19" | sudo tee -a /boot/config.txt

echo ""
echo "Configuration mise à jour :"
echo "- GPIO18 : Récepteur IR (entrée)"
echo "- GPIO19 : Émetteur IR (sortie)"
echo ""

# Afficher la config actuelle
echo "Contenu de la section IR dans config.txt :"
grep -A2 -B2 "gpio-ir" /boot/config.txt

echo ""
echo "REDÉMARRAGE NÉCESSAIRE pour appliquer les changements !"
echo ""
echo "Après redémarrage, vérifiez avec :"
echo "ls -la /dev/lirc*"
echo ""
echo "Vous devriez voir :"
echo "- /dev/lirc0 (récepteur)"  
echo "- /dev/lirc1 (émetteur)"
echo ""
echo "Voulez-vous redémarrer maintenant ? (y/n)"
read -r response
if [[ "$response" =~ ^[Yy]$ ]]; then
    echo "Redémarrage..."
    sudo reboot
else
    echo "Redémarrage annulé. N'oubliez pas de redémarrer avec : sudo reboot"
fi
