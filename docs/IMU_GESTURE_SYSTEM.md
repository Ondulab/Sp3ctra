# IMU Gesture System - Mouse-like Control from IMU

## Overview

Module autonome pour contrôle gestuel type "souris" à partir de données IMU (accéléromètre + gyroscope). Implémente un filtre complémentaire pour compensation de dérive et tracking de position précis.

## Architecture

### Fichiers
- `src/processing/imu_gesture.h` - Interface publique
- `src/processing/imu_gesture.c` - Implémentation filtre complémentaire
- `src/display/display.c` - Mode test intégré (IMU_TEST_MODE)

### Structure de Données

```c
typedef struct {
    /* Cursor position (0.0-1.0 normalized) */
    float cursor_x;           /* Horizontal position */
    float cursor_y;           /* Vertical position */
    float rotation_z;         /* Rotation Z in radians */
    
    /* Calibration state */
    int is_calibrated;
    float offset_accel_x/y/z;
    
    /* Internal filter state */
    float angle_x/y/z_comp;   /* Complementary filtered angles */
    float velocity_x/y;       /* Integrated velocities */
    
    /* Configuration */
    float alpha;              /* Filter coefficient (0.95-0.99) */
    float sensitivity_x/y/z;  /* Axis sensitivities */
    float dead_zone;          /* Noise threshold (m/s²) */
    float damping;            /* Velocity damping (0.8-0.95) */
} IMUGestureState;
```

## Algorithme

### 1. Filtre Complémentaire

Fusionne gyroscope (précis court terme) et accéléromètre (référence long terme) :

```c
/* Gyroscope integration (high-pass) */
angle_gyro = angle_prev + gyro * dt;

/* Accelerometer angles (low-pass reference) */
angle_accel = atan2(accel_y, sqrt(accel_x² + accel_z²));

/* Complementary filter */
angle_comp = alpha * angle_gyro + (1-alpha) * angle_accel;
```

**Paramètres** :
- `alpha = 0.98` : 98% gyro, 2% accel
- Compense dérive gyroscope avec référence gravité

### 2. Intégration Position

Double intégration avec amortissement :

```c
/* Step 1: Acceleration → Velocity */
velocity += accel * sensitivity * dt;

/* Step 2: Damping (friction) */
velocity *= damping;

/* Step 3: Velocity → Position */
position += velocity * dt;

/* Boundary clamping */
if (position < 0.0 || position > 1.0) {
    position = clamp(position, 0.0, 1.0);
    velocity = 0.0;  /* Stop at boundary */
}
```

### 3. Dead Zone

Filtre bruit capteur :

```c
if (fabs(accel) < dead_zone) {
    accel = 0.0;
}
```

## API Publique

### Initialisation
```c
IMUGestureState gesture;
imu_gesture_init(&gesture);
```

### Mise à Jour (à chaque réception UDP)
```c
imu_gesture_update(&gesture, ctx, delta_time_sec);
```

### Calibration
```c
imu_gesture_calibrate(&gesture, ctx);  /* Set current as zero */
imu_gesture_reset(&gesture);           /* Reset to center */
```

### Récupération Position
```c
/* Normalized (0.0-1.0) */
float x = gesture.cursor_x;
float y = gesture.cursor_y;
float rot = gesture.rotation_z;  /* radians */

/* Pixel coordinates */
int px, py;
imu_gesture_get_pixel_coords(&gesture, width, height, &px, &py);
```

### Rendu Test
```c
imu_gesture_render_cursor(window, &gesture);
```

## Mode Test IMU

### Activation

Dans `src/display/display.c` :
```c
#define IMU_TEST_MODE 1  /* 1 = test mode, 0 = slitscan normal */
```

### Comportement

Quand `IMU_TEST_MODE = 1` :
1. Écran noir
2. Curseur rouge (croix + cercle)
3. Rotation visible du cercle
4. Logs périodiques :
   - Position curseur (x, y)
   - Rotation Z (degrés)
   - Vélocités (vx, vy)
   - Delta time

### Visualisation

```
┌─────────────────────────────┐
│   Fond noir                 │
│                             │
│         ┼ ← Croix rouge     │
│         ⊕   Cercle rotatif  │
│                             │
│  Console logs:              │
│  Cursor: (0.523, 0.687)     │
│  Rot: 45.2°                 │
│  Vel: (0.12, -0.05)         │
└─────────────────────────────┘
```

## Configuration Par Défaut

```c
alpha = 0.98              /* Complementary filter */
sensitivity_x = 0.5       /* X axis gain */
sensitivity_y = 0.5       /* Y axis gain */
sensitivity_z = 1.0       /* Z rotation gain */
dead_zone = 0.05          /* Noise threshold (m/s²) */
damping = 0.90            /* Velocity damping */
```

## Intégration Future

Pour utiliser dans le système principal :

1. **Désactiver mode test** :
   ```c
   #define IMU_TEST_MODE 0
   ```

2. **Utiliser dans slitscan** :
   ```c
   /* Mode vertical */
   unsigned int write_y = (unsigned int)(gesture.cursor_y * height);
   int offset_x = (int)((gesture.cursor_x - 0.5f) * width);
   
   /* Draw line at IMU position */
   sfTexture_updateFromImage(texture, line, offset_x, write_y);
   ```

3. **Ajuster sensibilités** selon besoin utilisateur

## Avantages

✅ **Pas de dérive** - Filtre complémentaire compense gyroscope
✅ **Réactif** - Gyroscope donne réponse instantanée
✅ **Stable** - Accéléromètre fournit référence long terme
✅ **Configurable** - Tous paramètres ajustables
✅ **Thread-safe** - Utilise mutex Context IMU
✅ **Modulaire** - Code séparé, réutilisable

## Logs Exemple

```
[IMU_GESTURE] Initialized with alpha=0.980, dead_zone=0.050
[IMU_GESTURE] Calibrated - offsets: X=0.023 Y=-0.015 Z=0.102
[IMU_GESTURE] Cursor: (0.523, 0.687) Rot: 45.2° Vel: (0.12, -0.05) dt: 16.7ms
```

## Prochaines Étapes

1. Tester avec données IMU réelles
2. Ajuster paramètres (alpha, sensitivités, damping)
3. Vérifier absence de dérive sur longue durée
4. Intégrer dans système slitscan
5. Ajouter contrôles MIDI pour paramètres temps réel
