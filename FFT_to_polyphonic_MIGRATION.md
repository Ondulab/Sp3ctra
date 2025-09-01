# Migration Guide: FFT to Polyphonic Renaming

## Vue d'ensemble

Ce document décrit les changements effectués lors de la refactorisation du code pour renommer le concept "FFT" en "Polyphonic" dans le projet Sp3ctra. Cette refactorisation a été réalisée pour mieux refléter la nature de l'algorithme de synthèse utilisé.

## Changements effectués

### Fichiers renommés

Les fichiers ont déjà été renommés et déplacés dans une étape précédente :
- `src/core/synth_fft.c` → `src/synthesis/polyphonic/synth_polyphonic.c`
- `src/core/synth_fft.h` → `src/synthesis/polyphonic/synth_polyphonic.h`

### Symboles renommés

#### Fonctions

- `synth_fftMode_init()` → `synth_polyphonicMode_init()`
- `synth_fftMode_process()` → `synth_polyphonicMode_process()`
- `synth_fftMode_thread_func()` → `synth_polyphonicMode_thread_func()`
- `synth_fft_note_on()` → `synth_polyphonic_note_on()`
- `synth_fft_note_off()` → `synth_polyphonic_note_off()`
- `synth_fft_set_volume_adsr_attack()` → `synth_polyphonic_set_volume_adsr_attack()`
- `synth_fft_set_volume_adsr_decay()` → `synth_polyphonic_set_volume_adsr_decay()`
- `synth_fft_set_volume_adsr_sustain()` → `synth_polyphonic_set_volume_adsr_sustain()`
- `synth_fft_set_volume_adsr_release()` → `synth_polyphonic_set_volume_adsr_release()`
- `synth_fft_set_vibrato_rate()` → `synth_polyphonic_set_vibrato_rate()`
- `synth_fft_set_vibrato_depth()` → `synth_polyphonic_set_vibrato_depth()`

#### Variables et structures

- `fft_audio_buffers` → `polyphonic_audio_buffers`
- `fft_current_buffer_index` → `polyphonic_current_buffer_index`
- `fft_buffer_index_mutex` → `polyphonic_buffer_index_mutex`
- `fft_context` → `polyphonic_context`
- `enable_fft_synth` → `enable_polyphonic_synth`
- `fft_thread_created` → `polyphonic_thread_created`

#### Constantes et définitions

- `FORCE_DISABLE_FFT` → `FORCE_DISABLE_POLYPHONIC`
- `AUTO_DISABLE_FFT_WITHOUT_MIDI` → `AUTO_DISABLE_POLYPHONIC_WITHOUT_MIDI`
- `FFT_PRINT_INTERVAL` → `POLYPHONIC_PRINT_INTERVAL`

## Utilisation

### Avant

```c
// Initialisation
synth_fftMode_init();

// Traitement audio
synth_fftMode_process(audioBuffer, bufferSize);

// Gestion MIDI
synth_fft_note_on(noteNumber, velocity);
synth_fft_note_off(noteNumber);
```

### Après

```c
// Initialisation
synth_polyphonicMode_init();

// Traitement audio
synth_polyphonicMode_process(audioBuffer, bufferSize);

// Gestion MIDI
synth_polyphonic_note_on(noteNumber, velocity);
synth_polyphonic_note_off(noteNumber);
```

## Compatibilité

Cette refactorisation est purement nominative et ne change pas le comportement du code. Aucune modification n'est nécessaire dans votre code client si vous utilisez les nouvelles API.

## Raison du changement

Le terme "FFT" (Fast Fourier Transform) ne reflétait pas correctement l'algorithme utilisé, qui est en réalité une synthèse polyphonique. Ce changement de nom clarifie l'intention et le fonctionnement du code.

## Notes supplémentaires

- Les API mathématiques liées à la véritable FFT (comme celles de la bibliothèque KissFFT) n'ont pas été modifiées.
- Les commentaires et la documentation ont été mis à jour pour refléter la terminologie correcte.
