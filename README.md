# Sp3ctra - Synthétiseur Audio en Temps Réel

Sp3ctra est un synthétiseur audio avancé développé en C/C++ avec une architecture modulaire. Il supporte la synthèse additive et polyphonique, utilise RtAudio pour le traitement audio en temps réel, et intègre la connectivité MIDI et DMX pour le contrôle d'éclairage.

## Caractéristiques principales

- **Synthèse audio en temps réel** : Moteurs de synthèse additive et polyphonique
- **Architecture modulaire** : Code organisé par domaines fonctionnels
- **Multi-plateforme** : Support macOS, Linux et Raspberry Pi
- **Format VST/AU** : Plugin audio pour DAW (Ableton, Reaper, Logic Pro, etc.)
- **Contrôle MIDI** : Interface complète pour contrôleurs MIDI
- **Intégration DMX** : Contrôle d'éclairage synchronisé
- **Interface flexible** : Mode graphique (SFML), ligne de commande ou plugin VST
- **Optimisations ARM** : Support spécialisé pour Raspberry Pi 4/5

## Architecture du projet

```
src/
├── core/                    # Cœur de l'application
│   ├── main.c              # Point d'entrée principal
│   ├── config.h            # Configuration globale
│   └── context.h           # Contexte d'exécution
├── audio/                   # Système audio
│   ├── rtaudio/            # Interface RtAudio
│   ├── buffers/            # Gestion des buffers audio
│   ├── effects/            # Effets audio (reverb, EQ, auto-volume)
│   └── pan/                # Panoramique lock-free
├── synthesis/               # Moteurs de synthèse
│   ├── additive/           # Synthèse additive
│   └── polyphonic/         # Synthèse polyphonique + FFT
├── communication/           # Communications externes
│   ├── midi/               # Contrôleur MIDI
│   ├── network/            # Communication UDP
│   └── dmx/                # Interface DMX
├── display/                 # Affichage et visualisation
├── threading/               # Gestion des threads
├── utils/                   # Utilitaires et helpers
└── config/                  # Fichiers de configuration
```

## Prérequis

### macOS
```bash
# Installation des dépendances via Homebrew
brew install fftw libsndfile rtaudio rtmidi
brew install sfml@2 csfml  # Pour l'interface graphique (optionnel)
```

### Linux (Raspberry Pi / Debian/Ubuntu)
```bash
# Installation des dépendances
sudo apt update
sudo apt install build-essential
sudo apt install libfftw3-dev libsndfile1-dev libasound2-dev
sudo apt install librtaudio-dev librtmidi-dev
sudo apt install libsfml-dev libcsfml-dev  # Pour l'interface graphique (optionnel)
```

## Compilation

### Compilation avec Makefile (recommandée)

Le projet utilise un Makefile moderne avec architecture modulaire :

```bash
# Compilation standard
make

# Compilation sans interface graphique
make CFLAGS="-O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -DNO_SFML"

# Nettoyage
make clean

# Nettoyage complet
make distclean

# Aide
make help
```

### Scripts de compilation

Le projet inclut des scripts de compilation spécialisés :

```bash
# Compilation pour macOS (debug)
./scripts/build/build_mac_debug.sh

# Compilation pour macOS (release)
./scripts/build/build_mac_release.sh

# Compilation pour Raspberry Pi
./scripts/build/build_raspberry_release.sh

# Script de build général
./scripts/build/build.sh
```

## Utilisation

### Format Plugin VST/AU

Sp3ctra est également disponible en format plugin audio (VST3, AU, Standalone) pour une intégration complète dans votre DAW préféré.

```bash
# Compilation du plugin VST
bash scripts/build_vst.sh

# Lancement du standalone
bash scripts/run_standalone.sh
```

📖 **Documentation VST** : Consultez `vst/QUICKSTART.md` pour le guide complet d'utilisation du plugin.

⚠️ **Limitation connue** : En raison d'une configuration globale partagée, **une seule instance du plugin Sp3ctra VST peut être chargée à la fois** dans un projet DAW. Cette limitation sera résolue dans une future version.

### Lancement de base (standalone)

Après compilation, l'exécutable se trouve dans `build/Sp3ctra` :

```bash
Usage: ./build/Sp3ctra [OPTIONS]
```

### Options disponibles

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Affiche le message d'aide |
| `--display` | Active l'affichage visuel du scanner |
| `--list-audio-devices` | Liste les périphériques audio disponibles et quitte |
| `--audio-device=<ID>` | Utilise un périphérique audio spécifique |
| `--no-dmx` | Désactive la sortie d'éclairage DMX |
| `--dmx-port=<PORT>` | Spécifie le port série DMX (défaut: `/dev/tty.usbserial-AD0JUL0N`) |
| `--silent-dmx` | Supprime les messages d'erreur DMX |
| `--test-tone` | Active le mode tonalité de test (440Hz) |
| `--debug-image` | Active la visualisation de débogage des transformations d'image |

### Exemples d'utilisation

```bash
# Utiliser le périphérique audio 3
./build/Sp3ctra --audio-device=3

# Lister tous les périphériques audio
./build/Sp3ctra --list-audio-devices

# Fonctionner sans DMX
./build/Sp3ctra --no-dmx

# Fonctionner avec affichage visuel
./build/Sp3ctra --display --audio-device=1

# Mode test avec tonalité 440Hz
./build/Sp3ctra --test-tone --no-dmx

# Mode débogage avec visualisation d'images
./build/Sp3ctra --debug-image --display
```

## Moteurs de synthèse

### Synthèse additive
- Génération de formes d'onde complexes par addition d'harmoniques
- Contrôle précis des amplitudes et phases
- Optimisée pour le temps réel

### Synthèse polyphonique
- Support multi-notes simultanées
- Utilisation de FFT pour l'analyse spectrale
- Gestion avancée des enveloppes

## Configuration

### Fichiers de configuration

Le projet utilise des fichiers de configuration modulaires dans `src/config/` :

- `config_audio.h` : Paramètres audio (sample rate, buffer size)
- `config_synth_luxstral.h` : Configuration synthèse additive
- `config_synth_luxsynth.h` : Configuration synthèse polyphonique
- `config_display.h` : Paramètres d'affichage
- `config_dmx.h` : Configuration DMX
- `config_debug.h` : Options de débogage

### Personnalisation

```c
// Exemple de configuration audio dans config_audio.h
#define SAMPLE_RATE 44100
#define BUFFER_SIZE 512
#define NUM_CHANNELS 2
```

## Optimisations

### Raspberry Pi 5 (ARM64)
- Optimisations ARM Cortex-A76
- Support des instructions SIMD/NEON
- Compilation avec `-march=armv8-a+crc+simd`

### Raspberry Pi 4 (ARM32)
- Optimisations ARM Cortex-A72
- Support NEON FP
- Configuration spécifique ARM32

### Scripts d'optimisation
```bash
# Déploiement optimisé sur Raspberry Pi
./scripts/deploy_raspberry/deploy_to_pi.sh

# Installation des dépendances Raspberry Pi
./scripts/deploy_raspberry/install_dependencies_raspberry.sh
```

## Contrôles et interfaces

### Interface MIDI
- Support complet des contrôleurs MIDI
- Mapping configurable des paramètres
- Gestion polyphonique des notes

### Interface DMX
- Contrôle d'éclairage synchronisé
- Support des protocoles DMX standard
- Configuration flexible des canaux

### Interface réseau
- Communication UDP pour contrôle distant
- API de contrôle en temps réel
- Synchronisation multi-instances

## Débogage et diagnostic

### Outils de débogage
```bash
# Test des oscillateurs avec génération d'images
./test_debug_oscillators.sh

# Mode débogage avec visualisation d'images
./build/Sp3ctra --debug-image --display

# Mode test avec tonalité de référence
./build/Sp3ctra --test-tone --no-dmx
```

### Images de débogage
Le système génère automatiquement des images de débogage dans `debug_images/` pour visualiser :
- Volumes des oscillateurs
- Spectres de fréquence
- Formes d'onde générées
- Transformations d'images en temps réel

## Dépannage

### Problèmes audio
```bash
# Vérifier les périphériques disponibles
./build/Sp3ctra --list-audio-devices

# Test avec périphérique spécifique
./build/Sp3ctra --audio-device=0
```

### Problèmes DMX
```bash
# Lancer sans DMX pour isoler le problème
./build/Sp3ctra --no-dmx

# Vérifier les ports série
ls /dev/tty* | grep usb
```

### Problèmes de compilation
```bash
# Nettoyage complet
make distclean

# Recompilation avec informations de débogage
make CFLAGS="-g -O0 -Wall -Wextra -DDEBUG"
```

## Migration et changements récents

Le projet a récemment migré de la terminologie "IFFT" vers "LuxStral" pour mieux refléter l'algorithme de synthèse utilisé. Consultez `MIGRATION.md` pour les détails complets.

### Changements principaux
- `synth_IfftMode()` → `synth_LuxStralMode()`
- Réorganisation modulaire du code source
- Amélioration des performances et de la lisibilité

## Documentation supplémentaire

- `docs/raspberry/SETUP_RASPBERRY_PI_SSHFS.md` : Configuration Raspberry Pi
- `docs/auto_volume_spec.md` : Spécification du système de volume automatique
- `MIGRATION.md` : Guide de migration IFFT → LuxStral

## Scripts utiles

| Script | Description |
|--------|-------------|
| `scripts/build/build_mac_debug.sh` | Compilation macOS debug |
| `scripts/build/build_mac_release.sh` | Compilation macOS release |
| `scripts/build/build_raspberry_release.sh` | Compilation Raspberry Pi |
| `scripts/deploy_raspberry/deploy_to_pi.sh` | Déploiement sur Raspberry Pi |
| `test_debug_oscillators.sh` | Test et débogage des oscillateurs |

## Développement

### Structure modulaire
Le code est organisé en modules indépendants pour faciliter :
- La maintenance et les tests
- L'ajout de nouvelles fonctionnalités
- La compilation conditionnelle
- Le débogage ciblé

### Conventions de code
- Code source et commentaires en anglais
- Documentation utilisateur en français
- Messages de commit selon Conventional Commits
- Tests unitaires pour chaque module

## Licence

[À spécifier selon les besoins du projet]

## Contribution

Les contributions sont les bienvenues. Merci de :
1. Respecter l'architecture modulaire
2. Suivre les conventions de code établies
3. Tester sur Raspberry Pi avant soumission
4. Documenter les nouvelles fonctionnalités

Pour plus d'informations sur l'architecture et le développement, consultez la documentation dans le dossier `docs/`.
