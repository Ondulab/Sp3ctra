# Algorithme de Synthèse Additive Sp3ctra - Documentation Technique

## Vue d'ensemble

L'algorithme de synthèse additive développé pour Sp3ctra transforme des données d'image en temps réel en signaux audio multi-oscillateurs. Conçu initialement pour fonctionner sur STM32 avec des contraintes mémoire strictes, il utilise des techniques d'optimisation avancées pour générer un spectre sonore riche à partir d'une seule table d'onde de référence.

## Architecture Générale

### Principe de Base
- **Entrée** : Image RGB (3456 pixels à 400 DPI)
- **Traitement** : Conversion en intensités par note musicale
- **Sortie** : Signal audio stéréo 48 kHz

### Pipeline de Traitement
```
Image RGB → Extraction Couleur → Moyennage → Mapping Non-linéaire → 
Gap Limiter → Synthèse Additive → Modulation Contraste → Audio Final
```

## Optimisation Mémoire - Table d'Onde Unique

### Concept Révolutionnaire
Au lieu de stocker une table d'onde pour chaque fréquence (approche classique), l'algorithme utilise **une seule table de référence** pour l'octave de base.

### Mécanisme de Saut Adaptatif
```c
// Octave 0 : saut de 1 dans la table (fréquence de base)
// Octave 1 : saut de 2 dans la table (fréquence × 2)
// Octave 2 : saut de 4 dans la table (fréquence × 4)
// Octave n : saut de 2^n dans la table (fréquence × 2^n)

new_idx = (waves[note].current_idx + waves[note].octave_coeff);
if (new_idx >= waves[note].area_size) {
    new_idx -= waves[note].area_size;
}
```

### Avantages
- **Économie RAM massive** : ~95% de réduction mémoire vs approche classique
- **Cohérence harmonique** : Toutes les octaves basées sur la même forme d'onde
- **Performance** : Accès mémoire optimisé

## Extraction et Traitement Couleur

### Mode Mono - Conversion Niveaux de Gris
```c
// Poids perceptuels standards
gray[i] = (r * 299 + g * 587 + b * 114) * 65535 / 255000;
```

### Mode Stéréo - Séparation Perceptuelle
Utilise la théorie des couleurs opposées pour séparer les canaux :

#### Canal Chaud (Gauche)
```c
// Axe opponent rouge-bleu
O_rb = b_norm - r_norm;
// Axe opponent vert-magenta  
O_gm = (2.0f * g_norm - r_norm - b_norm) / 2.0f;
// Score de chaleur
S_warm = max(0, OPPONENT_ALPHA * O_rb + OPPONENT_BETA * O_gm);
```

#### Canal Froid (Droite)
```c
S_cold = max(0, OPPONENT_ALPHA * (-O_rb) + OPPONENT_BETA * (-O_gm));
```

### Inversion Couleur Adaptative
- **Fond blanc** : `volume = VOLUME_AMP_RESOLUTION - intensité`
- **Fond noir** : `volume = intensité` (pas d'inversion)

## Gap Limiter - Innovation Anti-Glitch

### Problématique
Les changements brutaux de volume créent des discontinuités audibles (clics, pops).

### Solution Physique
Limitation de la vitesse de changement basée sur les caractéristiques physiques de chaque fréquence.

#### Calcul du Maximum Volume Increment
```c
max_volume_increment = 
    sin(octave_coeff * 2π / area_size) * (WAVE_AMP_RESOLUTION/2) / 256;
```

#### Logique Physique
- **Fréquences graves** : Transitions lentes (cohérent avec la physique des basses)
- **Fréquences aiguës** : Transitions rapides (réactivité naturelle des aigus)
- **Pondération automatique** : Plus l'octave est élevée, plus le changement autorisé est important

### Algorithme de Transition
```c
#ifdef GAP_LIMITER
for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
        if (waves[note].current_volume < target_volume) {
            waves[note].current_volume += waves[note].volume_increment;
            if (waves[note].current_volume > target_volume) {
                waves[note].current_volume = target_volume;
            }
        } else {
            waves[note].current_volume -= waves[note].volume_decrement;
            if (waves[note].current_volume < target_volume) {
                waves[note].current_volume = target_volume;
            }
        }
        additiveBuffer[buff_idx] = waves[note].current_volume;
}
#endif
```

## Mapping Non-Linéaire

### Correction Gamma
```c
float normalizedIntensity = imageBuffer_f32[note] / VOLUME_AMP_RESOLUTION;
normalizedIntensity = pow(normalizedIntensity, GAMMA_VALUE); // 1.8f
imageBuffer_f32[note] = normalizedIntensity * VOLUME_AMP_RESOLUTION;
```

### Objectif
Compenser la réponse non-linéaire de la perception humaine pour un rendu plus naturel.

## Modulation de Contraste Dynamique

### Calcul de Contraste
```c
float calculate_contrast(int32_t *imageData, size_t size) {
    // Calcul de variance avec échantillonnage optimisé
    float variance = calculate_variance_with_sampling(imageData, size);
    float contrast_ratio = sqrt(variance) / sqrt(max_possible_variance);
    
    // Application courbe de réponse
    float adjusted_contrast = pow(contrast_ratio, CONTRAST_ADJUSTMENT_POWER);
    
    // Limitation entre CONTRAST_MIN et 1.0
    return CONTRAST_MIN + (1.0f - CONTRAST_MIN) * adjusted_contrast;
}
```

### Application
- **Images floues** : Volume réduit (facteur ≈ 0.2)
- **Images contrastées** : Volume maximal (facteur = 1.0)
- **Transition fluide** : Courbe de réponse progressive

## Optimisation Multi-Threading

### Pool de Threads Persistants
- **3 threads workers** : Division du spectre en 3 zones
- **Affinité CPU** : Attribution de threads à des cœurs spécifiques
- **Pré-calcul parallèle** : Données waves[] calculées en amont
- **Synchronisation optimisée** : Mutex et conditions pour coordination

### Répartition des Charges
```c
// Thread 0 : notes 0 à N/3
// Thread 1 : notes N/3 à 2N/3  
// Thread 2 : notes 2N/3 à N
```

## Normalisation Multi-Plateforme

### Compensation Automatique
```c
#ifdef __linux__
// Pi/Linux : Division par 3 (BossDAC/ALSA amplifie naturellement)
scale_float(additiveBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
#else
// Mac : Pas de division (CoreAudio ne compense pas)
#endif
```

## Lissage Temporel (Optionnel)

### Filtre Anti-Bruit Capteur
```c
#if ENABLE_IMAGE_TEMPORAL_SMOOTHING
filter->smoothed_values[i] = 
    alpha * filter->smoothed_values[i] + (1.0f - alpha) * current;
#endif
```

### Paramètres
- **Alpha** : 0.98f (lissage fort)
- **Seuil de bruit** : 0.001f (relatif à l'amplitude max)
- **Lissage adaptatif** : Basé sur la magnitude des variations

## Constantes Clés

### Résolutions
- `WAVE_AMP_RESOLUTION` : 16,777,215 (amplitude des formes d'onde)
- `VOLUME_AMP_RESOLUTION` : 65,535 (résolution volume 16-bit)
- `SAMPLING_FREQUENCY` : 48,000 Hz

### Paramètres Musicaux
- `START_FREQUENCY` : 65.41 Hz (Do grave)
- `SEMITONE_PER_OCTAVE` : 12
- `COMMA_PER_SEMITONE` : 36 (résolution micro-tonale)

### Contrôle Volume
- `VOLUME_INCREMENT` : 100 (vitesse montée)
- `VOLUME_DECREMENT` : 100 (vitesse descente)

## Performance et Optimisations

### Techniques d'Optimisation
1. **Échantillonnage adaptatif** : Contraste calculé avec stride
2. **Buffers locaux** : Évite les allocations dynamiques
3. **Calculs vectoriels** : Opérations SIMD quand possible
4. **Cache-friendly** : Accès mémoire séquentiels

### Métriques Typiques
- **Latence** : ~8.3ms (400 échantillons à 48 kHz)
- **CPU Usage** : ~15-25% sur Raspberry Pi 5
- **RAM Usage** : ~10MB (vs ~200MB approche classique)

## Conclusion

Cet algorithme représente une approche innovante de la synthèse additive, combinant :
- **Efficacité mémoire** exceptionnelle
- **Qualité audio** préservée
- **Réactivité temps réel** optimisée
- **Cohérence physique** dans le traitement

L'utilisation d'une table d'onde unique avec saut adaptatif, couplée au Gap Limiter physiquement motivé, crée un système de synthèse à la fois performant et musicalement cohérent.
