#!/bin/bash

# Script d'installation de la règle udev pour l'adaptateur DMX Sp3ctra
# Utilise un adaptateur FT232 USB-Serial pour contrôle DMX

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UDEV_RULE_FILE="99-sp3ctra-dmx.rules"
UDEV_RULE_PATH="/etc/udev/rules.d/${UDEV_RULE_FILE}"

echo "=== Installation de la règle udev pour DMX Sp3ctra ==="

# Vérifier que le fichier de règle existe
if [ ! -f "${SCRIPT_DIR}/${UDEV_RULE_FILE}" ]; then
    echo "❌ Erreur: fichier de règle udev non trouvé: ${SCRIPT_DIR}/${UDEV_RULE_FILE}"
    exit 1
fi

# Vérifier les permissions (doit être exécuté en root)
if [ "$EUID" -ne 0 ]; then
    echo "❌ Ce script doit être exécuté en tant que root"
    echo "Utilisation: sudo $0"
    exit 1
fi

# Copier la règle udev
echo "📄 Installation de la règle udev..."
cp "${SCRIPT_DIR}/${UDEV_RULE_FILE}" "${UDEV_RULE_PATH}"
chmod 644 "${UDEV_RULE_PATH}"
echo "✅ Règle installée: ${UDEV_RULE_PATH}"

# Recharger les règles udev
echo "🔄 Rechargement des règles udev..."
udevadm control --reload-rules
udevadm trigger

# Attendre que les règles soient appliquées
sleep 2

# Vérifier l'installation
echo ""
echo "=== Vérification de l'installation ==="

# Chercher l'adaptateur FT232
if lsusb | grep -q "0403:6001"; then
    echo "✅ Adaptateur FT232 détecté"
    
    # Vérifier si le lien symbolique existe
    if [ -L "/dev/sp3ctra-dmx" ]; then
        echo "✅ Lien symbolique /dev/sp3ctra-dmx créé avec succès"
        echo "📍 Lien vers: $(readlink /dev/sp3ctra-dmx)"
        
        # Vérifier les permissions
        ls -la /dev/sp3ctra-dmx
        
        echo ""
        echo "🎉 Installation terminée avec succès !"
        echo ""
        echo "Configuration DMX prête:"
        echo "  - Port DMX: /dev/sp3ctra-dmx"
        echo "  - Baud rate: 250000 (avec termios2)"
        echo "  - Adaptateur: FT232 USB-Serial"
        echo ""
        echo "Vous pouvez maintenant recompiler et lancer Sp3ctra:"
        echo "  make clean && make"
        echo "  ./build/Sp3ctra"
        
    else
        echo "⚠️  Lien symbolique /dev/sp3ctra-dmx non créé"
        echo "Vérifiez que l'adaptateur USB est bien branché"
        
        # Afficher les ports série disponibles
        echo ""
        echo "Ports série détectés:"
        ls -la /dev/tty{USB,ACM}* 2>/dev/null || echo "Aucun port série USB détecté"
    fi
else
    echo "❌ Adaptateur FT232 (0403:6001) non détecté"
    echo ""
    echo "Adaptateurs USB connectés:"
    lsusb
    echo ""
    echo "Vérifiez que l'adaptateur DMX est bien branché"
fi

echo ""
echo "=== Informations de dépannage ==="
echo "- Règle udev: ${UDEV_RULE_PATH}"
echo "- Log udev: journalctl -f | grep udev"
echo "- Test manuel: udevadm test /sys/class/tty/ttyUSB0 2>&1"
