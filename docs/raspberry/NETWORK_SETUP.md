# Configuration Réseau du Raspberry Pi

Ce document décrit la procédure de configuration réseau du Raspberry Pi pour le projet Sp3ctra, utilisant le script `setup_network.sh`.

## Prérequis

- Raspberry Pi sous Debian Bookworm
- NetworkManager installé (`sudo apt install network-manager`)
- Utilitaire iw installé (`sudo apt install iw`)
- Droits root (sudo)

## Configuration

Le script configure automatiquement :

1. Interface Ethernet (eth0) :
   - IP statique : 192.168.100.10
   - Métrique de routage : 100 (prioritaire)
   - Connexion automatique au démarrage

2. Interface WiFi (wlan0) :
   - Configuration via paramètres SSID/PSK
   - Métrique de routage : 200 (secondaire)
   - Connexion automatique au démarrage

## Utilisation

1. Rendre le script exécutable :
```bash
chmod +x setup_network.sh
```

2. Exécuter le script avec les paramètres requis :
```bash
sudo ./setup_network.sh --ssid "NOM_WIFI" --psk "MOT_DE_PASSE_WIFI"
```

Options disponibles :
- `--ssid` : Nom du réseau WiFi (requis)
- `--psk` : Mot de passe du réseau WiFi (requis)
- `--country` : Code pays pour les régulations WiFi (optionnel, par défaut : FR)

## Vérification

Le script affiche automatiquement :
- Le statut des interfaces réseau
- La configuration IP
- La table de routage

Pour vérifier manuellement après coup :
```bash
# Statut des connexions
nmcli device status

# Configuration IP
ip addr show

# Table de routage
ip route
```

## Priorité des Connexions

Le script configure les métriques de routage pour :
1. Privilégier l'interface Ethernet (eth0) pour le trafic UDP sur 192.168.100.10
2. Utiliser le WiFi comme connexion secondaire

## Dépannage

1. Si la connexion Ethernet échoue :
   - Vérifier le câble physique
   - Vérifier que l'IP 192.168.100.10 n'est pas déjà utilisée
   ```bash
   nmcli connection show eth0-static
   ```

2. Si la connexion WiFi échoue :
   - Vérifier que le SSID et le mot de passe sont corrects
   - Vérifier la force du signal
   ```bash
   nmcli device wifi list
   ```

3. Pour redémarrer une connexion :
   ```bash
   # Pour Ethernet
   sudo nmcli connection down eth0-static
   sudo nmcli connection up eth0-static
   
   # Pour WiFi
   sudo nmcli connection down wifi-managed
   sudo nmcli connection up wifi-managed
