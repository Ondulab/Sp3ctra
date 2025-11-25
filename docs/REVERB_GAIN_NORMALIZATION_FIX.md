# Correction du gain excessif de réverbération

## Problème identifié

Lorsque `reverb_send` était réglé à 100%, le signal audio présentait une augmentation de gain, même avec `reverbMix` à 0%. 

### Cause racine

Le problème se trouvait dans `processReverbOptimized()` dans `audio_rtaudio.cpp`:

```cpp
// CODE BUGUÉ
if (!reverbEnabled || reverbMix <= 0.0f) {
    outputL = inputL;  // ❌ ERREUR: retourne l'input au lieu de 0!
    outputR = inputR;  // ❌ ERREUR: retourne l'input au lieu de 0!
    return;
}
```

### Flux du signal bugué

```
reverb_send = 100% + reverbMix = 0%

1. reverb_input = source × 1.0
2. processReverbOptimized(reverb_input, ..., reverb_left, reverb_right)
3. Comme reverbMix = 0, la fonction retournait: reverb_left = reverb_input
4. mixed = dry_sample + reverb_left = signal + signal = DOUBLE GAIN!
```

## Solution implémentée

### 1. Correction dans processReverbOptimized()

Quand `reverbMix = 0%`, la fonction doit retourner **silence (0.0f)**, pas l'input:

```cpp
// CODE CORRIGÉ
if (!reverbEnabled || reverbMix <= 0.0f) {
    outputL = 0.0f;  // ✓ Silence, pas de reverb
    outputR = 0.0f;  // ✓ Silence, pas de reverb
    return;
}
```

### 2. Normalisation additionnelle dans ZitaRev1

Pour prévenir l'accumulation de gain dans les boucles de feedback, ajout d'un facteur de compensation en sortie de `ZitaRev1::process()`:

```cpp
const float OUTPUT_COMPENSATION = 0.25f;  // Compense les 8 lignes de délai
outputL[i] = (centerComponent + sideComponent) * OUTPUT_COMPENSATION;
outputR[i] = (centerComponent - sideComponent) * OUTPUT_COMPENSATION;
```

### 3. Suppression du code legacy

Suppression de la fonction `processReverb()` non utilisée:
- Déclaration retirée de `audio_rtaudio.h`
- Implémentation retirée de `audio_rtaudio.cpp`
- Seule `processReverbOptimized()` est conservée (utilisée dans le callback RT)

## Flux du signal de réverbération

### Architecture complète

```
┌─────────────────────────────────────────────────────────────────────┐
│                    AUDIO CALLBACK (RT-safe)                         │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │                           │
                    ▼                           ▼
        ┌───────────────────┐       ┌───────────────────┐
        │  Source LuxStral  │       │ Source LuxSynth │
        │   (stereo L/R)    │       │      (mono)       │
        └─────────┬─────────┘       └─────────┬─────────┘
                  │                           │
                  │ × mix_level               │ × mix_level
                  ▼                           ▼
        ┌───────────────────┐       ┌───────────────────┐
        │  dry_sample_L/R   │       │  dry_sample_L/R   │
        └─────────┬─────────┘       └─────────┬─────────┘
                  │                           │
                  └──────────┬────────────────┘
                             │
                ┌────────────┴────────────┐
                │                         │
                ▼                         ▼
    ┌─────────────────────┐   ┌─────────────────────┐
    │   DRY PATH          │   │   REVERB SEND       │
    │                     │   │                     │
    │  dry_sample_L/R     │   │  reverb_input_L/R   │
    │                     │   │  = source × send    │
    └──────────┬──────────┘   └──────────┬──────────┘
               │                         │
               │                         ▼
               │              ┌─────────────────────┐
               │              │  ZitaRev1::process  │
               │              │                     │
               │              │  8 delay lines      │
               │              │  feedback = 0.9     │
               │              │  gain0 ≈ 1.0        │
               │              │                     │
               │              │  OUTPUT_COMP = 0.25 │
               │              └──────────┬──────────┘
               │                         │
               │                         ▼
               │              ┌─────────────────────┐
               │              │  reverb_L/R × mix   │
               │              └──────────┬──────────┘
               │                         │
               └──────────┬──────────────┘
                          │
                          ▼
                ┌─────────────────────┐
                │  mixed = dry + wet  │
                └──────────┬──────────┘
                           │
                           ▼
                ┌─────────────────────┐
                │   EQ (3-band)       │
                └──────────┬──────────┘
                           │
                           ▼
                ┌─────────────────────┐
                │  × masterVolume     │
                │  + limiting         │
                └──────────┬──────────┘
                           │
                           ▼
                    ┌─────────────┐
                    │   OUTPUT    │
                    └─────────────┘
```

### Paramètres de contrôle

1. **reverb_send** (0.0 - 1.0)
   - Contrôle la quantité de signal envoyée à la réverbération
   - **Indépendant** du mix level
   - Permet d'avoir de la réverb même si le canal est muté dans le mix

2. **reverbMix** (0.0 - 1.0)
   - Contrôle le mélange dry/wet **en sortie** de ZitaRev1
   - 0% = pas de réverb audible (signal wet multiplié par 0)
   - 100% = réverb maximale (signal wet multiplié par 1)

3. **mix_level** (0.0 - 1.0)
   - Contrôle le niveau du canal dans le mix final
   - **Indépendant** du reverb send

### Comportement corrigé

| reverb_send | reverbMix | Résultat                                    |
|-------------|-----------|---------------------------------------------|
| 0%          | 0%        | Pas de réverb (aucun signal envoyé)         |
| 0%          | 100%      | Pas de réverb (aucun signal envoyé)         |
| 100%        | 0%        | **Pas de réverb audible** (wet × 0)         |
| 100%        | 50%       | Réverb à 50% du signal                      |
| 100%        | 100%      | Réverb maximale, **gain normalisé**         |

## Nettoyage automatique des buffers de réverbération

### Problème de "ghost reverb"

Quand tous les `reverb_send` sont coupés brutalement à 0%, les 8 lignes de délai de ZitaRev1 conservent leur contenu avec un feedback de 0.9. Cette réverbération résiduelle peut prendre plusieurs secondes à disparaître naturellement.

### Solution implémentée

Détection automatique dans le callback audio:

```cpp
// Détecter quand TOUS les reverb sends passent à zéro
static bool all_sends_zero_last_frame = false;
bool all_sends_zero = (cached_reverb_send_luxstral <= 0.01f &&
                       cached_reverb_send_luxsynth <= 0.01f &&
                       cached_reverb_send_luxwave <= 0.01f);

// Si transition de "au moins un send actif" vers "tous sends à 0"
// alors vider immédiatement les buffers de reverb
if (all_sends_zero && !all_sends_zero_last_frame && reverbEnabled) {
    zitaRev.clear();  // Vide les 8 lignes de délai
}
all_sends_zero_last_frame = all_sends_zero;
```

### Comportement

- **Sends actifs → Sends à 0**: Les buffers sont vidés instantanément
- **Sends à 0 → Sends actifs**: La réverb redémarre proprement sans "ghost reverb"
- **Transition progressive**: Si au moins un send reste > 0, pas de nettoyage

## Tests recommandés

1. **Test de gain unity**
   ```
   reverb_send = 100%
   reverbMix = 0%
   → Le signal de sortie doit avoir le même niveau que l'entrée
   ```

2. **Test de réverb maximale**
   ```
   reverb_send = 100%
   reverbMix = 100%
   → La réverb doit être audible sans augmentation excessive du gain
   ```

3. **Test d'indépendance**
   ```
   mix_level = 0% (canal muté)
   reverb_send = 100%
   reverbMix = 100%
   → La réverb doit être audible (send indépendant du mix)
   ```

4. **Test de nettoyage automatique**
   ```
   1. reverb_send = 100%, reverbMix = 100% (réverb active)
   2. Couper brutalement tous les sends à 0%
   3. Remettre un send à 100%
   → Pas de "ghost reverb", la réverb redémarre proprement
   ```

## Fichiers modifiés

- `src/audio/effects/ZitaRev1.cpp` - Ajout de OUTPUT_COMPENSATION
- `src/audio/rtaudio/audio_rtaudio.cpp` - Suppression de processReverb()
- `src/audio/rtaudio/audio_rtaudio.h` - Suppression de la déclaration

## Notes techniques

- Le facteur de compensation (0.25) a été déterminé empiriquement
- Il peut nécessiter un ajustement selon les paramètres de réverbération
- La normalisation est appliquée **après** le traitement ZitaRev1 mais **avant** le mélange dry/wet
- Cette approche préserve le caractère de la réverbération tout en évitant l'amplification excessive
