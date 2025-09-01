# Migration Guide: IFFT to Additive Renaming

## Vue d'ensemble

Ce document décrit les changements effectués lors de la refactorisation du code pour renommer le concept "IFFT" en "Additive" dans le projet Sp3ctra. Cette refactorisation a été réalisée pour mieux refléter la nature de l'algorithme de synthèse utilisé.

## Changements effectués

### Fichiers renommés

- `src/core/synth_ifft.c` → `src/synthesis/additive/synth_additive.c`
- `src/core/synth_ifft.h` → `src/synthesis/additive/synth_additive.h`

### Symboles renommés

#### Fonctions

- `synth_IfftMode()` → `synth_AdditiveMode()`
- `synth_IfftMode_Stateless()` → `synth_AdditiveMode_Stateless()`
- `synth_IfftMode_ThreadSafe()` → `synth_AdditiveMode_ThreadSafe()`

#### Variables et structures

- `ifftBuffer` → `additiveBuffer`
- `thread_ifftBuffer` → `thread_additiveBuffer`
- `gIfftInstance` → `gAdditiveInstance`

#### Constantes et définitions

- `IFFT_ENGINE` → `ADDITIVE_ENGINE`
- `CONFIG_IFFT_*` → `CONFIG_ADDITIVE_*`

### Fichiers de documentation mis à jour

- `docs/additive_synthesis_algorithm.md`
- `docs/additive_synthesis_improvements.md`
- `docs/audio_artifacts_analysis.md`
- `docs/audio_artifacts_solutions.md`
- `docs/phase_aware_gap_limiter_refactoring.md`

## Utilisation

### Avant

```c
// Initialisation
gIfftInstance = synth_ifft_create();

// Traitement audio
synth_IfftMode(imageData, audioBuffer);

// Mode thread-safe
synth_IfftMode_Stateless(imageData, audioBuffer, state);
```

### Après

```c
// Initialisation
gAdditiveInstance = synth_additive_create();

// Traitement audio
synth_AdditiveMode(imageData, audioBuffer);

// Mode thread-safe
synth_AdditiveMode_Stateless(imageData, audioBuffer, state);
```

## Compatibilité

Cette refactorisation est purement nominative et ne change pas le comportement du code. Aucune modification n'est nécessaire dans votre code client si vous utilisez les nouvelles API.

## Raison du changement

Le terme "IFFT" (Inverse Fast Fourier Transform) ne reflétait pas correctement l'algorithme utilisé, qui est en réalité une synthèse additive directe. Ce changement de nom clarifie l'intention et le fonctionnement du code.

## Notes supplémentaires

- Les API mathématiques liées à la véritable IFFT (comme celles de bibliothèques externes) n'ont pas été modifiées.
- Les commentaires et la documentation ont été mis à jour pour refléter la terminologie correcte.
