# CISYNTH - Synthétiseur Audio en Temps Réel

CISYNTH est un synthétiseur audio avancé développé en C/C++ qui peut fonctionner avec ou sans interface graphique. Il utilise RtAudio pour le traitement audio en temps réel et supporte la connectivité DMX pour le contrôle d'éclairage.

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
sudo apt install build-essential qt5-qmake qt5-default
sudo apt install libfftw3-dev libsndfile1-dev libasound2-dev
sudo apt install librtaudio-dev librtmidi-dev
sudo apt install libsfml-dev libcsfml-dev  # Pour l'interface graphique (optionnel)
```

## Compilation

### Compilation rapide en mode CLI (recommandée)

Pour compiler et reconstruire l'application en mode ligne de commande :

```bash
./rebuild_cli.sh --cli
```

Cette commande :
- Nettoie les fichiers de compilation précédents
- Compile l'application en mode CLI optimisé
- Affiche les instructions d'utilisation
- Liste les périphériques audio disponibles

### Compilation manuelle

Pour une compilation manuelle avec plus de contrôle :

```bash
# Compilation de base
./build_nogui.sh

# Compilation en mode CLI
./build_nogui.sh --cli

# Compilation et exécution directe
./build_nogui.sh --cli --run
```

## Utilisation

### Lancement de base

Après compilation, l'exécutable se trouve dans `build_nogui/CISYNTH_noGUI` :

```bash
# Lancement en mode CLI sans DMX (recommandé pour débuter)
./build_nogui/CISYNTH_noGUI --cli --no-dmx

# Lancement en mode CLI avec DMX
./build_nogui/CISYNTH_noGUI --cli
```

### Options disponibles

L'application supporte plusieurs options de ligne de commande :

| Option | Description |
|--------|-------------|
| `--cli` | Active le mode ligne de commande (sans interface graphique) |
| `--no-dmx` | Désactive complètement le support DMX |
| `--dmx-port=<port>` | Spécifie le port DMX à utiliser (ex: `/dev/tty.usbserial-XXX`) |
| `--silent-dmx` | Supprime les messages d'erreur DMX |
| `--list-audio-devices` | Affiche la liste des périphériques audio disponibles |

### Exemples d'utilisation

```bash
# Mode CLI de base sans DMX (idéal pour tester)
./build_nogui/CISYNTH_noGUI --cli --no-dmx

# Mode CLI avec port DMX spécifique
./build_nogui/CISYNTH_noGUI --cli --dmx-port=/dev/tty.usbserial-A106X4X3

# Mode CLI avec DMX silencieux
./build_nogui/CISYNTH_noGUI --cli --silent-dmx

# Lister les périphériques audio disponibles
./build_nogui/CISYNTH_noGUI --cli --no-dmx --list-audio-devices
```

## Contrôles

Une fois l'application lancée en mode CLI :
- **Ctrl+C** : Arrêter l'application
- L'application écoute les connexions MIDI et UDP pour le contrôle en temps réel
- Les paramètres audio peuvent être ajustés via l'API interne

## Optimisations

### Raspberry Pi 5 (ARM64)
Le projet inclut des optimisations spécifiques pour le Raspberry Pi 5 :
- Optimisations ARM Cortex-A76
- Support des instructions SIMD/NEON
- Optimisations mathématiques pour l'audio en temps réel

### Raspberry Pi 4 (ARM32)
Optimisations pour le Raspberry Pi 4 :
- Optimisations ARM Cortex-A72
- Support NEON FP
- Configuration spécifique ARM32

## Architecture du projet

```
src/
├── core/                 # Cœur du moteur audio
│   ├── audio_rtaudio.*   # Interface RtAudio
│   ├── synth.*           # Moteur de synthèse
│   ├── reverb.*          # Système de réverbération
│   ├── dmx.*             # Support DMX
│   ├── midi_controller.* # Contrôleur MIDI
│   └── kissfft/          # Bibliothèque FFT
├── main.cpp              # Point d'entrée Qt (mode GUI)
└── MainWindow.*          # Interface graphique Qt
```

## Dépannage

### Problèmes audio
```bash
# Vérifier les périphériques audio disponibles
./build_nogui/CISYNTH_noGUI --cli --no-dmx --list-audio-devices

# Sur Linux, vérifier ALSA
aplay -l
```

### Problèmes DMX
```bash
# Lancer sans DMX pour isoler le problème
./build_nogui/CISYNTH_noGUI --cli --no-dmx

# Vérifier les ports série disponibles
ls /dev/tty*
```

### Problèmes de compilation
```bash
# Nettoyer complètement
rm -rf build_nogui

# Recompiler depuis zéro
./rebuild_cli.sh --cli
```

## Scripts utiles

| Script | Description |
|--------|-------------|
| `build_nogui.sh` | Script de compilation principal |
| `rebuild_cli.sh` | Nettoyage et recompilation CLI |
| `kill_all_cisynth.sh` | Arrêt de toutes les instances |
| `launch_cisynth_optimized.sh` | Lancement optimisé |
| `optimize_pi5_system.sh` | Optimisations système Pi 5 |

## Configuration avancée

### Mode développement
Pour activer les informations de débogage et FPS :
```bash
# La macro PRINT_FPS est activée par défaut
# Pour plus de debug, modifier src/core/config.h
```

### Personnalisation audio
Les paramètres audio peuvent être ajustés dans `src/core/config.h` :
- Taille des buffers
- Fréquence d'échantillonnage
- Nombre de canaux

## Licence

[Indiquer ici la licence du projet]

## Contribution

Les contributions sont les bienvenues. Merci de suivre les conventions de code établies et de tester sur Raspberry Pi avant de soumettre une pull request.
