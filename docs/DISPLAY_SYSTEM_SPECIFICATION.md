# Spécification du Système d'Affichage

## Vue d'ensemble

Le système d'affichage Sp3ctra fournit une visualisation dynamique et configurable des données de capteurs entrantes avec support de multiples modes de défilement, contrôle de mouvement basé sur IMU, et contrôle MIDI en temps réel. Le système intègre deux effets de mouvement IMU indépendants : rotation sur l'axe z basée sur le gyroscope et positionnement de ligne basé sur les accéléromètres x et y.

## Architecture

### Composants

1. **Tampon d'Affichage** (`src/display/display_buffer.c`)
   - Tampon circulaire pour l'historique des lignes de balayage
   - Taille configurable (100-10000 lignes)
   - Gestion efficace de la mémoire

2. **Moteur de Rendu** (`src/display/display.c`)
   - Rendu basé sur SFML
   - Support d'orientation verticale/horizontale
   - Effets de zoom, fondu et persistance
   - Intégration du contrôle de mouvement IMU

3. **Configuration** (`src/config/config_display.h`)
   - Paramètres configurables à l'exécution
   - Réglages contrôlables par MIDI
   - Configuration persistante dans `sp3ctra.ini`

### Architecture des Données IMU

#### Structure de Contexte (`src/core/context.h`)

```c
/* Accélérations brutes (pour intégration locale) */
float imu_raw_x;           /* Accéléromètre X (m/s²) */
float imu_raw_y;           /* Accéléromètre Y (m/s²) */
float imu_raw_z;           /* Accéléromètre Z (m/s²) */

/* Gyroscope brut (pour référence) */
float imu_gyro_x;          /* Gyroscope X (rad/s) */
float imu_gyro_y;          /* Gyroscope Y (rad/s) */
float imu_gyro_z;          /* Gyroscope Z (rad/s) */

/* Angles pré-intégrés depuis le firmware du capteur */
float imu_angle_x;         /* Roulis (radians) */
float imu_angle_y;         /* Tangage (radians) */
float imu_angle_z;         /* Lacet (radians) - utilisé pour la rotation */

/* Positions pré-intégrées (non utilisées actuellement) */
float imu_position_x;
float imu_position_y;
float imu_position_z;
```

#### Flux de Données

```
Thread UDP (multithreading.c)
    ↓ [Analyse paquet IMU]
    ↓ [Verrouillage imu_mutex]
    ↓ [Mise à jour champs Context]
    ↓ [Déverrouillage imu_mutex]
    ↓
Thread Affichage (display.c)
    ↓ [Verrouillage imu_mutex]
    ↓ [Lecture données IMU]
    ↓ [Déverrouillage imu_mutex]
    ↓ [Traitement effets de mouvement]
```

## Paramètres de Configuration

### Paramètres d'Affichage Principaux

#### Orientation
```ini
orientation = 0
orientation_min = 0.0
orientation_max = 1.0
orientation_scaling = discrete
```
- **0** : Défilement vertical (haut/bas)
- **1** : Défilement horizontal (gauche/droite)
- **MIDI** : CC:35:0

#### Vitesse de Défilement UDP
```ini
udp_scroll_speed = 1.0
udp_scroll_speed_min = -1.0
udp_scroll_speed_max = 1.0
udp_scroll_speed_scaling = linear
```
- **Plage** : -1.0 (inverse) à +1.0 (avant)
- **0.0** : Pause
- **±1.0** : 100% de la vitesse de réception de base
- **MIDI** : CC:36:0

#### Contrôle de Position Accéléromètre Axe Y
```ini
accel_y_position_control = 0.0
accel_y_position_control_min = -1.0
accel_y_position_control_max = 1.0
accel_y_position_control_scaling = linear
```
- **Plage** : -1.0 à +1.0
- Contrôle le gain appliqué à la position pré-intégrée de l'accéléromètre Y
- Détermine la position de la ligne perpendiculaire au défilement
- **0.0** : Position centrée fixe (pas d'effet IMU)
- **±1.0** : Pleine amplitude de mouvement
- Voir section "Positionnement de Ligne IMU" pour détails
- **MIDI** : CC:37:0

#### Position Initiale de Ligne
```ini
initial_line_position = 1.0
initial_line_position_min = -1.0
initial_line_position_max = 1.0
initial_line_position_scaling = linear
```
- **+1.0** : Bord proche (bas en vertical, droite en horizontal)
- **0.0** : Centre de la fenêtre
- **-1.0** : Bord éloigné (haut en vertical, gauche en horizontal)
- **MIDI** : CC:38:0

#### Épaisseur de Ligne
```ini
line_thickness = 0.0
line_thickness_min = 0.0
line_thickness_max = 1.0
line_thickness_scaling = linear
```
- **0.0** : Ligne d'un seul pixel
- **1.0** : Fenêtre complète (mode code-barres)
- Valeurs intermédiaires : épaisseur proportionnelle
- **MIDI** : CC:39:0

### Paramètres d'Affichage Avancés

#### Temps de Transition
```ini
transition_time_ms = 100.0
transition_time_ms_min = 0.0
transition_time_ms_max = 1000.0
transition_time_ms_scaling = linear
```
- Durée de transition douce en millisecondes
- Empêche les changements de paramètres brusques
- **MIDI** : CC:40:0

#### Sensibilité Accéléromètre
```ini
accel_sensitivity = 1.0
accel_sensitivity_min = 0.1
accel_sensitivity_max = 5.0
accel_sensitivity_scaling = exponential
```
- Multiplicateur global pour la réactivité de l'accéléromètre
- Affecte le positionnement de ligne et les futurs effets de mouvement
- **1.0** : Sensibilité normale
- **<1.0** : Sensibilité réduite
- **>1.0** : Sensibilité augmentée
- **MIDI** : CC:41:0

#### Force de Fondu
```ini
fade_strength = 0.0
fade_strength_min = 0.0
fade_strength_max = 1.0
fade_strength_scaling = linear
```
- **0.0** : Pas de fondu (lignes nettes)
- **1.0** : Fondu maximum (mélange doux)
- Crée des transitions douces entre les lignes de balayage
- **MIDI** : CC:42:0

#### Persistance de Ligne
```ini
line_persistence = 0.0
line_persistence_min = 0.0
line_persistence_max = 10.0
line_persistence_scaling = linear
```
- Durée en secondes avant que les lignes ne s'estompent
- **0.0** : Persistance infinie (défilement classique)
- **>0.0** : Les lignes s'estompent après le temps spécifié
- **MIDI** : CC:43:0

#### Zoom d'Affichage
```ini
display_zoom = 0.0
display_zoom_min = -1.0
display_zoom_max = 1.0
display_zoom_scaling = linear
```
- **-1.0** : Zoom arrière 50%
- **0.0** : Taille normale 100%
- **+1.0** : Zoom avant 200%
- **Formule** : `zoom_factor = 1.0 + display_zoom`
- **MIDI** : CC:44:0

#### Taille du Tampon Historique
```ini
history_buffer_size = 1160
history_buffer_size_min = 100
history_buffer_size_max = 10000
history_buffer_size_scaling = discrete
```
- Nombre de lignes de balayage à conserver en mémoire
- Affecte l'utilisation mémoire et l'historique de défilement
- **MIDI** : CC:45:0

### Paramètres de Mouvement IMU

#### Rotation Gyroscope Activée
```ini
gyro_rotation_enabled = 0.0
gyro_rotation_enabled_min = 0.0
gyro_rotation_enabled_max = 1.0
gyro_rotation_enabled_scaling = discrete
```
- **0.0** : Rotation désactivée
- **1.0** : Rotation activée
- Contrôle la rotation d'image sur l'axe Z basée sur le gyroscope
- **MIDI** : CC:46:0

#### Sensibilité de Rotation Gyroscope
```ini
gyro_rotation_sensitivity = 1.0
gyro_rotation_sensitivity_min = 0.1
gyro_rotation_sensitivity_max = 5.0
gyro_rotation_sensitivity_scaling = exponential
```
- Multiplicateur de vitesse de rotation
- **1.0** : Vitesse de rotation normale
- **<1.0** : Rotation plus lente
- **>1.0** : Rotation plus rapide
- **MIDI** : CC:47:0

#### Lissage de Rotation
```ini
rotation_smoothing = 0.85
rotation_smoothing_min = 0.0
rotation_smoothing_max = 0.95
rotation_smoothing_scaling = linear
```
- Facteur de lissage exponentiel pour mouvement fluide
- **0.0** : Réponse instantanée (saccadée)
- **0.85** : Équilibré (RECOMMANDÉ)
- **0.95** : Très lisse (latence)
- Valeurs plus élevées = plus lisse mais plus de latence
- **MIDI** : CC:48:0

### Paramètres Fixes

#### Dimensions de Fenêtre
```ini
window_width = CIS_MAX_PIXELS_NB  # Dynamique basé sur capteur
window_height = 1160              # Hauteur par défaut
```
- Actuellement défini dans `config_display.h`
- Sera migré vers `sp3ctra.ini`

## Contrôle de Mouvement IMU

Le système d'affichage intègre deux effets de mouvement IMU **indépendants et complémentaires** :

1. **Rotation sur Axe Z** : Fait pivoter l'image entière autour de son centre (effet gyroscope)
2. **Positionnement de Ligne** : Contrôle où apparaissent les nouvelles lignes perpendiculairement au défilement (effet accéléromètre)

**Important** : Ces deux effets sont **indépendants des modes vertical/horizontal**. La rotation Z fonctionne de la même manière quel que soit le mode de défilement choisi.

### Rotation sur Axe Z

#### Algorithme (`src/display/display.c`)

```c
/* Lecture angle pré-intégré depuis capteur */
float imu_angle_z = ctx->imu_angle_z;  // Radians

/* Conversion en degrés et application sensibilité */
float target_angle = (imu_angle_z * 180.0f / M_PI) * sensitivity;

/* Lissage exponentiel pour mouvement fluide */
g_current_rotation_angle += (target_angle - g_current_rotation_angle) * (1.0f - smoothing);
```

#### Application SFML

```c
/* Définir origine au centre de texture */
sfSprite_setOrigin(sprite, (sfVector2f){
    texture_size.x / 2.0f,
    texture_size.y / 2.0f
});

/* Appliquer rotation */
sfSprite_setRotation(sprite, g_current_rotation_angle);

/* Positionner au centre de fenêtre */
sfSprite_setPosition(sprite, (sfVector2f){
    window_size.x / 2.0f,
    window_size.y / 2.0f
});
```

#### Caractéristiques

- **Dérive nulle** : Le firmware du capteur gère l'intégration et la correction
- **Priorité fluidité** : Facteur de lissage élevé (0.85 par défaut) pour mouvement doux
- **Décroissance automatique** : Retour à 0° quand désactivé
- **Pas de stabilisation horizon** : Rotation libre autour de l'axe Z

### Positionnement de Ligne

#### Algorithme Basé sur Position Pré-Intégrée

Le système utilise les positions pré-intégrées fournies par le firmware du capteur IMU, évitant ainsi les problèmes de dérive liés à l'intégration locale.

```c
/* Lecture position pré-intégrée depuis le capteur */
float imu_pos_y = ctx->imu_position_y;  /* Position intégrée par le firmware */

/* Application du gain de contrôle */
float position_gain = accel_y_position_control;

/* Calcul position normalisée avec sensibilité */
float normalized_position = 0.5f + (imu_pos_y * position_gain * accel_sensitivity);

/* Limitation pour garder ligne visible (5%-95% de la fenêtre) */
if (normalized_position < 0.05f) normalized_position = 0.05f;
if (normalized_position > 0.95f) normalized_position = 0.95f;
```

**Avantages de cette approche** :
- Pas de dérive cumulative (le firmware gère la correction)
- Réponse plus stable et prévisible
- Cohérence avec l'approche utilisée pour la rotation (angles pré-intégrés)

#### Mappage Spécifique à l'Orientation

**Mode Vertical** (défilement haut/bas) :
```c
/* IMU Y contrôle position horizontale (perpendiculaire au défilement) */
unsigned int line_x_pos = (unsigned int)(normalized_position * (width - 1));

/* Dessiner bande verticale à position X contrôlée par IMU */
sfTexture_updateFromImage(foreground_texture, vertical_strip, line_x_pos, 0);
```

**Mode Horizontal** (défilement gauche/droite) :
```c
/* IMU Y contrôle position verticale (perpendiculaire au défilement) */
unsigned int line_y_pos = (unsigned int)(normalized_position * (height - 1));

/* Dessiner bande horizontale à position Y contrôlée par IMU */
sfTexture_updateFromImage(foreground_texture, horizontal_strip, 0, line_y_pos);
```

#### Caractéristiques

- **Stabilité** : Utilise l'intégration du firmware (pas de dérive locale)
- **Toujours visible** : Limitation stricte [5%, 95%]
- **Contrôle perpendiculaire** : Position perpendiculaire à la direction de défilement
- **Réponse proportionnelle** : Le gain permet de doser l'amplitude de l'effet

## Modes de Comportement

### Mode Vertical (orientation = 0)

**Défilement Avant Normal (udp_scroll_speed > 0)**
- Nouvelles lignes apparaissent en bas
- Contenu défile vers le haut
- Anciennes lignes disparaissent en haut

**Défilement Inverse (udp_scroll_speed < 0)**
- Nouvelles lignes apparaissent en haut
- Contenu défile vers le bas
- Anciennes lignes disparaissent en bas

**Effets IMU (indépendants du mode de défilement)**
- `gyro_rotation_enabled` : Fait pivoter l'image entière autour du centre (rotation Z)
- `accel_y_position_control` : Contrôle la position horizontale des nouvelles lignes (perpendiculaire au défilement)
- Lignes dessinées à position X contrôlée par IMU créent effet de dessin fluide

**Mode Code-Barres (line_thickness = 1.0)**
- **Optimisation importante** : Quand l'épaisseur est à 100%, le système n'a plus besoin de calculer l'historique de défilement
- Ligne unique remplit toute la hauteur de la fenêtre
- Crée un effet de code-barres vertical dynamique
- IMU contrôle la position horizontale du code-barres en temps réel
- **Cas d'usage** : Visualisation minimaliste, performances optimales, effet stroboscopique

### Mode Horizontal (orientation = 1)

**Défilement Avant Normal (udp_scroll_speed > 0)**
- Nouvelles lignes apparaissent à droite
- Contenu défile vers la gauche
- Anciennes lignes disparaissent à gauche

**Défilement Inverse (udp_scroll_speed < 0)**
- Nouvelles lignes apparaissent à gauche
- Contenu défile vers la droite
- Anciennes lignes disparaissent à droite

**Effets IMU (indépendants du mode de défilement)**
- `gyro_rotation_enabled` : Fait pivoter l'image entière autour du centre (rotation Z)
- `accel_y_position_control` : Contrôle la position verticale des nouvelles lignes (perpendiculaire au défilement)
- Lignes dessinées à position Y contrôlée par IMU créent effet de dessin fluide

**Mode Code-Barres (line_thickness = 1.0)**
- **Optimisation importante** : Quand l'épaisseur est à 100%, le système n'a plus besoin de calculer l'historique de défilement
- Ligne unique remplit toute la largeur de la fenêtre
- Crée un effet de code-barres horizontal dynamique
- IMU contrôle la position verticale du code-barres en temps réel
- **Cas d'usage** : Visualisation minimaliste, performances optimales, effet stroboscopique

## Mappage Contrôle MIDI

Tous les paramètres d'affichage sont mappés sur MIDI CC Canal 1 :

| Paramètre | Numéro CC | Plage | Description |
|-----------|-----------|-------|-------------|
| orientation | 35 | 0-1 | Vertical/Horizontal |
| udp_scroll_speed | 36 | -1.0 à +1.0 | Vitesse/direction défilement |
| accel_y_position_control | 37 | -1.0 à +1.0 | Gain position axe Y |
| initial_line_position | 38 | -1.0 à +1.0 | Position apparition ligne |
| line_thickness | 39 | 0.0 à 1.0 | Épaisseur ligne |
| transition_time_ms | 40 | 0-1000 | Temps transition douce |
| accel_sensitivity | 41 | 0.1-5.0 | Sensibilité accéléromètre |
| fade_strength | 42 | 0.0-1.0 | Force effet fondu |
| line_persistence | 43 | 0-10 | Durée vie ligne (secondes) |
| display_zoom | 44 | -1.0 à +1.0 | Facteur zoom |
| history_buffer_size | 45 | 100-10000 | Taille tampon (lignes) |
| gyro_rotation_enabled | 46 | 0-1 | Activer/désactiver rotation |
| gyro_rotation_sensitivity | 47 | 0.1-5.0 | Vitesse rotation |
| rotation_smoothing | 48 | 0.0-0.95 | Facteur lissage |

**Réservé** : CC:49-63 pour futures fonctionnalités d'affichage
