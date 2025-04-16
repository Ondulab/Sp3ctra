
# Guide de dépannage pour la connexion à la Jetson Nano

Ce document explique comment résoudre les problèmes de connectivité avec la Jetson Nano et utiliser le script de déploiement amélioré.

## Problème

Le message d'erreur suivant indique que la Jetson Nano n'est pas accessible à l'adresse IP configurée:
```
ssh: connect to host 192.168.2.10 port 22: No route to host
```

## Causes possibles

1. **Adresse IP incorrecte**: L'adresse IP de la Jetson Nano a changé (souvent le cas avec DHCP)
2. **Jetson éteinte**: La Jetson n'est pas allumée ou n'a pas terminé son démarrage
3. **Problème réseau**: La Jetson et votre ordinateur ne sont pas sur le même réseau
4. **Configuration SSH**: Le service SSH n'est pas activé sur la Jetson
5. **Pare-feu**: Un pare-feu bloque les connexions vers la Jetson

## Solution: Script de déploiement avec dépannage automatique

Le script `deploy_with_troubleshooting.sh` intègre plusieurs fonctionnalités de dépannage pour résoudre ces problèmes.

### Utilisation de base

```bash
./deploy_with_troubleshooting.sh
```

### Options disponibles

```bash
./deploy_with_troubleshooting.sh [options]
```

Options:
- `--ip <adresse_ip>`: Spécifie une adresse IP alternative (par défaut: 192.168.2.10)
- `--skip-check`: Ignore la vérification de connectivité (utile pour forcer le déploiement)
- `--scan-network`: Active le scan réseau automatique pour trouver la Jetson
- `--serial-port <port>`: Spécifie le port série pour la connexion USB (détection auto si omis)

### Méthodes de dépannage intégrées

Le script propose automatiquement plusieurs méthodes pour résoudre les problèmes de connectivité:

#### 1. Scan réseau automatique

Recherche les appareils avec SSH actif sur le réseau local:
```bash
./deploy_with_troubleshooting.sh --scan-network
```

Nécessite `nmap` installé:
- Sur macOS: `brew install nmap`
- Sur Linux: `sudo apt-get install nmap`

#### 2. Connexion série

Permet d'obtenir l'adresse IP actuelle de la Jetson en se connectant via le port série USB:

1. Connectez la Jetson à votre ordinateur avec un câble USB
2. Exécutez le script qui détectera automatiquement le port série
3. Connectez-vous avec les identifiants (ondulab/sp3ctra)
4. Exécutez `ip a` sur la Jetson pour trouver son adresse IP
5. Le script vous demandera de saisir cette adresse IP

#### 3. Configuration manuelle de l'adresse IP

Permet de spécifier manuellement une adresse IP si vous la connaissez:
```bash
./deploy_with_troubleshooting.sh --ip 192.168.1.123
```

### Exemples d'utilisation

#### Déploiement standard avec détection automatique des problèmes

```bash
./deploy_with_troubleshooting.sh
```

Le script vérifiera la connectivité et proposera des solutions en cas de problème.

#### Scan du réseau pour trouver la Jetson

```bash
./deploy_with_troubleshooting.sh --scan-network
```

#### Déploiement vers une adresse IP spécifique

```bash
./deploy_with_troubleshooting.sh --ip 192.168.1.123
```

#### Utilisation d'un port série spécifique

```bash
./deploy_with_troubleshooting.sh --serial-port /dev/tty.usbserial-1234
```

## Procédure recommandée

1. Essayez d'abord le déploiement standard: `./deploy_with_troubleshooting.sh`
2. Si cela échoue, assurez-vous que la Jetson est allumée et connectée au réseau
3. Utilisez le script avec l'option `--scan-network` pour trouver la Jetson
4. Si le scan échoue, connectez la Jetson via USB et utilisez la connexion série
5. Une fois la bonne adresse IP trouvée, configurez-la avec `--ip <adresse_ip>`

## Vérification du réseau de la Jetson

Si vous avez accès à la Jetson via la connexion série, vous pouvez vérifier sa configuration réseau:

1. Exécutez `ip a` ou `ifconfig` pour voir toutes les interfaces et leurs adresses IP
2. Vérifiez que l'interface réseau (eth0 ou wlan0) a une adresse IP valide
3. Testez la connectivité avec `ping 8.8.8.8` pour vérifier l'accès Internet
4. Vérifiez que SSH est activé avec `systemctl status ssh`

## Configurer la Jetson avec une IP statique

Si vous souhaitez éviter les problèmes de changement d'adresse IP, configurez une IP statique:

1. Connectez-vous à la Jetson (via SSH ou série)
2. Éditez le fichier de configuration réseau:
   ```bash
   sudo nano /etc/netplan/01-netcfg.yaml
   ```
3. Configurez une adresse IP statique:
   ```yaml
   network:
     version: 2
     ethernets:
       eth0:
         dhcp4: no
         addresses: [192.168.1.200/24]
         gateway4: 192.168.1.1
         nameservers:
           addresses: [8.8.8.8, 8.8.4.4]
   ```
4. Appliquez la configuration:
   ```bash
   sudo netplan apply
   ```
