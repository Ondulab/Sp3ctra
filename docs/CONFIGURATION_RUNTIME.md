# Configuration Runtime de la Synthèse Additive

## Vue d'ensemble

Le système Sp3ctra permet maintenant de modifier les paramètres de la synthèse additive sans recompilation, grâce à un fichier de configuration runtime au format INI.

## Fichier de configuration

### Emplacement
Le fichier de configuration `additive_synth.ini` doit être placé dans le répertoire de travail de l'application.

### Création automatique
Si le fichier n'existe pas au démarrage, l'application le crée automatiquement avec les valeurs par défaut.

### Format
Le fichier utilise le format INI standard avec des sections et des paramètres clé=valeur.

## Paramètres disponibles

### Section [auto_volume]
Contrôle le système de volume automatique basé sur l'activité IMU.

| Paramètre | Type | Plage | Défaut | Description |
|-----------|------|-------|--------|-------------|
| `imu_active_threshold_x` | float | 0.0-10.0 | 0.01 | Seuil d'accélération X pour considérer l'activité |
| `imu_filter_alpha_x` | float | 0.0-1.0 | 0.25 | Facteur de lissage exponentiel pour l'accélération X |
| `imu_inactivity_timeout_s` | int | 1-3600 | 5 | Délai d'inactivité avant atténuation (secondes) |
| `auto_volume_inactive_level` | float | 0.0-1.0 | 0.01 | Volume cible en mode inactif |
| `auto_volume_active_level` | float | 0.0-1.0 | 1.0 | Volume cible en mode actif |
| `auto_volume_fade_ms` | int | 10-10000 | 600 | Durée de transition (millisecondes) |
| `auto_volume_poll_ms` | int | 1-1000 | 10 | Fréquence de mise à jour (millisecondes) |

### Section [synthesis]
Contrôle les paramètres de base de la synthèse additive.

| Paramètre | Type | Plage | Défaut | Description |
|-----------|------|-------|--------|-------------|
| `start_frequency` | float | 20.0-20000.0 | 65.41 | Fréquence de base (Hz) |
| `semitone_per_octave` | int | 1-24 | 12 | Nombre de demi-tons par octave |
| `comma_per_semitone` | int | 1-100 | 36 | Subdivisions par demi-ton |
| `volume_increment` | int | 1-100 | 1 | Incrément de volume |
| `volume_decrement` | int | 1-100 | 1 | Décrément de volume |

**Note importante :** Le paramètre `pixels_per_note` reste une constante de compilation (définie dans `config_synth_additive.h`) car il est utilisé pour dimensionner des tableaux dans des structures. Il ne peut pas être modifié à l'exécution.

## Exemple de fichier de configuration

```ini
# Sp3ctra Additive Synthesis Configuration
# This file was automatically generated with default values
# Modify these values as needed - the program will validate them on startup

[auto_volume]
imu_active_threshold_x = 0.020
imu_filter_alpha_x = 0.300
imu_inactivity_timeout_s = 10
auto_volume_inactive_level = 0.020
auto_volume_active_level = 0.900
auto_volume_fade_ms = 800
auto_volume_poll_ms = 15

[synthesis]
start_frequency = 70.00
semitone_per_octave = 12
comma_per_semitone = 24
volume_increment = 2
volume_decrement = 2
pixels_per_note = 2
```

## Validation et gestion d'erreurs

### Validation automatique
- Tous les paramètres sont validés au démarrage
- Les valeurs hors limites provoquent l'arrêt du programme avec un message d'erreur explicite
- Les paramètres inconnus génèrent des avertissements mais n'arrêtent pas l'exécution

### Messages d'erreur typiques
```
[CONFIG ERROR] imu_active_threshold_x must be between 0.0 and 10.0, got 15.000
[CONFIG ERROR] Configuration validation failed with 1 error(s). Exiting.
```

### Récupération d'erreurs
- Si le fichier est corrompu, supprimez-le : l'application le recréera avec les valeurs par défaut
- Les erreurs de syntaxe (format INI invalide) provoquent l'arrêt avec indication de la ligne problématique

## Utilisation

### Modification des paramètres
1. Éditez le fichier `additive_synth.ini` avec un éditeur de texte
2. Modifiez les valeurs souhaitées en respectant les plages autorisées
3. Sauvegardez le fichier
4. Redémarrez l'application pour appliquer les changements

### Retour aux valeurs par défaut
Supprimez le fichier `additive_synth.ini` et redémarrez l'application.

## Avantages

- **Pas de recompilation** : Modification des paramètres sans rebuild
- **Validation stricte** : Prévention des valeurs invalides
- **Rétrocompatibilité** : Fonctionnement identique si le fichier n'existe pas
- **Documentation intégrée** : Commentaires dans le fichier généré
- **Gestion d'erreurs robuste** : Messages d'erreur clairs et récupération automatique

## Architecture technique

### Fichiers impliqués
- `src/config/config_loader.h` : Interface du système de configuration
- `src/config/config_loader.c` : Implémentation du parseur INI et validation
- `src/config/config_synth_additive.h` : Définitions des structures de données
- `additive_synth.ini` : Fichier de configuration runtime

### Intégration
Le système remplace les anciennes définitions `#define` par des variables globales chargées dynamiquement au démarrage de l'application.
