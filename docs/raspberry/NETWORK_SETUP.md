# Configuration Réseau du Raspberry Pi

Ce document décrit la procédure de configuration réseau du Raspberry Pi pour le projet Sp3ctra, utilisant le script amélioré `setup_network.sh`.

## Prérequis

- Raspberry Pi sous Debian Bookworm
- NetworkManager installé (`sudo apt install network-manager`)
- Utilitaire iw installé (`sudo apt install iw`)
- Droits root (sudo)

## Configuration

Le script configure automatiquement :

1. **Interface Ethernet (eth0)** :
   - IP statique : 192.168.100.10
   - Métrique de routage : 100 (prioritaire)
   - Connexion automatique au démarrage

2. **Interface WiFi (wlan0)** :
   - Configuration automatique de la bande WiFi (2.4GHz/5GHz)
   - Gestion des conflits avec wpa_supplicant
   - Paramètres de sécurité optimisés (WPA/WPA2 mixte)
   - Métrique de routage : 200 (secondaire)
   - Connexion automatique au démarrage

## Fonctionnalités avancées du script

### Détection automatique de bande WiFi
- **PRE_WIFI** → 2.4GHz (bande bg)
- **PRE_WIFI_5GHZ** → 5GHz (bande a)
- **SSIDs avec "5GHZ" ou "5G"** → 5GHz automatiquement
- **SSIDs avec "2GHZ" ou "2G"** → 2.4GHz automatiquement
- **Autres SSIDs** → 2.4GHz par défaut

### Noms de connexion dynamiques
- Le script génère automatiquement des noms de connexion logiques
- Exemple : `PRE_WIFI_2GHZ`, `PRE_WIFI_5GHZ_5GHZ`

### Résolution automatique des conflits
- Désactivation automatique de wpa_supplicant
- Nettoyage des anciennes connexions WiFi
- Configuration optimisée de NetworkManager

## Utilisation

### Syntaxe de base
```bash
sudo ./setup_network.sh --ssid "NOM_WIFI" --psk "MOT_DE_PASSE_WIFI" [OPTIONS]
```

### Options disponibles
- `--ssid SSID` : Nom du réseau WiFi (requis)
- `--psk PASSWORD` : Mot de passe du réseau WiFi (requis)
- `--country CODE` : Code pays pour les régulations WiFi (défaut: FR)
- `--band BAND` : Force une bande WiFi spécifique (défaut: auto)
  - `auto` : Détection automatique selon le SSID
  - `2g` : Force le 2.4GHz
  - `5g` : Force le 5GHz

### Exemples d'utilisation

1. **Configuration automatique (recommandée)**
```bash
# Auto-détection 2.4GHz pour PRE_WIFI
sudo ./Sp3ctra/scripts/raspberry/setup_network.sh --ssid "PRE_WIFI" --psk "FB5FA76AC3"

# Auto-détection 5GHz pour PRE_WIFI_5GHZ
sudo ./Sp3ctra/scripts/raspberry/setup_network.sh --ssid "PRE_WIFI_5GHZ" --psk "FB5FA76AC3"
```

2. **Force une bande spécifique**
```bash
# Force le 2.4GHz même pour un SSID 5GHz
sudo ./setup_network.sh --ssid "PRE_WIFI_5GHZ" --psk "FB5FA76AC3" --band 2g

# Force le 5GHz pour un SSID 2.4GHz
sudo ./setup_network.sh --ssid "PRE_WIFI" --psk "FB5FA76AC3" --band 5g
```

3. **Avec code pays personnalisé**
```bash
sudo ./setup_network.sh --ssid "Mon_WiFi" --psk "password" --country US --band auto
```

## Vérification

Le script affiche automatiquement :
- Le statut des interfaces réseau
- La configuration IP
- La table de routage
- Les tentatives de connexion avec retry logic

### Vérification manuelle
```bash
# Statut des connexions
nmcli device status

# Configuration IP
ip addr show

# Table de routage
ip route

# Lister toutes les connexions
nmcli connection show

# Détail d'une connexion spécifique
nmcli connection show PRE_WIFI_2GHZ
```

## Priorité des Connexions

Le script configure les métriques de routage pour :
1. **Ethernet (métrique 100)** : Prioritaire pour le trafic UDP sur 192.168.100.10
2. **WiFi (métrique 200)** : Connexion secondaire avec fallback automatique

## Résolution des problèmes

### Problèmes courants résolus automatiquement
- ✅ Conflit avec wpa_supplicant (désactivé automatiquement)
- ✅ Problèmes DFS sur canaux 5GHz (utilisation du 2.4GHz par défaut)
- ✅ Paramètres de sécurité WiFi incompatibles (configuration WPA/WPA2 mixte)
- ✅ Permissions incorrectes des fichiers de configuration
- ✅ Anciennes connexions conflictuelles (nettoyage automatique)

### Diagnostic manuel si nécessaire

1. **Vérifier l'état du hardware WiFi**
```bash
# Vérifier les drivers
lsmod | grep brcm

# Vérifier l'interface
ip link show wlan0

# Vérifier les blocages radio
sudo rfkill list all
```

2. **Diagnostiquer NetworkManager**
```bash
# Statut du service
sudo systemctl status NetworkManager

# Logs récents
sudo journalctl -u NetworkManager -n 20 --no-pager

# Scanner les réseaux
sudo nmcli device wifi list
```

3. **Tester la connectivité**
```bash
# Test de connectivité
ping -c 3 8.8.8.8

# Vérifier l'IP obtenue
ip addr show wlan0
```

### Commandes de dépannage avancé

```bash
# Redémarrer NetworkManager si nécessaire
sudo systemctl restart NetworkManager

# Réactiver une connexion
sudo nmcli connection up PRE_WIFI_2GHZ

# Forcer un scan WiFi
sudo nmcli device wifi rescan

# Vérifier les paramètres de sécurité
nmcli connection show PRE_WIFI_2GHZ | grep security
```

## Architecture du script

### Flux d'exécution
1. **Vérification des prérequis** (root, outils)
2. **Résolution des conflits** (wpa_supplicant)
3. **Nettoyage** (anciennes connexions)
4. **Configuration sécurisée** de NetworkManager
5. **Configuration Ethernet** (IP statique)
6. **Configuration WiFi dynamique** (bande auto-détectée)
7. **Activation avec retry logic**
8. **Vérification de connectivité**

### Sécurité
- Configuration NetworkManager sécurisée (pas d'auto-création)
- Permissions correctes (600) pour les fichiers de connexion
- Logging activé pour audit
- Désactivation des fonctions potentiellement problématiques

## Support des cas d'usage

### Cas d'usage supportés
- ✅ Réseaux 2.4GHz et 5GHz
- ✅ WPA/WPA2 Personnel (TKIP+AES)
- ✅ Canaux DFS et non-DFS
- ✅ Connexion automatique au démarrage
- ✅ Fallback Ethernet automatique
- ✅ Persistance après redémarrage

### Limitations connues
- ❌ WPA3 (non testé)
- ❌ Enterprise (802.1x)
- ❌ Réseaux cachés (nécessite modification manuelle)
- ❌ Connexions simultanées multiple SSIDs

## Changelog

### Version améliorée (2025)
- ✨ Détection automatique de bande WiFi
- ✨ Noms de connexion dynamiques
- ✨ Résolution automatique des conflits wpa_supplicant
- ✨ Paramètres de sécurité optimisés
- ✨ Support du paramètre --band
- ✨ Retry logic améliorée
- 🐛 Correction des problèmes de persistance
- 🐛 Résolution des conflits de services réseau
