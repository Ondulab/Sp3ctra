# Guide de déploiement de CISYNTH CLI sur Jetson Nano

Ce document décrit le processus de déploiement de l'application CISYNTH en mode CLI (sans interface graphique) sur une Jetson Nano. Il inclut des instructions détaillées pour le transfert des fichiers, la compilation, la configuration et l'exécution de l'application.

## Prérequis

- Une Jetson Nano accessible via SSH (adresse IP : 192.168.2.10)
- Connexion réseau entre votre machine locale et la Jetson Nano
- Compte utilisateur sur la Jetson Nano avec les permissions sudo (utilisateur : ondulab)
- Optionnel : Connexion série USB pour le débogage

## Scripts fournis

Ce package contient trois scripts principaux pour faciliter le déploiement et le débogage :

1. **deploy_to_jetson.sh** - Script principal pour déployer CISYNTH sur la Jetson Nano
2. **connect_to_jetson_serial.sh** - Script pour se connecter à la console série de la Jetson Nano
3. **run_on_jetson.sh** - Script exécuté sur la Jetson pour compiler et lancer l'application

## Processus de déploiement

### 1. Déploiement automatisé

Le script `deploy_to_jetson.sh` automatise l'ensemble du processus de déploiement. Vous pouvez l'utiliser avec différentes options :

```bash
# Utilisation standard
./deploy_to_jetson.sh

# Spécifier une adresse IP différente
./deploy_to_jetson.sh --ip 192.168.1.100

# Ignorer la vérification de connectivité
./deploy_to_jetson.sh --skip-check
```

Options disponibles :
- `--ip <adresse_ip>` : Spécifie l'adresse IP de la Jetson Nano
- `--skip-check` : Ignore la vérification de connectivité (utile si le ping est bloqué)
- `--help` : Affiche l'aide

Ce script effectue les actions suivantes :
- Vérifie la connexion à la Jetson Nano
- Prépare les fichiers source nécessaires
- Adapte la configuration pour Linux (notamment le port DMX)
- Transfère les fichiers sur la Jetson
- Configure les permissions
- Installe les dépendances nécessaires
- Configure les droits pour l'accès au port DMX
- Optionnellement, exécute l'application

### 2. Connexion série pour le débogage et la configuration

La connexion série est particulièrement utile pour :
- Trouver l'adresse IP actuelle de la Jetson Nano
- Accéder à la Jetson quand elle n'est pas accessible par le réseau
- Déboguer des problèmes de démarrage

Pour vous connecter via le port série USB :

```bash
# Utilisation standard
./connect_to_jetson_serial.sh

# Spécifier un port série différent
./connect_to_jetson_serial.sh --port /dev/tty.usbserial-ABCDEF
```

Le script détectera automatiquement les ports série disponibles si celui spécifié n'existe pas.

Ce script établit une connexion série avec la Jetson Nano, ce qui peut être utile pour :
- Déboguer des problèmes de démarrage
- Surveiller les logs système
- Accéder à la Jetson si la connexion réseau ne fonctionne pas

Identifiants de connexion :
- Utilisateur : `ondulab`
- Mot de passe : `sp3ctra`

## Configuration DMX

L'application CISYNTH peut utiliser un contrôleur DMX pour les effets lumineux. Sur Linux/Jetson, le port DMX est généralement `/dev/ttyUSB0`. Le script de déploiement configure automatiquement ce paramètre dans `config.h`.

Si votre contrôleur DMX utilise un port différent, modifiez la variable `DMX_PORT` dans le script `deploy_to_jetson.sh` avant de l'exécuter.

## Exécution manuelle sur la Jetson

Si vous préférez exécuter manuellement l'application après le déploiement :

1. Connectez-vous à la Jetson via SSH :
   ```bash
   ssh ondulab@192.168.2.10
   ```

2. Naviguez vers le répertoire de l'application :
   ```bash
   cd ~/cisynth_cli
   ```

3. Exécutez le script de compilation et d'exécution :
   ```bash
   ./run_on_jetson.sh
   ```

## Dépannage

### Trouver l'adresse IP de la Jetson Nano

Si vous ne connaissez pas l'adresse IP actuelle de votre Jetson Nano :

1. Connectez-vous via le port série USB :
   ```bash
   ./connect_to_jetson_serial.sh
   ```

2. Une fois connecté, identifiez-vous avec les identifiants :
   - Utilisateur: `ondulab`
   - Mot de passe: `sp3ctra`

3. Exécutez la commande suivante pour afficher les interfaces réseau :
   ```bash
   ip a
   ```

4. Recherchez l'interface eth0 et notez son adresse IP (généralement 192.168.x.x)

5. Utilisez cette adresse IP avec le script de déploiement :
   ```bash
   ./deploy_to_jetson.sh --ip <adresse_ip>
   ```

### Problème de connexion SSH

Si vous ne pouvez pas vous connecter via SSH, vérifiez :
- Que la Jetson est allumée et connectée au réseau
- Que l'adresse IP est correcte (vérifiez avec la connexion série comme expliqué ci-dessus)
- Qu'aucun pare-feu ne bloque la connexion
- Si le ping échoue mais que vous êtes sûr de l'adresse IP, utilisez l'option `--skip-check` :
  ```bash
  ./deploy_to_jetson.sh --ip <adresse_ip> --skip-check
  ```

### Problème d'accès au port DMX

Si l'application ne peut pas accéder au port DMX :
- Vérifiez que le contrôleur est bien branché
- Vérifiez le nom du port avec `ls -l /dev/ttyUSB*`
- Assurez-vous que l'utilisateur appartient au groupe `dialout`
  ```bash
  sudo usermod -a -G dialout ondulab
  # Déconnectez-vous et reconnectez-vous pour appliquer les changements
  ```

### Problèmes de compilation

Si la compilation échoue :
- Vérifiez que toutes les dépendances sont installées
  ```bash
  sudo apt-get update
  sudo apt-get install build-essential qmake libfftw3-dev libsndfile-dev libsfml-dev libcsfml-dev
  ```
- Consultez les messages d'erreur dans la sortie de compilation

## Personnalisation

### Modification de la configuration

Pour modifier les paramètres de CISYNTH, éditez le fichier `src/core/config.h` avant le déploiement ou directement sur la Jetson après le déploiement.

Les paramètres importants incluent :
- `DMX_PORT` : Port du contrôleur DMX
- `UDP_MAX_NB_PACKET_PER_LINE` : Configuration réseau
- `SAMPLING_FREQUENCY` : Fréquence d'échantillonnage audio

## Opérations avancées

### Mise à jour de l'application

Pour mettre à jour l'application après des modifications locales, exécutez simplement à nouveau le script de déploiement :

```bash
./deploy_to_jetson.sh
```

### Débogage

Pour un débogage plus avancé, vous pouvez :
1. Activer des macros de débogage dans `config.h` (décommentez les lignes `#define PRINT_*`)
2. Recompiler et exécuter l'application
3. Observer la sortie pour les informations de débogage
