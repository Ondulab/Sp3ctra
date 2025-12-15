# IMU Attitude Compensation System

## Vue d'Ensemble

Système de contrôle gestuel avec **compensation complète d'attitude** permettant un contrôle type "souris" indépendant de l'inclinaison du plateau IMU.

## Problème Résolu

### Avant (Double Intégration)
```
❌ Effet "bulle" - Doit incliner pour bouger
❌ Dépendant de l'orientation du plateau
❌ Dérive accélération
```

### Après (Projection Référentiel)
```
✅ Contrôle direct type souris
✅ Indépendant de l'inclinaison
✅ Pas de dérive (gyroscope direct)
```

## Architecture

### Pipeline Complet

```
IMU (plateau incliné)
    ↓
[1] Filtre Complémentaire 3D
    → Estime attitude (roll, pitch, yaw)
    ↓
[2] Matrice Rotation 3x3
    → Transforme référentiel plateau → horizontal
    ↓
[3] Projection Gyroscope
    → Gyro dans plan horizontal
    ↓
[4] Contrôle Curseur Direct
    → Position = ∫ gyro_horizontal dt
```

## Algorithmes

### 1. Filtre Complémentaire 3D

Fusionne gyroscope (précis court terme) et accéléromètre (référence gravité long terme) :

```c
/* Gyroscope integration */
roll_gyro = roll_prev + gyro_x * dt;
pitch_gyro = pitch_prev + gyro_y * dt;
yaw_gyro = yaw_prev + gyro_z * dt;

/* Accelerometer reference */
roll_accel = atan2(accel_y, sqrt(accel_x² + accel_z²));
pitch_accel = atan2(-accel_x, sqrt(accel_y² + accel_z²));

/* Complementary filter (alpha = 0.98) */
roll = 0.98 * roll_gyro + 0.02 * roll_accel;
pitch = 0.98 * pitch_gyro + 0.02 * pitch_accel;
yaw = yaw_gyro;  /* No magnetometer */
```

**Résultat** : Attitude stable sans dérive

### 2. Matrice de Rotation 3D

Transforme vecteurs du référentiel plateau vers référentiel horizontal :

```c
/* ZYX Euler convention */
R = Rz(yaw) * Ry(pitch) * Rx(roll)

/* Matrix elements */
R[0][0] = cos(yaw)*cos(pitch)
R[0][1] = cos(yaw)*sin(pitch)*sin(roll) - sin(yaw)*cos(roll)
R[0][2] = cos(yaw)*sin(pitch)*cos(roll) + sin(yaw)*sin(roll)
...
```

**Résultat** : Projection correcte quelque soit l'inclinaison

### 3. Projection Gyroscope

```c
/* Gyroscope dans référentiel plateau */
gyro_body = [gyro_x, gyro_y, gyro_z]

/* Projection dans référentiel horizontal */
gyro_horizontal = R * gyro_body

/* Contrôle curseur */
cursor_x += gyro_horizontal[0] * sensitivity * dt
cursor_y += gyro_horizontal[1] * sensitivity * dt
rotation_z = yaw * sensitivity
```

**Résultat** : Mouvement horizontal même si plateau incliné

## Conversions Unités

### Configuration IMU (250°/s, 4G)

```c
/* Gyroscope: raw → °/s → rad/s */
GYRO_SCALE = 250.0 / 32768.0  /* 16-bit ADC */
gyro_rad_s = raw_value * GYRO_SCALE * (π/180)

/* Accelerometer: raw → G → m/s² */
ACCEL_SCALE = 4.0 / 32768.0
accel_m_s2 = raw_value * ACCEL_SCALE * 9.81
```

## Paramètres Configurables

### Dans `imu_gesture.c` (lignes 18-23)

```c
DEFAULT_ALPHA = 0.98          /* Filtre complémentaire */
DEFAULT_SENSITIVITY_X = 0.02  /* Sensibilité X (gyro direct) */
DEFAULT_SENSITIVITY_Y = 0.02  /* Sensibilité Y (gyro direct) */
DEFAULT_SENSITIVITY_Z = 0.5   /* Sensibilité rotation Z */
DEFAULT_DEAD_ZONE = 0.5       /* Seuil bruit (°/s) */
DEFAULT_DAMPING = 0.95        /* Amortissement curseur */
```

### Ajustements Recommandés

**Si curseur trop sensible** :
```c
DEFAULT_SENSITIVITY_X = 0.01  /* Réduire */
DEFAULT_SENSITIVITY_Y = 0.01
```

**Si pas assez sensible** :
```c
DEFAULT_SENSITIVITY_X = 0.05  /* Augmenter */
DEFAULT_SENSITIVITY_Y = 0.05
```

**Si rotation dérive** :
```c
DEFAULT_SENSITIVITY_Z = 0.2   /* Réduire */
```

**Si curseur nerveux** :
```c
DEFAULT_DEAD_ZONE = 1.0       /* Augmenter seuil */
DEFAULT_DAMPING = 0.98        /* Plus d'amortissement */
```

## Logs de Diagnostic

### Format
```
[IMU_GESTURE] Cursor: (x, y) | Attitude: R=roll° P=pitch° Y=yaw° | Gyro_H: (gx, gy)°/s
```

### Exemple
```
[IMU_GESTURE] Cursor: (0.523, 0.687) | Attitude: R=15.2° P=-8.5° Y=45.3° | Gyro_H: (12.3, -5.7)°/s
```

### Interprétation

- **Cursor** : Position normalisée (0.0-1.0)
- **Attitude R/P/Y** : Orientation plateau (roll/pitch/yaw en degrés)
- **Gyro_H** : Vitesse rotation dans plan horizontal (°/s)

## Test de Validation

### Procédure

1. **Plateau horizontal** :
   - Tourner plateau → curseur bouge X/Y
   - Roll/Pitch ≈ 0°

2. **Plateau incliné 30°** :
   - Tourner plateau → curseur bouge TOUJOURS X/Y
   - Roll ou Pitch ≈ 30°
   - Gyro_H compensé automatiquement

3. **Rotation Z** :
   - Tourner autour axe vertical
   - Yaw change
   - Ligne jaune tourne dans visualisation

### Résultats Attendus

✅ **Mouvement X/Y identique** quelque soit inclinaison
✅ **Pas d'effet bulle** - Contrôle direct
✅ **Rotation Z visible** - Ligne jaune tourne
✅ **Attitude stable** - Roll/Pitch convergent

## Visualisation Test Mode

```
┌─────────────────────────────┐
│   Fond noir                 │
│                             │
│         ┼ ← Croix rouge     │
│         ⊕   Cercle          │
│         │   Ligne jaune     │
│         └─→ (rotation)      │
│                             │
│  Console:                   │
│  Attitude: R=15° P=-8° Y=45°│
│  Gyro_H: (12, -6)°/s        │
└─────────────────────────────┘
```

## Avantages Techniques

✅ **Compensation complète** - Matrice rotation 3D
✅ **Pas de dérive** - Filtre complémentaire
✅ **Réactif** - Gyroscope direct (pas d'intégration)
✅ **Stable** - Accéléromètre référence gravité
✅ **Indépendant inclinaison** - Projection référentiel
✅ **Configurable** - Tous paramètres ajustables
✅ **Thread-safe** - Mutex Context IMU

## Limitations

⚠️ **Yaw dérive** - Pas de magnétomètre (gyro pur)
⚠️ **Accélérations externes** - Perturbent roll/pitch
⚠️ **Calibration** - Nécessaire au démarrage

## Prochaines Étapes

1. Tester avec données IMU réelles
2. Ajuster sensibilités selon retour utilisateur
3. Vérifier compensation à différentes inclinaisons (0°, 30°, 45°)
4. Ajouter magnétomètre si disponible (yaw stable)
5. Intégrer dans système slitscan
6. Ajouter contrôles MIDI temps réel

## Références

- **Filtre Complémentaire** : Mahony et al. (2008)
- **Matrices Rotation** : Euler ZYX convention
- **IMU Fusion** : Madgwick AHRS algorithm (simplifié)
