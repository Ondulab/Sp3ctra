# Améliorations Proposées - Algorithme de Synthèse Additive Sp3ctra

## Vue d'ensemble

Ce document présente les améliorations identifiées pour résoudre les artefacts audio persistants (sauts disgracieux) dans l'algorithme de synthèse additive actuel. Les solutions proposées maintiennent la cohérence physique du système tout en améliorant significativement la qualité audio.

## Problématique Actuelle

### Symptômes Observés
- **Sauts disgracieux** : Clics et pops audibles malgré le Gap Limiter
- **Artefacts sur changements brusques** : Particulièrement visibles lors de transitions d'image rapides
- **Incohérence temporelle** : Changements de volume non synchronisés avec la phase du signal

### Analyse des Causes
1. **max_volume_increment trop élevé** : Valeurs comme 1,123.30 pour les octaves aiguës permettent des sauts trop importants
2. **Mise à jour asynchrone** : Changements de volume appliqués à n'importe quelle phase du cycle
3. **Manque de pondération temporelle** : Pas de prise en compte de l'amplitude instantanée

## Solution Recommandée : Pondération par Phase

### Principe Physique

L'amélioration principale consiste à **pondérer max_volume_increment/decrement selon la position dans le cycle sinusoïdal**, respectant ainsi les lois de l'acoustique et de l'énergie instantanée.

#### Logique Acoustique Fondamentale
- **Aux croisements zéro** : Énergie instantanée nulle → changements maximaux autorisés (aucune discontinuité)
- **Aux pics d'amplitude** : Énergie instantanée maximale → **aucun changement autorisé** (évite les artefacts)
- **Phases intermédiaires** : Modulation progressive selon l'énergie du signal

### Formule de Pondération Correcte

```c
// Facteur de pondération basé sur la position dans le cycle sinusoïdal
float current_phase_amplitude = fabs(waveBuffer[buff_idx]);
float max_amplitude = WAVE_AMP_RESOLUTION / 2.0f;
float phase_factor = 1.0f - (current_phase_amplitude / max_amplitude);

// Pondération du max_volume_increment par la phase
float adaptive_increment = waves[note].volume_increment * phase_factor;
float adaptive_decrement = waves[note].volume_decrement * phase_factor;
```

#### Comportement Résultant (Physiquement Cohérent)
- **Croisement zéro** (amplitude = 0) : `phase_factor = 1.0` → **changement maximal autorisé**
- **Pic sinusoïde** (amplitude = max) : `phase_factor = 0.0` → **aucun changement** (protection totale)
- **Phases intermédiaires** : Modulation progressive selon l'énergie instantanée

## Implémentation Détaillée

### Modification du Gap Limiter

```c
#ifdef GAP_LIMITER_PHASE_AWARE
for (buff_idx = 0; buff_idx < AUDIO_BUFFER_SIZE; buff_idx++) {
    // Calcul du facteur de phase
    float current_amplitude = fabs(waveBuffer[buff_idx]);
    float max_amplitude = WAVE_AMP_RESOLUTION / 2.0f;
    float phase_factor = 1.0f - (current_amplitude / max_amplitude);
    
    // Seuil minimum pour éviter les blocages complets
    if (phase_factor < MIN_PHASE_FACTOR) {
        phase_factor = MIN_PHASE_FACTOR;
    }
    
    // Application adaptative
    float target_volume = imageBuffer_f32[note];
    float volume_diff = target_volume - waves[note].current_volume;
    
    if (fabs(volume_diff) > SMALL_CHANGE_THRESHOLD) {
        // Gros changement : pondération par phase
        float adaptive_increment = waves[note].volume_increment * phase_factor;
        if (volume_diff > 0) {
            waves[note].current_volume += adaptive_increment;
            if (waves[note].current_volume > target_volume) {
                waves[note].current_volume = target_volume;
            }
        } else {
            waves[note].current_volume -= adaptive_increment;
            if (waves[note].current_volume < target_volume) {
                waves[note].current_volume = target_volume;
            }
        }
    } else {
        // Petit changement : application directe
        waves[note].current_volume = target_volume;
    }
    
    volumeBuffer[buff_idx] = waves[note].current_volume;
}
#endif
```

### Nouvelles Constantes de Configuration

```c
// Dans config.h
#define ENABLE_PHASE_AWARE_GAP_LIMITER 1
#define MIN_PHASE_FACTOR 0.1f           // Facteur minimum (évite blocage complet)
#define SMALL_CHANGE_THRESHOLD 1000.0f  // Seuil petit/gros changement
#define PHASE_SENSITIVITY 1.0f          // Sensibilité à la phase (0.5-2.0)
```

## Améliorations Complémentaires

### 1. Seuil Adaptatif par Fréquence

```c
// Seuil proportionnel à la fréquence
float freq_factor = waves[note].frequency / START_FREQUENCY;
float adaptive_threshold = SMALL_CHANGE_THRESHOLD * sqrt(freq_factor);
```

**Justification** : Les basses nécessitent plus de précaution que les aigus.

### 2. Hystérésis pour Éviter l'Oscillation

```c
// État précédent pour éviter les oscillations
static float previous_phase_factor[NUMBER_OF_NOTES];
float smoothed_phase_factor = 0.7f * phase_factor + 0.3f * previous_phase_factor[note];
previous_phase_factor[note] = smoothed_phase_factor;
```

### 3. Mode Debug pour Validation

```c
#ifdef DEBUG_PHASE_AWARE_GAP_LIMITER
if (note == DEBUG_NOTE_INDEX && buff_idx % 100 == 0) {
    printf("Note %d: amp=%.2f, phase_factor=%.3f, vol_change=%.2f\n",
           note, current_amplitude, phase_factor, adaptive_increment);
}
#endif
```

## Alternatives Évaluées

### Option A : Réduction Arbitraire de max_volume_increment
```c
// REJETÉE : Pas de base physique
waves[note].max_volume_increment /= 10.0f;
```
**Problème** : Coefficients arbitraires, perte de cohérence physique.

### Option B : Synchronisation Stricte sur Croisements Zéro
```c
// REJETÉE : Crée des "pas" audibles
if (fabs(waveBuffer[buff_idx]) < ZERO_CROSSING_THRESHOLD) {
    // Autoriser changement seulement ici
}
```
**Problème** : Quantification temporelle, artefacts fréquentiels.

### Option C : Pondération par Phase (RETENUE)
**Avantages** :
- Base physique solide
- Transition continue
- Pas d'artefacts fréquentiels
- Adaptabilité par fréquence

## Plan d'Implémentation

### Phase 1 : Implémentation de Base
1. **Ajouter les constantes** dans `config.h`
2. **Modifier le Gap Limiter** dans `synth_additive.c`
3. **Tests unitaires** sur quelques notes représentatives

### Phase 2 : Optimisation
1. **Profiling performance** : Mesurer l'impact CPU
2. **Ajustement des seuils** : Optimiser les constantes
3. **Tests audio** : Validation subjective

### Phase 3 : Fonctionnalités Avancées
1. **Seuil adaptatif** par fréquence
2. **Hystérésis** anti-oscillation
3. **Mode debug** pour analyse

## Métriques de Validation

### Tests Objectifs
- **THD+N** : Distorsion harmonique + bruit
- **Analyse spectrale** : Détection d'harmoniques parasites
- **Mesure de clics** : Comptage automatique des discontinuités

### Tests Subjectifs
- **Écoute critique** : Panel d'auditeurs expérimentés
- **Comparaison A/B** : Avant/après amélioration
- **Scénarios d'usage** : Images variées (contrastées, floues, animées)

## Impact Attendu

### Améliorations Audio
- **Réduction drastique** des clics et pops
- **Transitions plus musicales** : Respect de la phase naturelle
- **Cohérence spectrale** : Pas d'harmoniques parasites

### Performance
- **Impact CPU minimal** : ~2-5% d'augmentation
- **Latence inchangée** : Pas d'impact sur le temps réel
- **Mémoire stable** : Pas d'allocation supplémentaire

### Compatibilité
- **Rétrocompatible** : Flag de compilation pour activation
- **Multi-plateforme** : Fonctionne sur STM32, Pi, Mac
- **Configurable** : Paramètres ajustables selon l'usage

## Conclusion

La pondération par phase représente une évolution naturelle de l'algorithme Gap Limiter existant. Elle préserve la philosophie physique originale tout en résolvant les artefacts résiduels par une approche acoustiquement cohérente.

Cette amélioration transforme un système déjà innovant en une solution de synthèse additive de qualité professionnelle, maintenant l'efficacité mémoire exceptionnelle tout en atteignant une qualité audio irréprochable.

## Références Techniques

### Acoustique
- **Théorie des discontinuités** : Impact des changements de phase sur la perception
- **Psychoacoustique** : Seuils de détection des artefacts temporels

### Traitement du Signal
- **Fenêtrage adaptatif** : Techniques de modulation d'amplitude
- **Anti-aliasing temporel** : Prévention des artefacts de quantification

### Optimisation
- **Calculs vectoriels** : Optimisation SIMD pour les opérations de phase
- **Cache efficiency** : Minimisation des accès mémoire aléatoires
