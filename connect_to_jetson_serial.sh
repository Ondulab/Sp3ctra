#!/bin/bash

# Script pour se connecter à la Jetson Nano via port série et récupérer son IP
# Utilise picocom au lieu de screen
# Usage: ./connect_to_jetson_serial.sh [--port PORT]

# Port série par défaut
TTY_PORT="/dev/tty.usbmodem14208210304343"
BAUD_RATE=115200

# Couleurs
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Traitement des arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --port)
            TTY_PORT="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [--port PORT]"
            echo "  --port      Spécifie le port série (par défaut: $TTY_PORT)"
            exit 0
            ;;
        *)
            echo -e "${RED}Option non reconnue: $1${NC}"
            echo "Utilisez --help pour plus d'informations"
            exit 1
            ;;
    esac
done

# Détection des ports disponibles
detect_ports() {
    echo -e "${YELLOW}Recherche de ports série disponibles...${NC}"
    AVAILABLE_PORTS=$(ls -l /dev/tty.usb* /dev/tty.modem* 2>/dev/null | awk '{print $NF}')
    
    if [ -z "$AVAILABLE_PORTS" ]; then
        echo -e "${RED}Aucun port série USB détecté.${NC}"
        echo -e "${YELLOW}Assurez-vous que la Jetson est connectée via USB.${NC}"
        return 1
    fi

    echo -e "${GREEN}Ports série disponibles :${NC}"
    i=1
    for port in $AVAILABLE_PORTS; do
        echo "  $i. $port"
        i=$((i+1))
    done

    if [ -e "$TTY_PORT" ]; then
        echo -e "${GREEN}Utilisation du port spécifié : $TTY_PORT${NC}"
        return 0
    fi

    if [ $(echo "$AVAILABLE_PORTS" | wc -l) -eq 1 ]; then
        TTY_PORT=$AVAILABLE_PORTS
        echo -e "${GREEN}Un seul port détecté : $TTY_PORT${NC}"
        return 0
    fi

    echo -e "${YELLOW}Plusieurs ports détectés. Veuillez en choisir un :${NC}"
    select port in $AVAILABLE_PORTS "Quitter"; do
        if [ "$port" = "Quitter" ]; then
            echo "Opération annulée."
            exit 0
        elif [ -n "$port" ]; then
            TTY_PORT=$port
            echo -e "${GREEN}Port sélectionné : $TTY_PORT${NC}"
            return 0
        else
            echo -e "${RED}Sélection invalide. Veuillez réessayer.${NC}"
        fi
    done
}

# Vérification de l'existence du port
if [ ! -e "$TTY_PORT" ]; then
    echo -e "${YELLOW}Le port série spécifié ($TTY_PORT) n'existe pas.${NC}"
    detect_ports || exit 1
fi

# Vérification de picocom
if ! command -v picocom &> /dev/null; then
    echo -e "${RED}Erreur : picocom n'est pas installé.${NC}"
    echo -e "${YELLOW}Installez-le avec : sudo apt install picocom${NC}"
    exit 1
fi

# Instructions
echo -e "${GREEN}=== Connexion à la Jetson Nano via picocom ===${NC}"
echo -e "${YELLOW}Port      : $TTY_PORT${NC}"
echo -e "${YELLOW}Baud rate : $BAUD_RATE${NC}"
echo -e "${YELLOW}Identifiants : ondulab / sp3ctra${NC}"
echo
echo -e "${YELLOW}Instructions :${NC}"
echo -e "  - Pour quitter picocom : ${GREEN}Ctrl+A puis Ctrl+X${NC}"
echo -e "  - Pour trouver l'adresse IP : ${GREEN}ip a${NC} ou ${GREEN}ifconfig${NC}"
echo
echo -e "${YELLOW}Démarrage de la connexion série...${NC}"
sleep 1

# Connexion série
sudo picocom -b "$BAUD_RATE" "$TTY_PORT"