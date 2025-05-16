#!/bin/bash

# Script pour configurer l'authentification SSH par clé pour la Jetson Nano
# Permet de se connecter sans saisir de mot de passe

# Couleurs pour les messages
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Paramètres par défaut
JETSON_USER="ondulab"
JETSON_IP="192.168.55.1"

# Traitement des arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --ip)
      JETSON_IP="$2"
      shift 2
      ;;
    --user)
      JETSON_USER="$2"
      shift 2
      ;;
    --help)
      echo "Usage: $0 [--ip adresse_ip] [--user utilisateur]"
      echo "  --ip         Spécifie l'adresse IP de la Jetson Nano (défaut: 192.168.55.1)"
      echo "  --user       Spécifie l'utilisateur de la Jetson Nano (défaut: ondulab)"
      exit 0
      ;;
    *)
      echo "Option non reconnue: $1"
      echo "Utilisez --help pour plus d'informations"
      exit 1
      ;;
  esac
done

echo -e "${GREEN}=== Configuration de l'authentification SSH par clé pour ${JETSON_USER}@${JETSON_IP} ===${NC}"

# Vérifier si la clé SSH existe déjà
SSH_KEY="$HOME/.ssh/id_rsa"
if [ ! -f "$SSH_KEY" ]; then
    echo -e "${YELLOW}Génération d'une nouvelle paire de clés SSH...${NC}"
    ssh-keygen -t rsa -b 4096 -f "$SSH_KEY" -N ""
else
    echo -e "${YELLOW}Clé SSH existante trouvée à $SSH_KEY${NC}"
fi

# Vérifier si on peut contacter la Jetson
echo -e "${YELLOW}Vérification de la connexion à la Jetson Nano...${NC}"
if ! ping -c 1 $JETSON_IP &> /dev/null; then
    echo -e "${RED}Impossible de contacter la Jetson Nano à l'adresse $JETSON_IP${NC}"
    echo -e "${YELLOW}Vérifiez que la Jetson est allumée et connectée au réseau.${NC}"
    exit 1
fi

# Copier la clé publique sur la Jetson
echo -e "${YELLOW}Copie de la clé publique sur la Jetson Nano...${NC}"
echo -e "${YELLOW}Vous devrez saisir le mot de passe pour ${JETSON_USER}@${JETSON_IP} UNE DERNIÈRE FOIS${NC}"

# Créer le répertoire .ssh sur la Jetson si nécessaire
ssh $JETSON_USER@$JETSON_IP "mkdir -p ~/.ssh && chmod 700 ~/.ssh"

# Copier la clé et la configurer correctement
cat "$HOME/.ssh/id_rsa.pub" | ssh $JETSON_USER@$JETSON_IP "cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"

# Vérifier si la configuration a fonctionné
echo -e "${YELLOW}Vérification de la connexion SSH sans mot de passe...${NC}"
if ssh -o BatchMode=yes -o ConnectTimeout=5 $JETSON_USER@$JETSON_IP "echo 'SSH OK'" &> /dev/null; then
    echo -e "${GREEN}Configuration réussie!${NC}"
    echo -e "${GREEN}Vous pouvez maintenant vous connecter à ${JETSON_USER}@${JETSON_IP} sans mot de passe.${NC}"
    
    # Proposer de modifier les scripts de déploiement pour utiliser l'option -o BatchMode=yes
    echo -e "${YELLOW}Voulez-vous aussi modifier les scripts de déploiement pour éliminer les invites sudo? (o/n)${NC}"
    read -n 1 -r
    echo
    if [[ $REPLY =~ ^[Oo]$ ]]; then
        echo -e "${YELLOW}Configuration des scripts pour utiliser sudo sans mot de passe...${NC}"
        
        # Configurer sudo sans mot de passe pour l'utilisateur ondulab sur la Jetson
        echo -e "${YELLOW}Configuration de sudo sans mot de passe sur la Jetson Nano...${NC}"
        echo -e "${YELLOW}Note: Ceci nécessite des droits sudo sur la Jetson.${NC}"
        
        SUDOERS_CONFIG="$JETSON_USER ALL=(ALL) NOPASSWD: ALL"
        ssh $JETSON_USER@$JETSON_IP "echo '$SUDOERS_CONFIG' | sudo tee /etc/sudoers.d/010_$JETSON_USER-nopasswd > /dev/null && sudo chmod 440 /etc/sudoers.d/010_$JETSON_USER-nopasswd"
        
        echo -e "${GREEN}Configuration sudo terminée!${NC}"
    fi
    
    echo -e "${GREEN}Configuration SSH terminée!${NC}"
    echo -e "${YELLOW}Testez avec: ${NC}ssh $JETSON_USER@$JETSON_IP"
    echo -e "${YELLOW}Les scripts de déploiement ne vous demanderont plus de mot de passe.${NC}"
else
    echo -e "${RED}Échec de la configuration. Vérifiez les erreurs et réessayez.${NC}"
    exit 1
fi
