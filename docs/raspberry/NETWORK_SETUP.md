# Configuration Réseau du Raspberry Pi

Ce document décrit la procédure de configuration réseau du Raspberry Pi pour le projet Sp3ctra, utilisant l'architecture modulaire des scripts réseau.

## Architecture Modulaire

Le système de configuration réseau est désormais organisé en **trois scripts modulaires** :

1. **`setup_network.sh`** - **Orchestrateur principal** (maintient la compatibilité)
   - Configure Ethernet ET WiFi simultanément
   - Gère les priorités de routage
   - Interface unifiée pour la configuration complète

2. **`setup_ethernet.sh`** - **Script spécialisé Ethernet**
   - Configuration Ethernet uniquement
   - IP statique configurable
   - Gestion indépendante de l'interface filaire

3. **`setup_wifi.sh`** - **Script spécialisé WiFi**
   - Configuration WiFi uniquement
   - Gestion des bandes 2.4GHz/5GHz
   - Paramètres de sécurité optimisés

### Avantages de l'architecture modulaire

- ✅ **Modularité** : Configuration séparée Ethernet/WiFi selon les besoins
- ✅ **Flexibilité** : Paramètres spécifiques à chaque type de réseau
- ✅ **Maintenabilité** : Code plus simple à déboguer et maintenir
- ✅ **Réutilisabilité** : Scripts utilisables indépendamment
- ✅ **Compatibilité** : L'interface originale reste inchangée

## Prérequis

- Raspberry Pi sous Debian Bookworm
- NetworkManager installé (`sudo apt install network-manager`)
- Utilitaire iw installé (`sudo apt install iw`)
- Droits root (sudo)

## Utilisation des Scripts

### 1. Script Principal (setup_network.sh) - Recommandé

**Configuration complète Ethernet + WiFi** :
```bash
# Configuration automatique (comportement par défaut)
sudo ./setup_network.sh --ssid "PRE_WIFI" --psk "FB5FA76AC3"

# Configuration avec options personnalisées
sudo ./setup_network.sh --ssid "Office_WiFi" --psk "secret" \
     --ethernet-ip 10.0.1.50 --country US --band auto
```

**Modes spécialisés du script principal** :
```bash
# Configuration Ethernet uniquement
sudo ./setup_network.sh --ethernet-only --ethernet-ip 192.168.1.100

# Configuration WiFi uniquement
sudo ./setup_network.sh --wifi-only --ssid "MyWiFi_5G" --psk "password" --band 5g
```

### 2. Script Ethernet (setup_ethernet.sh) - Configuration Filaire

```bash
# Configuration basique avec IP par défaut (192.168.100.10)
sudo ./setup_ethernet.sh

# Configuration avec IP personnalisée
sudo ./setup_ethernet.sh --ip 192.168.1.50

# Configuration complète personnalisée
sudo ./setup_ethernet.sh --ip 10.0.1.100 --interface eth0 --metric 50
```

### 3. Script WiFi (setup_wifi.sh) - Configuration Sans-Fil

```bash
# Configuration WiFi basique
sudo ./setup_wifi.sh --ssid "MonWiFi" --psk "motdepasse"

# Configuration avec bande spécifique
sudo ./setup_wifi.sh --ssid "WiFi_5G" --psk "secret" --band 5g --country US

# Configuration avec interface personnalisée
sudo ./setup_wifi.sh --ssid "Guest" --psk "pass" --interface wlan1 --metric 300
```

## Options Disponibles

### Options du Script Principal (setup_network.sh)

| Option | Description | Défaut |
|--------|-------------|--------|
| `--ssid SSID` | Nom du réseau WiFi (requis pour WiFi) | - |
| `--psk PASSWORD` | Mot de passe WiFi (requis pour WiFi) | - |
| `--country CODE` | Code pays pour régulations WiFi | FR |
| `--band BAND` | Bande WiFi (auto\|2g\|5g) | auto |
| `--ethernet-ip IP` | IP statique Ethernet | 192.168.100.10 |
| `--ethernet-interface IFACE` | Interface Ethernet | eth0 |
| `--wifi-interface IFACE` | Interface WiFi | wlan0 |
| `--ethernet-metric N` | Métrique routage Ethernet | 100 |
| `--wifi-metric N` | Métrique routage WiFi | 200 |
| `--ethernet-only` | Configuration Ethernet uniquement | - |
| `--wifi-only` | Configuration WiFi uniquement | - |

### Options du Script Ethernet (setup_ethernet.sh)

| Option | Description | Défaut |
|--------|-------------|--------|
| `--ip IP` | Adresse IP statique | 192.168.100.10 |
| `--interface IFACE` | Nom de l'interface Ethernet | eth0 |
| `--metric METRIC` | Métrique de routage | 100 |
| `--connection-name NAME` | Nom de la connexion | eth0-static |

### Options du Script WiFi (setup_wifi.sh)

| Option | Description | Défaut |
|--------|-------------|--------|
| `--ssid SSID` | Nom du réseau WiFi (requis) | - |
| `--psk PASSWORD` | Mot de passe WiFi (requis) | - |
| `--country CODE` | Code pays pour régulations | FR |
| `--band BAND` | Bande WiFi (auto\|2g\|5g) | auto |
| `--interface IFACE` | Interface WiFi | wlan0 |
| `--metric METRIC` | Métrique de routage | 200 |

## Configuration Automatique

### Détection automatique de bande WiFi (mode `--band auto`)
- **PRE_WIFI** → 2.4GHz (bande bg)
- **PRE_WIFI_5GHZ** → 5GHz (bande a)
- **SSIDs avec "5GHZ" ou "5G"** → 5GHz automatiquement
- **SSIDs avec "2GHZ" ou "2G"** → 2.4GHz automatiquement
- **Autres SSIDs** → 2.4GHz par défaut

### Noms de connexion dynamiques
- Le script génère automatiquement des noms de connexion logiques
- Exemple : `PRE_WIFI_2GHZ`, `PRE_WIFI_5GHZ_5GHZ`

## Priorité des Connexions

Le système configure les métriques de routage pour :
1. **Ethernet (métrique 100)** : Prioritaire pour le trafic UDP sur 192.168.100.10
2. **WiFi (métrique 200)** : Connexion secondaire avec fallback automatique

Les métriques sont personnalisables via les options `--ethernet-metric` et `--wifi-metric`.

## Cas d'Usage Courants

### Cas d'usage 1 : Configuration initiale complète
```bash
# Setup complet du Raspberry Pi avec Ethernet + WiFi
sudo ./setup_network.sh --ssid "PRE_WIFI_5GHZ" --psk "FB5FA76AC3" --country FR
```

### Cas d'usage 2 : Réseaux de développement
```bash
# Configuration Ethernet pour développement local
sudo ./setup_ethernet.sh --ip 192.168.1.100

# Ajout WiFi ultérieurement
sudo ./setup_wifi.sh --ssid "Dev_WiFi" --psk "devpassword"
```

### Cas d'usage 3 : WiFi uniquement (sans Ethernet)
```bash
# Configuration pour Raspberry Pi WiFi-only
sudo ./setup_wifi.sh --ssid "HomeWiFi" --psk "homepassword" --band auto
```

### Cas d'usage 4 : Reconfiguration réseau
```bash
# Reconfiguration complète avec nouveaux paramètres
sudo ./setup_network.sh --ssid "NewWiFi" --psk "newpass" \
     --ethernet-ip 10.0.0.100 --band 5g --country US
```

## Fonctionnalités Avancées

### Résolution automatique des conflits
- ✅ Désactivation automatique de wpa_supplicant
- ✅ Nettoyage des anciennes connexions WiFi
- ✅ Configuration optimisée de NetworkManager
- ✅ Gestion des permissions des fichiers de configuration

### Retry Logic et Robustesse
- ✅ Tentatives multiples de connexion WiFi (3 essais)
- ✅ Vérification de connectivité Internet
- ✅ Gestion des erreurs avec messages explicites
- ✅ Logs détaillés pour diagnostic

### Paramètres de Sécurité
- ✅ Support WPA/WPA2 mixte (TKIP+AES)
- ✅ Configuration sécurisée des fichiers NetworkManager
- ✅ Désactivation de l'auto-création de connexions
- ✅ Logging activé pour audit sécurisé

## Vérification et Diagnostic

### Affichage automatique du statut
Tous les scripts affichent automatiquement :
- Statut des interfaces réseau
- Configuration IP active
- Table de routage
- Test de connectivité Internet

### Vérification manuelle
```bash
# Statut global des connexions
nmcli device status

# Détail d'une connexion spécifique
nmcli connection show eth0-static
nmcli connection show PRE_WIFI_5GHZ_5GHZ

# Configuration IP complète
ip addr show

# Table de routage avec métriques
ip route show
```

### Commandes de diagnostic
```bash
# Lister toutes les connexions configurées
nmcli connection show

# Vérifier les connexions actives
nmcli connection show --active

# Scanner les réseaux WiFi disponibles
nmcli device wifi list

# Logs NetworkManager récents
sudo journalctl -u NetworkManager -n 20 --no-pager
```

## Résolution des Problèmes

### Problèmes courants automatiquement résolus
- ✅ Conflit avec wpa_supplicant
- ✅ Problèmes DFS sur canaux 5GHz
- ✅ Paramètres de sécurité WiFi incompatibles
- ✅ Permissions incorrectes des fichiers
- ✅ Anciennes connexions conflictuelles

### Diagnostic approfondi

**1. Problèmes Ethernet** :
```bash
# Vérifier le lien physique
ethtool eth0

# Tester la connexion Ethernet
sudo ./setup_ethernet.sh --ip 192.168.100.10
```

**2. Problèmes WiFi** :
```bash
# Vérifier le hardware WiFi
lsmod | grep brcm
ip link show wlan0

# Scanner et reconnecter
sudo ./setup_wifi.sh --ssid "VotreSSID" --psk "VotrePassword"
```

**3. Problèmes de routage** :
```bash
# Afficher la table de routage détaillée
ip route show table all

# Tester chaque interface
ping -I eth0 -c 3 8.8.8.8
ping -I wlan0 -c 3 8.8.8.8
```

### Réparation rapide
```bash
# Redémarrage complet de NetworkManager
sudo systemctl restart NetworkManager

# Reconfiguration forcée
sudo ./setup_network.sh --ssid "VotreSSID" --psk "VotrePassword" --country FR
```

## Architecture Technique

### Flux d'exécution du script principal
1. **Analyse des arguments** (permet affichage aide sans root)
2. **Vérification des privilèges root**
3. **Vérification des scripts spécialisés**
4. **Exécution du script Ethernet** (si activé)
5. **Exécution du script WiFi** (si activé)
6. **Attente de stabilisation** (3 secondes)
7. **Affichage du statut final**

### Flux des scripts spécialisés
Ethernet (`setup_ethernet.sh`) :
- Vérification outils → Configuration NetworkManager → Configuration interface → Activation → Test

WiFi (`setup_wifi.sh`) :
- Vérification outils → Gestion pays WiFi → Résolution conflits → Nettoyage → Configuration → Activation avec retry → Test

### Sécurité et Permissions
- Vérification root obligatoire pour les opérations système
- Affichage aide possible sans privilèges élevés  
- Fichiers NetworkManager avec permissions 600
- Configuration sécurisée anti auto-création
- Logging détaillé pour audit

## Support et Compatibilité

### Cas d'usage supportés
- ✅ Réseaux 2.4GHz et 5GHz
- ✅ WPA/WPA2 Personnel (TKIP+AES)
- ✅ IP statique et DHCP
- ✅ Interfaces multiples
- ✅ Métriques de routage personnalisées
- ✅ Connexion automatique au démarrage
- ✅ Persistance après redémarrage

### Limitations connues
- ❌ WPA3 (non testé)
- ❌ Enterprise (802.1x)
- ❌ Réseaux cachés (modification manuelle nécessaire)
- ❌ Connexions simultanées multiples sur même interface

## Changelog

### Version Modulaire (2025)
- ✨ **Architecture modulaire** : 3 scripts spécialisés
- ✨ **setup_ethernet.sh** : Configuration Ethernet dédiée
- ✨ **setup_wifi.sh** : Configuration WiFi dédiée
- ✨ **Modes spécialisés** : --ethernet-only, --wifi-only
- ✨ **Paramètres étendus** : Métriques, interfaces personnalisables
- ✨ **Compatibilité maintenue** : Interface originale préservée
- ✨ **Aide sans root** : Affichage documentation sans privilèges
- 🔧 **Code refactorisé** : Maintenance facilitée
- 📚 **Documentation étendue** : Guide complet d'utilisation

### Version précédente (2025)
- ✨ Détection automatique de bande WiFi
- ✨ Noms de connexion dynamiques
- ✨ Résolution automatique des conflits wpa_supplicant
- ✨ Paramètres de sécurité optimisés
- ✨ Support du paramètre --band
- ✨ Retry logic améliorée
- 🐛 Correction des problèmes de persistance
- 🐛 Résolution des conflits de services réseau
