#!/bin/bash

# Script amélioré pour déployer CISYNTH CLI sur Jetson Nano
# avec fonctionnalités de dépannage automatique

# Valeurs par défaut
JETSON_USER="ondulab"
JETSON_IP="192.168.55.1"
JETSON_DIR="/home/$JETSON_USER/cisynth_cli"
DMX_PORT="/dev/ttyUSB0"  # Port DMX typique sur Linux
SKIP_CHECK=0
TRY_SCAN=0
TTY_PORT="/dev/tty.usbmodem14208210304343"  # Port série par défaut

# Couleurs pour les messages
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Traitement des arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --ip)
      JETSON_IP="$2"
      shift 2
      ;;
    --skip-check)
      SKIP_CHECK=1
      shift
      ;;
    --scan-network)
      TRY_SCAN=1
      shift
      ;;
    --serial-port)
      TTY_PORT="$2"
      shift 2
      ;;
    --help)
      echo "Usage: $0 [options]"
      echo "  --ip <adresse_ip>    Spécifie l'adresse IP de la Jetson Nano"
      echo "  --skip-check         Ignore la vérification de connectivité"
      echo "  --scan-network       Tente de scanner le réseau pour trouver la Jetson"
      echo "  --serial-port <port> Spécifie le port série pour la connexion USB"
      exit 0
      ;;
    *)
      echo "Option non reconnue: $1"
      echo "Utilisez --help pour plus d'informations"
      exit 1
      ;;
  esac
done

echo -e "${BLUE}=== Déploiement de CISYNTH CLI sur Jetson Nano ===${NC}"
echo -e "${BLUE}IP initiale: ${JETSON_IP}${NC}"
echo -e "${BLUE}Utilisateur: ${JETSON_USER}${NC}"
echo -e "${BLUE}Répertoire cible: ${JETSON_DIR}${NC}"

# Fonction pour détecter les ports série disponibles
detect_serial_ports() {
    echo -e "${YELLOW}Recherche de ports série disponibles...${NC}"
    
    # Lister les ports disponibles
    AVAILABLE_PORTS=$(ls -l /dev/tty.usb* /dev/tty.modem* 2>/dev/null | awk '{print $NF}')
    
    if [ -z "$AVAILABLE_PORTS" ]; then
        echo -e "${RED}Aucun port série USB détecté.${NC}"
        echo -e "${YELLOW}Assurez-vous que la Jetson est connectée via USB.${NC}"
        return 1
    fi
    
    # Afficher les ports disponibles
    echo -e "${GREEN}Ports série disponibles:${NC}"
    i=1
    for port in $AVAILABLE_PORTS; do
        echo -e "  ${i}. ${port}"
        i=$((i+1))
    done
    
    # Si le port spécifié existe, l'utiliser
    if [ -e "$TTY_PORT" ]; then
        echo -e "${GREEN}Utilisation du port spécifié: ${TTY_PORT}${NC}"
        return 0
    fi
    
    # Si un seul port est disponible, l'utiliser automatiquement
    if [ $(echo "$AVAILABLE_PORTS" | wc -l) -eq 1 ]; then
        TTY_PORT=$AVAILABLE_PORTS
        echo -e "${GREEN}Un seul port détecté. Utilisation automatique de: ${TTY_PORT}${NC}"
        return 0
    fi
    
    # Sinon, demander à l'utilisateur de choisir un port
    echo -e "${YELLOW}Plusieurs ports détectés. Veuillez en choisir un:${NC}"
    select port in $AVAILABLE_PORTS "Quitter"; do
        if [ "$port" = "Quitter" ]; then
            echo "Opération annulée."
            exit 0
        elif [ -n "$port" ]; then
            TTY_PORT=$port
            echo -e "${GREEN}Port sélectionné: ${TTY_PORT}${NC}"
            return 0
        else
            echo -e "${RED}Sélection invalide. Veuillez réessayer.${NC}"
        fi
    done
}

# Fonction pour scanner le réseau à la recherche de la Jetson
scan_for_jetson() {
    echo -e "${YELLOW}Tentative de détection automatique de la Jetson sur le réseau...${NC}"
    
    # Obtenir l'interface réseau et l'adresse IP locale
    LOCAL_IP=$(ifconfig | grep "inet " | grep -v 127.0.0.1 | awk '{print $2}' | head -n 1)
    if [ -z "$LOCAL_IP" ]; then
        echo -e "${RED}Impossible de déterminer l'adresse IP locale.${NC}"
        return 1
    fi
    
    # Déterminer le préfixe réseau
    NETWORK_PREFIX=$(echo $LOCAL_IP | cut -d. -f1-3)
    echo -e "${YELLOW}Scanning du réseau ${NETWORK_PREFIX}.0/24...${NC}"
    
    # Scanner le réseau (nécessite nmap)
    if ! command -v nmap &> /dev/null; then
        echo -e "${RED}Commande 'nmap' non trouvée. Installation nécessaire pour le scan réseau.${NC}"
        echo -e "${YELLOW}Sur macOS: brew install nmap${NC}"
        echo -e "${YELLOW}Sur Ubuntu/Debian: sudo apt-get install nmap${NC}"
        return 1
    fi
    
    # Scanner les ports 22 (SSH) ouverts sur le réseau
    echo -e "${YELLOW}Recherche des appareils avec le port SSH ouvert...${NC}"
    FOUND_IPS=$(nmap -p 22 --open -sT ${NETWORK_PREFIX}.0/24 | grep "Nmap scan report for" | awk '{print $NF}' | tr -d '()')
    
    if [ -z "$FOUND_IPS" ]; then
        echo -e "${RED}Aucun appareil avec SSH ouvert trouvé sur le réseau.${NC}"
        return 1
    fi
    
    # Afficher les appareils trouvés
    echo -e "${GREEN}Appareils trouvés avec port SSH ouvert:${NC}"
    i=1
    for ip in $FOUND_IPS; do
        hostname=$(ssh -o ConnectTimeout=2 -o BatchMode=yes -o StrictHostKeyChecking=no $JETSON_USER@$ip "hostname" 2>/dev/null || echo "Inconnu")
        echo -e "  ${i}. ${ip} (hostname: ${hostname})"
        i=$((i+1))
    done
    
    # Si un seul appareil est trouvé, proposer de l'utiliser automatiquement
    if [ $(echo "$FOUND_IPS" | wc -l) -eq 1 ]; then
        echo -e "${YELLOW}Un seul appareil SSH trouvé. Voulez-vous utiliser ${FOUND_IPS}? (o/n)${NC}"
        read -n 1 -r
        echo
        if [[ $REPLY =~ ^[Oo]$ ]]; then
            JETSON_IP=$FOUND_IPS
            echo -e "${GREEN}Utilisation de l'adresse IP: ${JETSON_IP}${NC}"
            return 0
        fi
    else
        # Sinon, demander à l'utilisateur de choisir une adresse IP
        echo -e "${YELLOW}Plusieurs appareils trouvés. Veuillez choisir une adresse IP:${NC}"
        select ip in $FOUND_IPS "Aucune de ces adresses"; do
            if [ "$ip" = "Aucune de ces adresses" ]; then
                echo "Sélection annulée."
                return 1
            elif [ -n "$ip" ]; then
                JETSON_IP=$ip
                echo -e "${GREEN}Adresse IP sélectionnée: ${JETSON_IP}${NC}"
                return 0
            else
                echo -e "${RED}Sélection invalide. Veuillez réessayer.${NC}"
            fi
        done
    fi
    
    return 1
}

# Fonction pour se connecter via série et obtenir l'adresse IP
get_ip_via_serial() {
    echo -e "${YELLOW}Tentative d'obtention de l'adresse IP via connexion série...${NC}"
    
    # Vérifier que screen est installé
    if ! command -v screen &> /dev/null; then
        echo -e "${RED}Erreur: La commande 'screen' n'est pas installée.${NC}"
        echo -e "${YELLOW}Installez-la avec: brew install screen${NC}"
        return 1
    fi
    
    # Détecter les ports série disponibles si nécessaire
    if [ ! -e "$TTY_PORT" ]; then
        echo -e "${YELLOW}Le port série spécifié ($TTY_PORT) n'existe pas.${NC}"
        detect_serial_ports || return 1
    fi
    
    echo -e "${YELLOW}Pour obtenir l'adresse IP de la Jetson via la connexion série:${NC}"
    echo -e "1. Une session screen va démarrer pour vous connecter à la Jetson"
    echo -e "2. Connectez-vous avec les identifiants: ondulab / sp3ctra"
    echo -e "3. Exécutez la commande: ip a"
    echo -e "4. Notez l'adresse IP de l'interface eth0 (généralement 192.168.x.x)"
    echo -e "5. Pour quitter la session screen: Ctrl+A puis Ctrl+\\ puis y"
    echo
    read -p "Appuyez sur Entrée pour continuer..."
    
    # Lancer screen pour la connexion série
    sudo screen $TTY_PORT 115200
    
    # Demander l'adresse IP trouvée
    echo
    echo -e "${YELLOW}Avez-vous trouvé l'adresse IP de la Jetson? (o/n)${NC}"
    read -n 1 -r
    echo
    if [[ $REPLY =~ ^[Oo]$ ]]; then
        read -p "Veuillez entrer l'adresse IP trouvée: " new_ip
        if [[ $new_ip =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            JETSON_IP=$new_ip
            echo -e "${GREEN}Nouvelle adresse IP définie: ${JETSON_IP}${NC}"
            return 0
        else
            echo -e "${RED}Format d'adresse IP invalide.${NC}"
            return 1
        fi
    fi
    
    return 1
}

# Vérifier la connectivité
check_connectivity() {
    echo -e "${YELLOW}Vérification de la connexion à la Jetson Nano (${JETSON_IP})...${NC}"
    
    # Vérifier si on peut pinger la Jetson
    if ping -c 1 $JETSON_IP &> /dev/null; then
        echo -e "${GREEN}Ping réussi à l'adresse ${JETSON_IP}${NC}"
        
        # Tester la connexion SSH
        if ssh -o ConnectTimeout=5 -o BatchMode=yes -o StrictHostKeyChecking=no $JETSON_USER@$JETSON_IP "echo 'SSH OK'" &> /dev/null; then
            echo -e "${GREEN}Connexion SSH réussie à ${JETSON_USER}@${JETSON_IP}${NC}"
            return 0
        else
            echo -e "${RED}Ping réussi mais connexion SSH échouée.${NC}"
            echo -e "${YELLOW}Vérifiez que le service SSH est actif et que les identifiants sont corrects.${NC}"
            return 1
        fi
    else
        echo -e "${RED}Ping échoué pour l'adresse ${JETSON_IP}${NC}"
        return 1
    fi
}

# Fonction principale de déploiement
deploy_to_jetson() {
    # Créer un répertoire temporaire pour préparer les fichiers
    echo -e "${YELLOW}Préparation des fichiers...${NC}"
    TMP_DIR=$(mktemp -d)
    mkdir -p $TMP_DIR/src/core

    # Copier les fichiers nécessaires
    cp CISYNTH_noGUI.pro $TMP_DIR/
    cp build_nogui.sh $TMP_DIR/
    cp run_on_jetson.sh $TMP_DIR/
    cp src/core/*.c $TMP_DIR/src/core/
    cp src/core/*.h $TMP_DIR/src/core/

    # Modifier le port DMX dans config.h pour Linux
    echo -e "${YELLOW}Adaptation du port DMX pour Linux...${NC}"
    sed 's|#define DMX_PORT.*|#define DMX_PORT "'$DMX_PORT'"|g' src/core/config.h > $TMP_DIR/src/core/config.h

    # Créer le répertoire distant
    echo -e "${YELLOW}Création du répertoire sur la Jetson Nano...${NC}"
    ssh $JETSON_USER@$JETSON_IP "mkdir -p $JETSON_DIR/src/core"

    # Transférer les fichiers
    echo -e "${YELLOW}Transfert des fichiers vers la Jetson Nano...${NC}"
    scp -r $TMP_DIR/* $JETSON_USER@$JETSON_IP:$JETSON_DIR/

    # Nettoyer le répertoire temporaire
    rm -rf $TMP_DIR

    # Rendre les scripts exécutables
    echo -e "${YELLOW}Configuration des permissions...${NC}"
    ssh $JETSON_USER@$JETSON_IP "chmod +x $JETSON_DIR/build_nogui.sh $JETSON_DIR/run_on_jetson.sh"

    # Installer les dépendances nécessaires
    echo -e "${YELLOW}Vérification des dépendances...${NC}"
    ssh $JETSON_USER@$JETSON_IP "sudo apt-get update && sudo apt-get install -y build-essential qmake libfftw3-dev libsndfile-dev libsfml-dev libcsfml-dev"

    # Ajouter l'utilisateur au groupe dialout pour accéder au port DMX
    echo -e "${YELLOW}Configuration des droits pour le port DMX...${NC}"
    ssh $JETSON_USER@$JETSON_IP "sudo usermod -a -G dialout $JETSON_USER"

    echo -e "${GREEN}Déploiement terminé!${NC}"
    echo -e "${YELLOW}Pour exécuter CISYNTH sur la Jetson Nano:${NC}"
    echo -e "${BLUE}1. Connectez-vous à la Jetson: ${NC}ssh $JETSON_USER@$JETSON_IP"
    echo -e "${BLUE}2. Naviguez vers le répertoire: ${NC}cd $JETSON_DIR"
    echo -e "${BLUE}3. Exécutez le script: ${NC}./run_on_jetson.sh"
    
    return 0
}

# Algorithme principal
CONNECTIVITY_OK=0

# Étape 1: Vérifier la connectivité si --skip-check n'est pas utilisé
if [ $SKIP_CHECK -eq 0 ]; then
    if check_connectivity; then
        CONNECTIVITY_OK=1
    else
        echo -e "${RED}Problème de connectivité avec la Jetson à l'adresse ${JETSON_IP}${NC}"
    fi
else
    echo -e "${YELLOW}Vérification de connectivité ignorée sur demande.${NC}"
    CONNECTIVITY_OK=1
fi

# Étape 2: Si la connectivité échoue, proposer des alternatives
if [ $CONNECTIVITY_OK -eq 0 ]; then
    echo -e "${YELLOW}Options de dépannage:${NC}"
    
    # Option 1: Scanner le réseau
    if [ $TRY_SCAN -eq 1 ] || { echo -e "1. Scanner le réseau pour trouver la Jetson"; \
                              echo -e "2. Se connecter via le port série pour obtenir l'adresse IP"; \
                              echo -e "3. Spécifier manuellement une autre adresse IP"; \
                              echo -e "4. Quitter"; \
                              read -p "Choisissez une option (1-4): " opt && [ "$opt" = "1" ]; }; then
        if scan_for_jetson; then
            if check_connectivity; then
                CONNECTIVITY_OK=1
            fi
        fi
    fi
    
    # Option 2: Obtenir l'IP via connexion série
    if [ $CONNECTIVITY_OK -eq 0 ] && { [ -n "$SERIAL_CONNECT" ] || [ "$opt" = "2" ]; }; then
        if get_ip_via_serial; then
            if check_connectivity; then
                CONNECTIVITY_OK=1
            fi
        fi
    fi
    
    # Option 3: Saisie manuelle de l'adresse IP
    if [ $CONNECTIVITY_OK -eq 0 ] && [ "$opt" = "3" ]; then
        read -p "Veuillez entrer l'adresse IP de la Jetson: " new_ip
        if [[ $new_ip =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            JETSON_IP=$new_ip
            echo -e "${GREEN}Nouvelle adresse IP définie: ${JETSON_IP}${NC}"
            if check_connectivity; then
                CONNECTIVITY_OK=1
            fi
        else
            echo -e "${RED}Format d'adresse IP invalide.${NC}"
        fi
    fi
    
    # Option 4: Quitter
    if [ $CONNECTIVITY_OK -eq 0 ] && [ "$opt" = "4" ]; then
        echo -e "${YELLOW}Opération annulée par l'utilisateur.${NC}"
        exit 0
    fi
fi

# Étape 3: Si la connectivité est toujours un problème, proposer de forcer le déploiement
if [ $CONNECTIVITY_OK -eq 0 ]; then
    echo -e "${RED}Impossible d'établir une connexion avec la Jetson Nano.${NC}"
    echo -e "${YELLOW}Voulez-vous forcer le déploiement sans vérification de connectivité? (o/n)${NC}"
    read -n 1 -r
    echo
    if [[ $REPLY =~ ^[Oo]$ ]]; then
        CONNECTIVITY_OK=1
        SKIP_CHECK=1
        echo -e "${YELLOW}Déploiement forcé activé.${NC}"
    else
        echo -e "${YELLOW}Déploiement annulé.${NC}"
        exit 1
    fi
fi

# Étape 4: Procéder au déploiement si la connectivité est OK
if [ $CONNECTIVITY_OK -eq 1 ]; then
    echo -e "${GREEN}Connectivité établie avec ${JETSON_USER}@${JETSON_IP}${NC}"
    
    # Déployer
    if deploy_to_jetson; then
        # Option pour exécuter directement
        read -p "Voulez-vous exécuter CISYNTH maintenant? (o/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Oo]$ ]]; then
            echo -e "${YELLOW}Exécution de CISYNTH sur la Jetson Nano...${NC}"
            echo -e "${YELLOW}(Utilisez Ctrl+C pour arrêter l'application)${NC}"
            ssh -t $JETSON_USER@$JETSON_IP "cd $JETSON_DIR && ./run_on_jetson.sh"
        fi
    else
        echo -e "${RED}Erreur lors du déploiement.${NC}"
        exit 1
    fi
else
    echo -e "${RED}Impossible d'établir une connexion avec la Jetson Nano.${NC}"
    echo -e "${YELLOW}Veuillez vérifier:${NC}"
    echo -e "1. Que la Jetson est allumée et connectée au réseau"
    echo -e "2. Que vous êtes sur le même réseau que la Jetson"
    echo -e "3. Que l'adresse IP est correcte (utilisez la connexion série pour vérifier)"
    exit 1
fi

echo -e "${GREEN}Terminé!${NC}"
exit 0
