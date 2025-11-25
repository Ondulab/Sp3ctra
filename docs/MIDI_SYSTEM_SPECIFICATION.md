# CAHIER DES CHARGES - Système MIDI Unifié (Unified MIDI System)

**Version:** 1.0  
**Date:** 30/10/2025  
**Auteur:** Sp3ctra Team  
**Statut:** Spécification initiale

---

## 1. CONTEXTE ET OBJECTIF

### 1.1 Problématique actuelle

Le code MIDI actuel (`midi_controller.cpp`) présente plusieurs limitations :
- **Mappings hard-codés** : Les CC sont définis dans le code C++
- **Conflits de numérotation** : Plusieurs paramètres utilisent les mêmes CC
- **Difficile à maintenir** : Ajouter un nouveau contrôle nécessite de modifier le code
- **Pas de flexibilité** : Impossible de reconfigurer sans recompiler
- **Dispersion** : Les contrôles sont éparpillés dans le code

### 1.2 Solution proposée : Système MIDI Unifié

Un système centralisé et configurable permettant :
- **Mapping externe** : Configuration via fichiers `.ini`
- **Flexibilité totale** : Support de n'importe quel contrôleur MIDI
- **Pas de conflits** : Validation automatique des mappings
- **Extensibilité** : Ajout facile de nouveaux paramètres
- **Callbacks centralisés** : Architecture propre et maintenable

---

## 2. ARCHITECTURE DU SYSTÈME

### 2.1 Vue d'ensemble

```
┌─────────────────────────────────────────────────────────────┐
│                    MIDI Hardware                             │
│              (Launchkey, nanoKONTROL2, etc.)                │
└────────────────────┬────────────────────────────────────────┘
                     │ MIDI Messages (CC, Notes, etc.)
                     ↓
┌─────────────────────────────────────────────────────────────┐
│              midi_controller.cpp (RtMidi)                    │
│              - Détection hardware                            │
│              - Réception messages MIDI                       │
└────────────────────┬────────────────────────────────────────┘
                     │ Raw MIDI data
                     ↓
┌─────────────────────────────────────────────────────────────┐
│           midi_mapping.c (NOUVEAU MODULE)                    │
│           - Chargement config (midi_mapping.ini)             │
│           - Validation mappings                              │
│           - Dispatch vers callbacks                          │
└────────────────────┬────────────────────────────────────────┘
                     │ Paramètre + Valeur normalisée
                     ↓
┌─────────────────────────────────────────────────────────────┐
│         midi_callbacks.c (NOUVEAU MODULE)                    │
│         - Callbacks par catégorie :                          │
│           * Audio global (reverb, EQ, volume)                │
│           * Synthèses (additive, polyphonic)                 │
│           * Séquenceur (players, global)                     │
│           * Système (freeze, resume)                         │
└────────────────────┬────────────────────────────────────────┘
                     │ Actions sur modules
                     ↓
┌─────────────────────────────────────────────────────────────┐
│              Modules cibles                                  │
│  ┌─────────────┐ ┌──────────────┐ ┌──────────────┐        │
│  │AudioSystem  │ │  Syntheses   │ │  Sequencer   │        │
│  │(reverb, EQ) │ │(add, poly)   │ │  (players)   │        │
│  └─────────────┘ └──────────────┘ └──────────────┘        │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Modules à créer

```
src/communication/midi/
├── midi_controller.cpp/h      (EXISTANT - interface hardware RtMidi)
├── midi_mapping.c/h           (NOUVEAU - système de mapping)
├── midi_callbacks.c/h         (NOUVEAU - callbacks centralisés)
└── midi_parameters.c/h        (NOUVEAU - définitions paramètres)
```

---

## 3. INVENTAIRE COMPLET DES PARAMÈTRES MIDI

### 3.1 Audio Global

| Paramètre | Description | Range | Default | Unité |
|-----------|-------------|-------|---------|-------|
| `master_volume` | Volume master de sortie | [0.0, 1.0] | 1.0 | normalized |
| `reverb_mix` | Dry/Wet mix réverbération | [0.0, 1.0] | 0.3 | normalized |
| `reverb_size` | Taille de la pièce | [0.0, 1.0] | 0.5 | normalized |
| `reverb_damp` | Amortissement haute fréquence | [0.0, 1.0] | 0.5 | normalized |
| `reverb_width` | Largeur stéréo | [0.0, 1.0] | 1.0 | normalized |
| `eq_low_gain` | Gain basses fréquences | [-24.0, 24.0] | 0.0 | dB |
| `eq_mid_gain` | Gain moyennes fréquences | [-24.0, 24.0] | 0.0 | dB |
| `eq_high_gain` | Gain hautes fréquences | [-24.0, 24.0] | 0.0 | dB |
| `eq_mid_freq` | Fréquence centrale médiums | [250, 5000] | 1000 | Hz |

### 3.2 Synthèse LuxStral

| Paramètre | Description | Range | Default | Unité |
|-----------|-------------|-------|---------|-------|
| `synth_luxstral_volume` | Niveau de mix | [0.0, 1.0] | 1.0 | normalized |
| `synth_luxstral_reverb_send` | Envoi vers réverb | [0.0, 1.0] | 0.2 | normalized |

### 3.3 Synthèse Polyphonique

| Paramètre | Description | Range | Default | Unité |
|-----------|-------------|-------|---------|-------|
| `synth_luxsynth_volume` | Niveau de mix | [0.0, 1.0] | 0.5 | normalized |
| `synth_luxsynth_reverb_send` | Envoi vers réverb | [0.0, 1.0] | 0.3 | normalized |
| `synth_luxsynth_lfo_vibrato` | Vitesse LFO vibrato | [0.1, 10.0] | 5.0 | Hz |
| `synth_luxsynth_env_attack` | Temps d'attaque enveloppe | [0.02, 2.0] | 0.1 | seconds |
| `synth_luxsynth_env_decay` | Temps de decay enveloppe | [0.02, 2.0] | 0.2 | seconds |
| `synth_luxsynth_env_release` | Temps de release enveloppe | [0.02, 2.0] | 0.3 | seconds |
| `synth_luxsynth_note_on` | Déclenchement note | MIDI Note | - | note number |
| `synth_luxsynth_note_off` | Relâchement note | MIDI Note | - | note number |

### 3.4 Séquenceur - Player 1 à 5

Pour chaque player (répété 5 fois) :

| Paramètre | Description | Range | Default | Unité |
|-----------|-------------|-------|---------|-------|
| `sequencer_pN_record_toggle` | Toggle enregistrement | [0, 127] | - | button |
| `sequencer_pN_play_stop` | Toggle lecture/pause (PLAYING ↔ STOPPED) | [0, 127] | - | button |
| `sequencer_pN_mute_toggle` | Toggle mute (PLAYING ↔ MUTED) | [0, 127] | - | button |
| `sequencer_pN_speed` | Vitesse de lecture | [0.1, 10.0] | 1.0 | multiplier |
| `sequencer_pN_blend_level` | Niveau dans le mix | [0.0, 1.0] | 1.0 | normalized |
| `sequencer_pN_offset` | Décalage temporel | [0.0, 1.0] | 0.0 | normalized |
| `sequencer_pN_attack` | ADSR attack | [0, 5000] | 10 | milliseconds |
| `sequencer_pN_release` | ADSR release | [0, 10000] | 100 | milliseconds |
| `sequencer_pN_loop_mode` | Mode de boucle (0=SIMPLE, 1=PINGPONG, 2=ONESHOT) | [0, 2] | 0 | enum |
| `sequencer_pN_playback_direction` | Direction lecture (0=FORWARD, 1=REVERSE) | [0, 1] | 0 | enum |

(N = 1 à 5)

### 3.5 Séquenceur - Global

| Paramètre | Description | Range | Default | Unité |
|-----------|-------------|-------|---------|-------|
| `sequencer_live_mix_level` | Niveau du live | [0.0, 1.0] | 0.5 | normalized |
| `sequencer_blend_mode` | Mode de fusion | [0, 3] | 0 | enum |
| `sequencer_master_tempo` | BPM manuel | [60, 240] | 120 | BPM |
| `sequencer_quantize_res` | Résolution quantif | [0, 3] | 1 | enum |

### 3.6 Système

| Paramètre | Description | Range | Default | Unité |
|-----------|-------------|-------|---------|-------|
| `system_freeze` | Freeze synth data | [0, 127] | - | button |
| `system_resume` | Resume avec fade | [0, 127] | - | button |

---

## 4. FICHIERS DE CONFIGURATION

### 4.1 Structure des fichiers

```
config/
├── midi_mapping.ini               # Affectations CC (utilisateur)
├── midi_parameters_defaults.ini   # Ranges + defaults (système)
└── midi_mapping_examples/         # Exemples prêts à l'emploi
    ├── launchkey_mini.ini
    ├── nanokontrol2.ini
    └── empty_template.ini
```

### 4.2 Format `midi_mapping.ini`

**Principe** : Affectations vides par défaut, utilisateur les remplit selon son contrôleur.

```ini
# ============================================================================
# MIDI MAPPING CONFIGURATION
# ============================================================================
# Format: parameter_name=TYPE:NUMBER
# Types: CC (Control Change), NOTE (Note On/Off), PITCHBEND
# Use "none" to disable a mapping
# Examples:
#   master_volume=CC:1        # CC1 controls master volume
#   note_on=NOTE:*            # All MIDI notes trigger note on
#   freeze=CC:105             # CC105 triggers freeze
# ============================================================================

[MIDI_DEVICE]
device_name=auto              # "auto" or specific device name
device_id=auto                # "auto" or specific device ID

# ============================================================================
# AUDIO GLOBAL
# ============================================================================

[AUDIO_GLOBAL]
master_volume=none
reverb_mix=none
reverb_size=none
reverb_damp=none
reverb_width=none
eq_low_gain=none
eq_mid_gain=none
eq_high_gain=none
eq_mid_freq=none

# ============================================================================
# SYNTHESIS LUXSTRAL
# ============================================================================

[SYNTH_LUXSTRAL]
volume=none
reverb_send=none

# ============================================================================
# SYNTHESIS LUXSYNTH
# ============================================================================

[SYNTH_LUXSYNTH]
volume=none
reverb_send=none
lfo_vibrato=none
env_attack=none
env_decay=none
env_release=none
note_on=none
note_off=none

# ============================================================================
# SEQUENCER - PLAYER 1
# ============================================================================

[SEQUENCER_PLAYER_1]
record_toggle=none
play_stop=none
speed=none
blend_level=none
offset=none
attack=none
release=none
loop_mode=none

# ============================================================================
# SEQUENCER - PLAYER 2
# ============================================================================

[SEQUENCER_PLAYER_2]
record_toggle=none
play_stop=none
speed=none
blend_level=none
offset=none
attack=none
release=none
loop_mode=none

# ============================================================================
# SEQUENCER - PLAYER 3
# ============================================================================

[SEQUENCER_PLAYER_3]
record_toggle=none
play_stop=none
speed=none
blend_level=none
offset=none
attack=none
release=none
loop_mode=none

# ============================================================================
# SEQUENCER - PLAYER 4
# ============================================================================

[SEQUENCER_PLAYER_4]
record_toggle=none
play_stop=none
speed=none
blend_level=none
offset=none
attack=none
release=none
loop_mode=none

# ============================================================================
# SEQUENCER - PLAYER 5
# ============================================================================

[SEQUENCER_PLAYER_5]
record_toggle=none
play_stop=none
speed=none
blend_level=none
offset=none
attack=none
release=none
loop_mode=none

# ============================================================================
# SEQUENCER - GLOBAL
# ============================================================================

[SEQUENCER_GLOBAL]
live_mix_level=none
blend_mode=none
master_tempo=none
quantize_res=none

# ============================================================================
# SYSTEM
# ============================================================================

[SYSTEM]
freeze=none
resume=none
```

### 4.3 Format `midi_parameters_defaults.ini`

**Principe** : Définitions système des ranges, defaults, scaling.

```ini
# ============================================================================
# MIDI PARAMETERS DEFAULTS & RANGES
# ============================================================================
# Defines technical specifications for all MIDI-controllable parameters
# This file should NOT be modified by end users
# ============================================================================

# ----------------------------------------------------------------------------
# AUDIO GLOBAL
# ----------------------------------------------------------------------------

[AUDIO_GLOBAL.master_volume]
default=1.0
min=0.0
max=1.0
scaling=linear
unit=normalized
description=Master output volume

[AUDIO_GLOBAL.reverb_mix]
default=0.3
min=0.0
max=1.0
scaling=linear
unit=normalized
description=Reverb dry/wet mix

[AUDIO_GLOBAL.reverb_size]
default=0.5
min=0.0
max=1.0
scaling=linear
unit=normalized
description=Reverb room size

[AUDIO_GLOBAL.reverb_damp]
default=0.5
min=0.0
max=1.0
scaling=linear
unit=normalized
description=Reverb high frequency damping

[AUDIO_GLOBAL.reverb_width]
default=1.0
min=0.0
max=1.0
scaling=linear
unit=normalized
description=Reverb stereo width

[AUDIO_GLOBAL.eq_low_gain]
default=0.0
min=-24.0
max=24.0
scaling=linear
unit=dB
description=EQ low frequency gain

[AUDIO_GLOBAL.eq_mid_gain]
default=0.0
min=-24.0
max=24.0
scaling=linear
unit=dB
description=EQ mid frequency gain

[AUDIO_GLOBAL.eq_high_gain]
default=0.0
min=-24.0
max=24.0
scaling=linear
unit=dB
description=EQ high frequency gain

[AUDIO_GLOBAL.eq_mid_freq]
default=1000.0
min=250.0
max=5000.0
scaling=logarithmic
unit=Hz
description=EQ mid frequency center

# ----------------------------------------------------------------------------
# SYNTHESIS LUXSTRAL
# ----------------------------------------------------------------------------

[SYNTH_LUXSTRAL.volume]
default=1.0
min=0.0
max=1.0
scaling=linear
unit=normalized
description=LuxStral synthesis mix level

[SYNTH_LUXSTRAL.reverb_send]
default=0.2
min=0.0
max=1.0
scaling=linear
unit=normalized
description=LuxStral synthesis reverb send amount

# ----------------------------------------------------------------------------
# SYNTHESIS LUXSYNTH
# ----------------------------------------------------------------------------

[SYNTH_LUXSYNTH.volume]
default=0.5
min=0.0
max=1.0
scaling=linear
unit=normalized
description=LuxSynth synthesis mix level

[SYNTH_LUXSYNTH.reverb_send]
default=0.3
min=0.0
max=1.0
scaling=linear
unit=normalized
description=LuxSynth synthesis reverb send amount

[SYNTH_LUXSYNTH.lfo_vibrato]
default=5.0
min=0.1
max=10.0
scaling=exponential
unit=Hz
description=LFO vibrato rate

[SYNTH_LUXSYNTH.env_attack]
default=0.1
min=0.02
max=2.0
scaling=exponential
unit=seconds
description=Volume envelope attack time

[SYNTH_LUXSYNTH.env_decay]
default=0.2
min=0.02
max=2.0
scaling=exponential
unit=seconds
description=Volume envelope decay time

[SYNTH_LUXSYNTH.env_release]
default=0.3
min=0.02
max=2.0
scaling=exponential
unit=seconds
description=Volume envelope release time

# ----------------------------------------------------------------------------
# SEQUENCER PLAYERS (template for player 1, repeat for 2-5)
# ----------------------------------------------------------------------------

[SEQUENCER_PLAYER_1.speed]
default=1.0
min=0.1
max=10.0
scaling=exponential
unit=multiplier
description=Playback speed multiplier

[SEQUENCER_PLAYER_1.blend_level]
default=1.0
min=0.0
max=1.0
scaling=linear
unit=normalized
description=Player contribution to mix

[SEQUENCER_PLAYER_1.offset]
default=0.0
min=0.0
max=1.0
scaling=linear
unit=normalized
description=Playback start offset (0-100% of sequence)

[SEQUENCER_PLAYER_1.attack]
default=10.0
min=0.0
max=5000.0
scaling=exponential
unit=milliseconds
description=ADSR attack time

[SEQUENCER_PLAYER_1.release]
default=100.0
min=0.0
max=10000.0
scaling=exponential
unit=milliseconds
description=ADSR release time

[SEQUENCER_PLAYER_1.loop_mode]
default=0
min=0
max=2
scaling=discrete
unit=enum
description=Loop mode (0=simple, 1=pingpong, 2=oneshot)
values=SIMPLE,PINGPONG,ONESHOT

# (Repeat for PLAYER_2, PLAYER_3, PLAYER_4, PLAYER_5...)

# ----------------------------------------------------------------------------
# SEQUENCER GLOBAL
# ----------------------------------------------------------------------------

[SEQUENCER_GLOBAL.live_mix_level]
default=0.5
min=0.0
max=1.0
scaling=linear
unit=normalized
description=Live input mix level

[SEQUENCER_GLOBAL.blend_mode]
default=0
min=0
max=3
scaling=discrete
unit=enum
description=Blend mode (0=mix, 1=crossfade, 2=overlay, 3=mask)
values=MIX,CROSSFADE,OVERLAY,MASK

[SEQUENCER_GLOBAL.master_tempo]
default=120.0
min=60.0
max=240.0
scaling=linear
unit=BPM
description=Manual BPM (fallback if no MIDI clock)

[SEQUENCER_GLOBAL.quantize_res]
default=1
min=0
max=3
scaling=discrete
unit=enum
description=Quantization resolution
values=QUARTER,EIGHTH,SIXTEENTH,BAR

# ----------------------------------------------------------------------------
# SYSTEM
# ----------------------------------------------------------------------------

# Note: Buttons like freeze/resume don't have ranges, only trigger values
[SYSTEM.freeze]
type=button
trigger_threshold=64
description=Freeze synth data

[SYSTEM.resume]
type=button
trigger_threshold=64
description=Resume synth data with fade
```

---

## 5. API DU SYSTÈME DE MAPPING

### 5.1 Structures de données

**`midi_mapping.h`** :

```c
#ifndef MIDI_MAPPING_H
#define MIDI_MAPPING_H

#include <stdint.h>

/* MIDI message types */
typedef enum {
    MIDI_MSG_NONE = 0,
    MIDI_MSG_CC,           // Control Change
    MIDI_MSG_NOTE_ON,      // Note On
    MIDI_MSG_NOTE_OFF,     // Note Off
    MIDI_MSG_PITCHBEND,    // Pitch Bend
    MIDI_MSG_AFTERTOUCH    // Channel Aftertouch
} MidiMessageType;

/* MIDI control specification */
typedef struct {
    MidiMessageType type;
    int channel;           // MIDI channel (0-15, or -1 for any)
    int number;            // CC number, note number, etc.
} MidiControl;

/* Parameter value with metadata */
typedef struct {
    float value;           // Normalized value [0.0, 1.0]
    float raw_value;       // Raw value in parameter's native unit
    const char *param_name;
    int is_button;         // 1 if button/trigger, 0 if continuous
} MidiParameterValue;

/* Callback function type */
typedef void (*MidiCallback)(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/* Initialization and cleanup */
int midi_mapping_init(void);
void midi_mapping_cleanup(void);

/* Load configuration files */
int midi_mapping_load_mappings(const char *config_file);
int midi_mapping_load_parameters(const char *params_file);

/* Register callbacks for specific parameters */
int midi_mapping_register_callback(
    const char *param_name,
    MidiCallback callback,
    void *user_data
);

/* Dispatch incoming MIDI messages */
void midi_mapping_dispatch(
    MidiMessageType type,
    int channel,
    int number,
    int value
);

/* Query current parameter values */
float midi_mapping_get_parameter_value(const char *param_name);
int midi_mapping_set_parameter_value(const char *param_name, float value);

/* Validation and diagnostics */
int midi_mapping_validate(void);
void midi_mapping_print_status(void);
int midi_mapping_has_conflicts(void);

#endif /* MIDI_MAPPING_H */
```

### 5.2 Exemple d'utilisation

```c
// Dans main.c, au démarrage

// 1. Initialiser le système MIDI
midi_mapping_init();

// 2. Charger les définitions de paramètres (système)
if (midi_mapping_load_parameters("midi_parameters_defaults.ini") != 0) {
    fprintf(stderr, "ERROR: Failed to load MIDI parameters\n");
    return EXIT_FAILURE;
}

// 3. Charger les mappings utilisateur
if (midi_mapping_load_mappings("midi_mapping.ini") != 0) {
    fprintf(stderr, "WARNING: Failed to load MIDI mappings, using defaults\n");
}

// 4. Valider la configuration
if (midi_mapping_has_conflicts()) {
    fprintf(stderr, "WARNING: MIDI mapping conflicts detected!\n");
    midi_mapping_print_status();
}

// 5. Enregistrer les callbacks (fait automatiquement par chaque module)
// Par exemple, dans audio_system.c:
midi_mapping_register_callback("master_volume", audio_callback_master_volume, audio_system);
midi_mapping_register_callback("reverb_mix", audio_callback_reverb_mix, audio_system);

// Dans sequencer.c:
image_sequencer_register_midi_callbacks(sequencer);  // Enregistre tous les callbacks du séquenceur
```

---

## 6. MODULE DE CALLBACKS

### 6.1 Organisation des callbacks

**`midi_callbacks.h`** :

```c
#ifndef MIDI_CALLBACKS_H
#define MIDI_CALLBACKS_H

#include "midi_mapping.h"

/* ============================================================================
 * AUDIO GLOBAL CALLBACKS
 * ============================================================================ */

void midi_cb_audio_master_volume(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_reverb_mix(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_reverb_size(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_reverb_damp(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_reverb_width(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_eq_low_gain(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_eq_mid_gain(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_eq_high_gain(const MidiParameterValue *param, void *user_data);
void midi_cb_audio_eq_mid_freq(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SYNTHESIS LUXSTRAL CALLBACKS
 * ============================================================================ */

void midi_cb_synth_luxstral_volume(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxstral_reverb_send(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SYNTHESIS LUXSYNTH CALLBACKS
 * ============================================================================ */

void midi_cb_synth_luxsynth_volume(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_reverb_send(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_lfo_vibrato(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_env_attack(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_env_decay(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_env_release(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_note_on(const MidiParameterValue *param, void *user_data);
void midi_cb_synth_luxsynth_note_off(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SEQUENCER PLAYER CALLBACKS (generic, player ID in user_data)
 * ============================================================================ */

void midi_cb_sequencer_player_record_toggle(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_play_stop(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_speed(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_blend_level(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_offset(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_attack(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_release(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_player_loop_mode(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SEQUENCER GLOBAL CALLBACKS
 * ============================================================================ */

void midi_cb_sequencer_live_mix_level(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_blend_mode(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_master_tempo(const MidiParameterValue *param, void *user_data);
void midi_cb_sequencer_quantize_res(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * SYSTEM CALLBACKS
 * ============================================================================ */

void midi_cb_system_freeze(const MidiParameterValue *param, void *user_data);
void midi_cb_system_resume(const MidiParameterValue *param, void *user_data);

/* ============================================================================
 * REGISTRATION HELPERS
 * ============================================================================ */

/* Register all callbacks for a specific module */
void midi_callbacks_register_audio(void);
void midi_callbacks_register_synth_luxstral(void);
void midi_callbacks_register_synth_luxsynth(void);
void midi_callbacks_register_sequencer(void *sequencer_instance);
void midi_callbacks_register_system(void);

#endif /* MIDI_CALLBACKS_H */
```

---

## 7. MIGRATION DU CODE EXISTANT

### 7.1 Plan de migration

#### Phase 1 : Création infrastructure (Semaine 1)
- [ ] Créer `midi_mapping.c/h` avec API complète
- [ ] Créer `midi_parameters.c/h` pour parser configs
- [ ] Créer `midi_callbacks.c/h` avec stubs
- [ ] Créer fichiers de configuration

#### Phase 2 : Migration audio global (Semaine 2)
- [ ] Migrer contrôles volume, reverb, EQ
- [ ] Tester avec contrôleur existant
- [ ] Valider fonctionnement identique

#### Phase 3 : Migration synthèses (Semaine 3)
- [ ] Migrer contrôles synthèse additive
- [ ] Migrer contrôles synthèse polyphonique
- [ ] Migrer notes MIDI
- [ ] Tests de performance

#### Phase 4 : Migration système (Semaine 4)
- [ ] Migrer freeze/resume
- [ ] Cleanup ancien code `midi_controller.cpp`
- [ ] Documentation

#### Phase 5 : Ajout séquenceur (Semaine 5)
- [ ] Ajouter callbacks séquenceur
- [ ] Intégration complète
- [ ] Tests finaux

### 7.2 Rétrocompatibilité

Durant la migration, garder l'ancien code fonctionnel :

```c
// Dans midi_controller.cpp
void MidiController::processMidiMessage(...) {
    // Nouveau système (prioritaire)
    if (g_midi_mapping_enabled) {
        midi_mapping_dispatch(messageType, channel, number, value);
        return;
    }
    
    // Ancien système (fallback)
