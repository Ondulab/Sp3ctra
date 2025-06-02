#!/bin/bash

# Script de déploiement pour Sony HT-X8500 IR sur Raspberry Pi
# Usage: ./deploy_to_pi.sh [pi_user@pi_ip]

PI_USER=${1:-sp3ctra@pi.local}
SSH_OPTS="-i ~/.ssh/id_rsa -o IdentitiesOnly=yes"
REMOTE_DIR="/home/sp3ctra/sony_ir"

echo "Déploiement des fichiers IR Sony HT-X8500 vers $PI_USER"
echo "Répertoire cible: $REMOTE_DIR"
echo ""

# Créer le répertoire distant
echo "Création du répertoire distant..."
ssh $SSH_OPTS $PI_USER "mkdir -p $REMOTE_DIR"

# Copier tous les fichiers IR
echo "Copie des fichiers IR..."
scp $SSH_OPTS sony_power_new.ir sony_volume_up.ir sony_volume_down.ir sony_mute.ir $PI_USER:$REMOTE_DIR/

# Copier le script de test
echo "Copie du script de test..."
scp $SSH_OPTS test_sony_ir.sh $PI_USER:$REMOTE_DIR/

# Copier la configuration LIRC
echo "Copie de la configuration LIRC..."
scp $SSH_OPTS sony_ht_x8500.lircd.conf $PI_USER:$REMOTE_DIR/

# Copier le README
echo "Copie de la documentation..."
scp $SSH_OPTS README_Sony_HT_X8500_IR.md $PI_USER:$REMOTE_DIR/

# Rendre le script de test exécutable
echo "Configuration des permissions..."
ssh $SSH_OPTS $PI_USER "chmod +x $REMOTE_DIR/test_sony_ir.sh"

echo ""
echo "Déploiement terminé !"
echo ""
echo "Prochaines étapes sur le Raspberry Pi :"
echo "1. Connectez-vous : ssh $PI_USER"
echo "2. Allez dans le répertoire : cd $REMOTE_DIR"
echo "3. Testez directement : ir-ctl -d /dev/lirc0 --carrier=40000 --duty-cycle=33 --send=sony_power_new.ir"
echo "4. Ou utilisez le script : ./test_sony_ir.sh"
echo ""
echo "Pour installer LIRC (optionnel) :"
echo "sudo cp sony_ht_x8500.lircd.conf /etc/lirc/lircd.conf.d/"
echo "sudo systemctl restart lircd"
