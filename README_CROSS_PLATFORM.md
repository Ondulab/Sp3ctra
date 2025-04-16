# Compilation cross-platform de CISYNTH

Ce document explique les modifications apportées pour rendre CISYNTH compatible à la fois sur macOS et sur Jetson Nano (Linux ARM).

## Modifications apportées

### 1. Compilation conditionnelle pour macOS et Linux

- Les inclusions spécifiques à macOS (`IOKit/serial/ioss.h`) sont maintenant conditionnelles
- Ajout de la définition `__LINUX__` pour la compilation sur Linux
- Correction de la définition de `CLI_MODE` pour éviter les redéfinitions

### 2. Gestion de l'absence de SFML sur Jetson

- Détection automatique de SFML à la compilation via pkg-config
- Implémentation de substituts pour les types SFML lorsque la bibliothèque n'est pas disponible
- Mode CLI amélioré qui fonctionne même sans SFML

### 3. Configuration du port série DMX

- Adaptation des fonctions de configuration du port série pour fonctionner sur Linux et macOS
- Support des baudrates standards sur Linux via cfsetispeed/cfsetospeed
- Support des baudrates personnalisés sur macOS via IOSSIOSPEED

## Installation des dépendances sur Jetson

Un script d'installation des dépendances a été créé pour simplifier la configuration sur Jetson Nano:

```bash
chmod +x install_dependencies_jetson.sh
./install_dependencies_jetson.sh
```

Ce script installe:
- Qt5 et dépendances de développement
- FFTW3 et libsndfile
- RtAudio et ALSA pour la gestion audio
- SFML si disponible (optionnel)

## Compilation

La compilation reste identique sur les deux plateformes:

```bash
./rebuild_cli.sh
```

Le script détecte automatiquement l'environnement et ajuste les paramètres en conséquence.

## Remarques

- Sur macOS, toutes les fonctionnalités sont disponibles
- Sur Jetson sans SFML, l'application fonctionne en mode CLI uniquement
- L'audio et le DMX fonctionnent sur les deux plateformes

## Options de lancement

```bash
# Lancer en mode CLI (sans interface graphique)
./build_nogui/CISYNTH_noGUI --cli

# Désactiver le DMX
./build_nogui/CISYNTH_noGUI --cli --no-dmx

# Spécifier un port DMX personnalisé
./build_nogui/CISYNTH_noGUI --cli --dmx-port=/dev/ttyUSB0

# Supprimer les messages d'erreur DMX
./build_nogui/CISYNTH_noGUI --cli --silent-dmx
