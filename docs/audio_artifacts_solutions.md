# Solutions pour réduire les artefacts sonores

## Solution 1 : Lissage temporel des données d'image (Priorité 1)

### Implémentation d'un filtre passe-bas temporel

```c
// Configuration dans config.h
#define IMAGE_TEMPORAL_SMOOTHING_ALPHA 0.85f  // Facteur de lissage (0.0-1.0)
#define IMAGE_NOISE_GATE_THRESHOLD 0.02f      // Seuil de bruit relatif

// Structure pour le lissage temporel
typedef struct {
    float previous_values[CIS_MAX_PIXELS_NB];
    float smoothed_values[CIS_MAX_PIXELS_NB];
    int initialized;
} ImageTemporalFilter;

// Fonction de lissage exponentiel avec gate de bruit
void apply_temporal_smoothing(int32_t *current_data, ImageTemporalFilter *filter) {
    const float alpha = IMAGE_TEMPORAL_SMOOTHING_ALPHA;
    const float noise_threshold = IMAGE_NOISE_GATE_THRESHOLD * VOLUME_AMP_RESOLUTION;
    
    for (int i = 0; i < CIS_MAX_PIXELS_NB; i++) {
        float current = (float)current_data[i];
        
        if (!filter->initialized) {
            filter->smoothed_values[i] = current;
            filter->previous_values[i] = current;
        } else {
            // Calcul de la différence relative
            float diff = fabsf(current - filter->previous_values[i]);
            
            if (diff < noise_threshold) {
                // Variation faible : lissage fort
                filter->smoothed_values[i] = alpha * filter->smoothed_values[i] + 
                                           (1.0f - alpha) * current;
            } else {
                // Variation significative : lissage réduit pour préserver la réactivité
                float adaptive_alpha = alpha * 0.7f;
                filter->smoothed_values[i] = adaptive_alpha * filter->smoothed_values[i] + 
                                           (1.0f - adaptive_alpha) * current;
            }
        }
        
        filter->previous_values[i] = current;
        current_data[i] = (int32_t)filter->smoothed_values[i];
    }
    
    filter->initialized = 1;
}
```

### Intégration dans le pipeline de synthèse

**Modification de `synth_AudioProcess()` :**
```c
// Ajouter après la conversion greyScale
static ImageTemporalFilter mono_filter = {0};
apply_temporal_smoothing(processed_grayScale, &mono_filter);
```

**Modification de `synth_AudioProcessStereo()` :**
```c
// Ajouter après l'extraction des canaux
static ImageTemporalFilter warm_filter = {0};
static ImageTemporalFilter cold_filter = {0};
apply_temporal_smoothing(processed_warmChannel, &warm_filter);
apply_temporal_smoothing(processed_coldChannel, &cold_filter);
```

## Solution 2 : Unification du traitement stéréo (Priorité 2)

### Élimination des conditions de course

```c
// Nouvelle structure pour les données thread-safe
typedef struct {
    // État local par thread pour éviter la contention
    uint32_t local_current_idx[NUMBER_OF_NOTES];
    float local_current_volume[NUMBER_OF_NOTES];
    int thread_initialized;
    
    // Buffers de travail locaux
    float thread_additiveBuffer[AUDIO_BUFFER_SIZE];
    float thread_sumVolumeBuffer[AUDIO_BUFFER_SIZE];
    float thread_maxVolumeBuffer[AUDIO_BUFFER_SIZE];
} ThreadSafeAudioState;

// Initialisation thread-local
static __thread ThreadSafeAudioState thread_state = {0};

// Version unifiée pour les deux canaux stéréo
void synth_AdditiveMode_ThreadSafe(int32_t *imageData, float *audioData, 
                               ThreadSafeAudioState *state) {
    // Utiliser l'état local au lieu de l'état global
    // Évite complètement la contention entre threads
}
```

## Solution 3 : Amélioration de la résolution numérique (Priorité 3)

### Pipeline haute précision

```c
// Nouvelles définitions de résolution
#define IMAGE_PROCESSING_RESOLUTION (1048576)  // 20-bit pour traitement interne
#define VOLUME_PROCESSING_RESOLUTION (16777216) // 24-bit pour volume

// Conversion haute précision RGB → luminance
uint32_t greyScale_HighPrecision(uint8_t *buffer_R, uint8_t *buffer_G, 
                                uint8_t *buffer_B, float *gray_float, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        // Conversion en float dès le début pour éviter les pertes
        float r = (float)buffer_R[i] / 255.0f;
        float g = (float)buffer_G[i] / 255.0f;
        float b = (float)buffer_B[i] / 255.0f;
        
        // Calcul en précision flottante
        float luminance = PERCEPTUAL_WEIGHT_R * r + 
                         PERCEPTUAL_WEIGHT_G * g + 
                         PERCEPTUAL_WEIGHT_B * b;
        
        // Stockage en float pour éviter les conversions multiples
        gray_float[i] = luminance * IMAGE_PROCESSING_RESOLUTION;
    }
    return 0;
}
```

## Solution 4 : Optimisation des transitions de volume (Priorité 4)

### GAP_LIMITER amélioré avec courbes de transition

```c
// Configuration avancée du gap limiter
#define GAP_LIMITER_ATTACK_TIME_MS 5.0f   // Temps de montée rapide
#define GAP_LIMITER_RELEASE_TIME_MS 50.0f // Temps de descente lent
#define GAP_LIMITER_CURVE_POWER 2.0f      // Courbe exponentielle

// Calcul des coefficients adaptatifs
void calculate_adaptive_gap_limiter(float target_volume, float current_volume,
                                   float *increment, float *decrement) {
    float volume_diff = target_volume - current_volume;
    float abs_diff = fabsf(volume_diff);
    
    // Coefficients de base
    float base_increment = waves[note].volume_increment;
    float base_decrement = waves[note].volume_decrement;
    
    if (volume_diff > 0) {
        // Montée : rapide pour les petites variations, modérée pour les grandes
        float attack_factor = 1.0f + (abs_diff / VOLUME_AMP_RESOLUTION) * 3.0f;
        *increment = base_increment * attack_factor;
        *decrement = base_decrement;
    } else {
        // Descente : toujours progressive pour éviter les clics
        float release_factor = 0.5f + (abs_diff / VOLUME_AMP_RESOLUTION) * 0.5f;
        *increment = base_increment;
        *decrement = base_decrement * release_factor;
    }
}
```

## Solution 5 : Filtrage adaptatif basé sur le contenu (Priorité 5)

### Détection automatique du type de contenu

```c
// Analyse du contenu d'image pour adapter le filtrage
typedef enum {
    CONTENT_STATIC,      // Image statique ou peu de mouvement
    CONTENT_SMOOTH,      // Transitions douces
    CONTENT_DYNAMIC,     // Mouvement rapide
    CONTENT_NOISY        // Contenu bruité nécessitant un filtrage fort
} ContentType;

ContentType analyze_image_content(int32_t *imageData, int32_t *previous_frame) {
    float total_variation = 0.0f;
    float max_variation = 0.0f;
    int high_freq_count = 0;
    
    for (int i = 1; i < CIS_MAX_PIXELS_NB - 1; i++) {
        // Variation temporelle
        float temporal_diff = fabsf((float)(imageData[i] - previous_frame[i]));
        total_variation += temporal_diff;
        if (temporal_diff > max_variation) max_variation = temporal_diff;
        
        // Variation spatiale (détection de bruit haute fréquence)
        float spatial_diff = fabsf((float)(imageData[i] - imageData[i-1])) +
                           fabsf((float)(imageData[i+1] - imageData[i]));
        if (spatial_diff > VOLUME_AMP_RESOLUTION * 0.1f) high_freq_count++;
    }
    
    float avg_variation = total_variation / CIS_MAX_PIXELS_NB;
    float noise_ratio = (float)high_freq_count / CIS_MAX_PIXELS_NB;
    
    if (avg_variation < VOLUME_AMP_RESOLUTION * 0.01f) return CONTENT_STATIC;
    if (noise_ratio > 0.3f) return CONTENT_NOISY;
    if (max_variation > VOLUME_AMP_RESOLUTION * 0.5f) return CONTENT_DYNAMIC;
    return CONTENT_SMOOTH;
}

// Application du filtrage adaptatif
void apply_adaptive_filtering(int32_t *imageData, ContentType content_type) {
    switch (content_type) {
        case CONTENT_STATIC:
            // Filtrage minimal pour préserver la réactivité
            break;
        case CONTENT_SMOOTH:
            // Filtrage modéré
            apply_temporal_smoothing(imageData, &filter);
            break;
        case CONTENT_DYNAMIC:
            // Filtrage léger avec réactivité préservée
            apply_temporal_smoothing_fast(imageData, &filter);
            break;
        case CONTENT_NOISY:
            // Filtrage fort pour éliminer le bruit
            apply_temporal_smoothing_strong(imageData, &filter);
            break;
    }
}
```

## Configuration recommandée

### Paramètres optimaux pour différents cas d'usage

```c
// Mode performance (latence minimale)
#define IMAGE_TEMPORAL_SMOOTHING_ALPHA 0.7f
#define GAP_LIMITER_ATTACK_TIME_MS 3.0f
#define GAP_LIMITER_RELEASE_TIME_MS 30.0f

// Mode qualité (artefacts minimaux)
#define IMAGE_TEMPORAL_SMOOTHING_ALPHA 0.9f
#define GAP_LIMITER_ATTACK_TIME_MS 8.0f
#define GAP_LIMITER_RELEASE_TIME_MS 80.0f

// Mode équilibré (recommandé)
#define IMAGE_TEMPORAL_SMOOTHING_ALPHA 0.85f
#define GAP_LIMITER_ATTACK_TIME_MS 5.0f
#define GAP_LIMITER_RELEASE_TIME_MS 50.0f
```

## Tests et validation

### Métriques de qualité audio

```c
// Fonction de mesure des artefacts
typedef struct {
    float click_detection_threshold;
    float pop_detection_threshold;
    int artifact_count;
    float total_harmonic_distortion;
} AudioQualityMetrics;

void measure_audio_quality(float *audioData, AudioQualityMetrics *metrics) {
    // Détection des discontinuités (clics/pops)
    for (int i = 1; i < AUDIO_BUFFER_SIZE; i++) {
        float diff = fabsf(audioData[i] - audioData[i-1]);
        if (diff > metrics->click_detection_threshold) {
            metrics->artifact_count++;
        }
    }
    
    // Calcul de la distorsion harmonique totale
    // (implémentation FFT pour analyse spectrale)
}
```

Ces solutions, implémentées progressivement, devraient considérablement réduire les artefacts sonores disgracieux causés par les fluctuations du flux d'image, particulièrement en mode couleur où les problèmes sont amplifiés.
