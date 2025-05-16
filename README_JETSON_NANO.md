# Guide de portage de CISYNTH sur Jetson Nano

Ce document explique comment porter et exécuter CISYNTH sur une Jetson Nano en mode ligne de commande.

## Modifications apportées

Les modifications suivantes ont été effectuées pour permettre l'exécution de l'application sans interface graphique :

1. **Mode CLI** : Ajout d'un mode fonctionnant sans fenêtre graphique SFML
   - Option `CLI_MODE` dans config.h
   - Adaptation du code dans main.c pour fonctionner sans fenêtre graphique
   - Adaptation des fonctions d'affichage dans display.c

2. **Scripts de build et d'exécution** :
   - Modification de `build_nogui.sh` pour supporter les options CLI
   - Création du script `run_on_jetson.sh` pour faciliter le déploiement sur Jetson Nano

3. **Configuration du build** :
   - Ajout de l'option `cli_mode` dans le fichier CISYNTH_noGUI.pro

## Prérequis

Assurez-vous que la Jetson Nano dispose des dépendances suivantes :

```bash
sudo apt-get update
sudo apt-get install build-essential qmake libfftw3-dev libsndfile-dev libsfml-dev libcsfml-dev
```

## Déploiement et exécution

### Méthode 1 : Utilisation du script automatisé

1. Transférez le code source complet vers la Jetson Nano
2. Rendez le script exécutable si nécessaire :
   ```bash
   chmod +x run_on_jetson.sh
   ```
3. Exécutez le script :
   ```bash
   ./run_on_jetson.sh
   ```

Le script vérifiera l'environnement, ajustera la configuration si nécessaire, et lancera l'application en mode CLI.

### Méthode 2 : Compilation et exécution manuelles

1. Compilez en mode CLI :
   ```bash
   ./build_nogui.sh --cli
   ```

2. Exécutez l'application :
   ```bash
   ./build_nogui/CISYNTH_noGUI --cli
   ```

## Configuration DMX

Si vous utilisez un contrôleur DMX, vous devrez peut-être ajuster le port dans `src/core/config.h`. Sur la Jetson Nano, le port est généralement `/dev/ttyUSB0` au lieu de `/dev/tty.usbserial-XXXXXXXX` utilisé sur macOS.

## Options de ligne de commande

L'application supporte les options suivantes :

- `--cli` : Active le mode ligne de commande (sans interface graphique)

## Dépannage

### Problèmes de port DMX

Si l'accès au port DMX échoue, vérifiez que :
1. Le port est correctement configuré dans config.h
2. L'utilisateur a les droits d'accès au port :
   ```bash
   sudo usermod -a -G dialout $USER
   # Déconnectez-vous et reconnectez-vous pour appliquer les changements
   ```

### Problèmes de dépendances

Si vous rencontrez des erreurs liées aux bibliothèques, assurez-vous que toutes les dépendances sont installées :
```bash
sudo apt-get update
sudo apt-get install build-essential qmake libfftw3-dev libsndfile-dev libsfml-dev libcsfml-dev
```

## Notes pour le développement futur

Pour étendre davantage le support du mode CLI :

1. Ajouter un mécanisme de gestion des signaux (SIGINT, SIGTERM) pour arrêter proprement l'application
2. Implémenter un enregistrement de log pour afficher les informations importantes en mode CLI
3. Considérer l'ajout d'options de ligne de commande supplémentaires pour configurer les paramètres d'exécution
