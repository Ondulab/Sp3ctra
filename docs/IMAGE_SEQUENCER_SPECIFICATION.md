# CAHIER DES CHARGES - Séquenceur d'Images (Image Sequencer)

**Version:** 2.0  
**Date:** 30/10/2025  
**Auteur:** Sp3ctra Team  
**Statut:** Spécification révisée  
**Dépendances:** MIDI_SYSTEM_SPECIFICATION.md (pour les contrôles MIDI)

---

## 1. CONTEXTE ET OBJECTIF

### 1.1 Position dans l'architecture

Le module s'insère entre `image_preprocessor` et les consommateurs (synthèses/DMX/affichage) :

```
[UDP Thread] → [Image Preprocessor] → [IMAGE SEQUENCER] → [Synthèses/DMX/Display]
                PreprocessedImageData     NEW MODULE         Ligne unifiée
```

### 1.2 Fonction principale

Enregistrer, manipuler temporellement et mixer des séquences de lignes d'images prétraitées avant de les transmettre aux consommateurs.

### 1.3 Valeur ajoutée

- **Performance créative** : Enregistrement et manipulation en temps réel de séquences visuelles
- **Contrôle temporel** : Lecture à vitesses variables, boucles, synchronisation
- **Expressivité** : Fusion de multiples séquences avec enveloppes ADSR
- **Intégration MIDI** : Contrôle via système MIDI unifié (voir MIDI_SYSTEM_SPECIFICATION.md)

---

## 2. SPÉCIFICATIONS TECHNIQUES

### 2.1 Données d'entrée/sortie

#### Entrée : `PreprocessedImageData`
- **Résolution** : **3456 pixels** exactement (400 DPI)
- **Source** : `CIS_MAX_PIXELS_NB` défini dans `config_instrument.h`
- **Composantes** :
  - `grayscale[3456]` : Données normalisées [0.0, 1.0]
  - `contrast_factor` : Facteur de contraste calculé
  - `stereo` : Données de panoramique stéréo (si activé)
    - `pan_positions[]` : Positions pan par note
    - `left_gains[]`, `right_gains[]` : Gains L/R par note
  - `dmx` : Zones DMX moyennées RGB (si activé)
    - `zone_r[]`, `zone_g[]`, `zone_b[]`
  - `timestamp_us` : Timestamp microseconde

#### Sortie : `PreprocessedImageData` (format identique)
- Résultat du mix de toutes les sources actives (séquences + live)
- Maintien de la compatibilité 100% avec tous les consommateurs existants
- **Garantie** : Format strictement identique à l'entrée

### 2.2 Capacité de stockage

| Paramètre | Valeur | Configuration |
|-----------|--------|---------------|
| Nombre de séquences | 5 | `sequencer_max_sequences=5` |
| Durée max/séquence | 5 secondes | `sequencer_max_duration_s=5.0` |
| Fréquence d'acquisition | 1000 images/s | Fixe (UDP) |
| Mémoire/séquence | ~70-80 MB | 5000 frames × PreprocessedImageData |
| Mémoire totale | ~350-400 MB | Acceptable sur 4-8 Go RAM |

**Calcul mémoire détaillé** :
```c
sizeof(PreprocessedImageData) ≈ 
    grayscale[3456] * 4 bytes           = 13.8 KB
    + stereo.pan_positions[3456] * 4    = 13.8 KB
    + stereo.left_gains[3456] * 4       = 13.8 KB
    + stereo.right_gains[3456] * 4      = 13.8 KB
    + dmx zones (~ 20 spots × 3 bytes)  = ~0.06 KB
    + metadata                           = ~0.5 KB
    ≈ 55 KB par frame

5 secondes @ 1000 fps = 5000 frames
5000 frames × 55 KB ≈ 275 MB par séquence

5 séquences × 275 MB = 1.375 GB
+ Overhead système ~200 MB
Total ≈ 1.6 GB (confortable sur 4-8 GB RAM)
```

**Note** : Les paramètres sont configurables dans `sp3ctra.ini` pour ajustement selon les besoins.

### 2.3 Contraintes temps-réel

#### Performance cible
- **Fréquence** : 1000 fps (1 ms/frame maximum)
- **Latence traitement** : < 100 µs pour le mix
- **CPU usage** : < 50% pour 5 players actifs (Raspberry Pi 5)

#### Contraintes strictes RT
- ❌ **INTERDIT** : Allocations dynamiques (`malloc`, `free`, `realloc`) en chemin critique
- ✅ **REQUIS** : Pré-allocation complète au démarrage (ring buffers statiques)
- ✅ **REQUIS** : Réutilisation des buffers existants
- ✅ **REQUIS** : Thread-safety lock-free ou mutex ultra-léger (< 10 µs)

#### Garanties
- Zéro underrun audio
- Zéro fragmentation mémoire
- Comportement déterministe

---

## 3. FONCTIONNALITÉS DU SÉQUENCEUR

### 3.1 Gestion des séquences (Players)

Chaque **Player** représente une séquence indépendante avec ses paramètres propres.

#### 3.1.1 États d'enregistrement

```
IDLE ──[start_record]──> RECORDING ──[stop_record]──> READY
                                                         │
                                                  [play] │
                                                         ↓
    ┌──────────────────────────── PLAYING ←─────────────┘
    │                                  │
    │ [mute]                     [stop]│
    ↓                                  ↓
  MUTED                            STOPPED
    │                                  │
    │ [unmute]                   [play]│
    └──────────────────────────────────┘
```

| État | Dans le mix ? | Description | ADSR | Usage |
|------|---------------|-------------|------|-------|
| `IDLE` | ❌ NON | Inactif, pas de séquence chargée | N/A | Vide |
| `RECORDING` | ❌ NON | Enregistrement en cours depuis le live | N/A | Capture |
| `READY` | ❌ NON | Séquence enregistrée, prête à être lue | N/A | Standby |
| `PLAYING` | ✅ OUI | Lecture active, avance dans la séquence | Active | Lecture normale |
| `STOPPED` | ✅ OUI | Frame figée, reste dans le mix | Release | Hold/Freeze |
| `MUTED` | ❌ NON | Silencieux, retiré du mix | 0 | Désactivation |

**Transitions importantes** :
- `PLAYING` → `STOPPED` : La position de lecture est figée, mais la frame reste dans le mix avec enveloppe en release
- `PLAYING` → `MUTED` : Le player est complètement retiré du mix (silence)
- `STOPPED` → `PLAYING` : Reprend la lecture depuis la position figée
- `MUTED` → `PLAYING` : Réactive le player et reprend la lecture

#### 3.1.2 Paramètres de lecture

##### Vitesse (Speed)
- **Multiplicateurs discrets** : 0.25×, 0.5×, 1×, 2×, 4×, 8×
- **Contrôle continu** : [0.1, 10.0] (pour contrôle MIDI fin)
- **Interpolation** : Linéaire entre frames pour vitesses fractionnaires

##### Offset temporel
- **Type** : Décalage du point de départ
- **Unités** : Frames ou secondes
- **Range** : [0, durée_séquence]
- **Usage** : Synchronisation précise, effets de phasing

##### Mode de boucle

| Mode | Comportement | Diagramme |
|------|--------------|-----------|
| `LOOP_SIMPLE` | Répétition continue | A→B→A→B→A→B... |
| `LOOP_PINGPONG` | Aller-retour | A→B→A→B→A... |
| `ONESHOT` | Lecture unique puis stop | A→B→[STOP] |

##### Direction de lecture

**Nouveau paramètre** : `playback_direction` contrôle le sens de lecture.

| Direction | Valeur | Description |
|-----------|--------|-------------|
| `FORWARD` | +1 | Lecture normale (A vers B) |
| `REVERSE` | -1 | Lecture inversée (B vers A) |

**Combinaisons possibles** :

| Mode de boucle | Direction | Comportement |
|----------------|-----------|--------------|
| `SIMPLE` | `FORWARD` | A→B→A→B→A→B... |
| `SIMPLE` | `REVERSE` | B→A→B→A→B→A... |
| `PINGPONG` | `FORWARD` | A→B→A→B→A... (démarre en avant) |
| `PINGPONG` | `REVERSE` | B→A→B→A→B... (démarre en arrière) |
| `ONESHOT` | `FORWARD` | A→B→[STOP] |
| `ONESHOT` | `REVERSE` | B→A→[STOP] |

**Note** : En mode PINGPONG, la direction change automatiquement aux limites. Le paramètre `playback_direction` définit uniquement la direction initiale.

#### 3.1.3 Mode de déclenchement

| Mode | Description | Usage |
|------|-------------|-------|
| `MANUAL` | Démarrage manuel via MIDI/code | Performance libre |
| `AUTO` | Démarrage automatique après enregistrement | Workflow rapide |
| `SYNC` | Synchronisé sur MIDI clock (quantifié) | Performance rythmique |

**Quantification (mode SYNC)** :
- 1/4 (quarter note)
- 1/8 (eighth note)
- 1/16 (sixteenth note)
- 1 mesure (bar)

#### 3.1.4 Enveloppe ADSR

Chaque player possède une enveloppe volume pour façonner son apparition/disparition dans le mix.

```
Volume
  1.0 │     ╱────────╲
      │    ╱ Decay    ╲
      │   ╱            ╲
      │  ╱   Sustain    ╲ Release
      │ ╱                ╲
  0.0 │────────────────────╲───
      └─────────────────────────> Time
      Attack    (hold)    Release
```

**Paramètres** :
- **Attack** (A) : Temps de montée (ms) - [0, 5000]
- **Decay** (D) : Temps de descente vers sustain (ms) - [0, 5000]
- **Sustain** (S) : Niveau de maintien - [0.0, 1.0]
- **Release** (R) : Temps de relâchement (ms) - [0, 10000]

**Valeurs par défaut** :
```ini
default_attack_ms=10.0
default_decay_ms=50.0
default_sustain_level=0.8
default_release_ms=100.0
```

### 3.2 Modes de fusion (Blend Modes)

Mix de plusieurs séquences + live (si activé).

#### MIX - Moyenne pondérée
```c
output = (player1 * weight1 + player2 * weight2 + ... + live * live_weight) / total_weight
```
- **Usage** : Mix général, transparence
- **Caractéristique** : Toutes les sources contribuent également

#### CROSSFADE - Transition progressive
```c
output = lerp(sourceA, sourceB, crossfade_amount)
```
- **Usage** : Transitions fluides entre séquences
- **Caractéristique** : Interpolation linéaire

#### OVERLAY - Superposition additive
```c
output = clamp(player1 + player2 + ... + live, 0.0, 1.0)
```
- **Usage** : Effets de cumul, intensité
- **Caractéristique** : Addition avec saturation

#### MASK - Masquage multiplicatif
```c
output = player1 * player2 * ... * live
```
- **Usage** : Effets de gate, filtrage
- **Caractéristique** : Multiplication (effet masque)

#### Configuration
```ini
[SEQUENCER]
blend_mode=MIX              # Défaut
live_mix_level=0.5          # Proportion du live [0.0, 1.0]
```

### 3.3 Synchronisation MIDI Clock

#### 3.3.1 Tempo master

**Source prioritaire** : MIDI clock (24 PPQ - Pulses Per Quarter note)
- Extraction automatique du BPM depuis MIDI timing messages
- Suivi en temps réel des changements de tempo

**Fallback** : Horloge interne
- Si pas de MIDI clock détectée
- BPM configurable manuellement (défaut: 120 BPM)

```ini
midi_clock_sync=1           # Activer sync MIDI clock
default_bpm=120.0           # BPM de fallback
```

#### 3.3.2 Quantification temporelle

Démarrage des séquences quantifié pour synchronisation rythmique précise :

| Resolution | Division | Exemple @ 120 BPM |
|------------|----------|-------------------|
| 1/4 note | Quarter | Tous les 500 ms |
| 1/8 note | Eighth | Tous les 250 ms |
| 1/16 note | Sixteenth | Tous les 125 ms |
| 1 bar | 4 beats | Tous les 2000 ms |

**Algorithme** :
1. Player demande start en mode `SYNC`
2. Calcul du prochain point de quantification
3. Attente passive jusqu'au point
4. Démarrage précis sur le beat

---

## 4. CONTRÔLE MIDI

### 4.1 Intégration avec le système MIDI unifié

Le séquenceur utilise le système MIDI unifié décrit dans **MIDI_SYSTEM_SPECIFICATION.md**.

**Paramètres MIDI du séquenceur** :

#### Par player (5 players) :
- `record_toggle` : Toggle enregistrement
- `play_stop` : Toggle lecture/pause (PLAYING ↔ STOPPED)
- `mute_toggle` : Toggle mute (PLAYING ↔ MUTED)
- `speed` : Vitesse de lecture [0.1, 10.0]
- `blend_level` : Niveau dans le mix [0.0, 1.0]
- `offset` : Décalage temporel [0.0, 1.0]
- `attack` : ADSR attack [0, 5000] ms
- `release` : ADSR release [0, 10000] ms
- `loop_mode` : Mode de boucle [0-2]
- `playback_direction` : Direction lecture (0=FORWARD, 1=REVERSE)

#### Global :
- `live_mix_level` : Niveau du live [0.0, 1.0]
- `blend_mode` : Mode de fusion [0-3]
- `master_tempo` : BPM manuel [60, 240]
- `quantize_res` : Résolution quantification [0-3]

### 4.2 Enregistrement des callbacks MIDI

Le séquenceur fournit une fonction d'enregistrement automatique :

```c
void image_sequencer_register_midi_callbacks(ImageSequencer *seq);
```

Cette fonction enregistre automatiquement tous les callbacks nécessaires auprès du système MIDI unifié.

**Voir** : `MIDI_SYSTEM_SPECIFICATION.md` section 6 pour les détails d'implémentation.

---

## 5. ARCHITECTURE LOGICIELLE

### 5.1 Structure de données

**Fichier** : `src/processing/image_sequencer.h`

```c
#ifndef IMAGE_SEQUENCER_H
#define IMAGE_SEQUENCER_H

#include <stdint.h>
#include <pthread.h>
#include "../processing/image_preprocessor.h"

/* Configuration constants */
#define MAX_SEQUENCE_DURATION_S 10.0f
#define MAX_SEQUENCE_FRAMES (int)(MAX_SEQUENCE_DURATION_S * 1000) // 10000 frames max
#define DEFAULT_NUM_PLAYERS 5

/* Player state machine */
typedef enum {
    PLAYER_STATE_IDLE,       // No sequence loaded
    PLAYER_STATE_RECORDING,  // Recording from live
    PLAYER_STATE_READY,      // Sequence loaded, ready to play
    PLAYER_STATE_PLAYING,    // Active playback
    PLAYER_STATE_STOPPED,    // Paused but still in mix (frame frozen)
    PLAYER_STATE_MUTED       // Muted, removed from mix
} PlayerState;

/* Loop modes */
typedef enum {
    LOOP_MODE_SIMPLE,        // A→B→A→B...
    LOOP_MODE_PINGPONG,      // A→B→A→B→A...
    LOOP_MODE_ONESHOT        // A→B→[STOP]
} LoopMode;

/* Trigger modes */
typedef enum {
    TRIGGER_MODE_MANUAL,     // Manual start via MIDI/API
    TRIGGER_MODE_AUTO,       // Auto-start after recording
    TRIGGER_MODE_SYNC        // Sync to MIDI clock (quantized)
} TriggerMode;

/* Blend modes for mixing sequences */
typedef enum {
    BLEND_MODE_MIX,          // Weighted average
    BLEND_MODE_CROSSFADE,    // Linear interpolation
    BLEND_MODE_OVERLAY,      // Additive with clipping
    BLEND_MODE_MASK          // Multiplicative masking
} BlendMode;

/* ADSR envelope for volume shaping */
typedef struct {
    /* Parameters (in milliseconds) */
    float attack_ms;
    float decay_ms;
    float sustain_level;     // [0.0, 1.0]
    float release_ms;
    
    /* Runtime state */
    float current_level;     // Current envelope output [0.0, 1.0]
    uint64_t trigger_time_us;
    uint64_t release_time_us;
    int is_triggered;        // 1 = attack/sustain phase, 0 = release phase
} ADSREnvelope;

/* Sequence player (one per sequence) */
typedef struct {
    /* Sequence storage (ring buffer) */
    PreprocessedImageData *frames;  // Statically allocated at init
    int buffer_capacity;             // Max frames (e.g., 5000 for 5s @ 1000fps)
    int recorded_frames;             // Actual recorded frames
    
    /* Playback control */
    float playback_position;         // Current position (float for fractional speeds)
    float playback_speed;            // Speed multiplier [0.1, 10.0]
    int playback_offset;             // Start offset in frames
    int playback_direction;          // 1 = forward, -1 = backward (for ping-pong)
    
    /* State and modes */
    PlayerState state;
    LoopMode loop_mode;
    TriggerMode trigger_mode;
    
    /* Envelope */
    ADSREnvelope envelope;
    
    /* Mix level */
    float blend_level;               // Player's contribution to mix [0.0, 1.0]
    
} SequencePlayer;

/* Main sequencer structure */
typedef struct {
    /* Players array */
    SequencePlayer *players;         // Array of players (static allocation)
    int num_players;                 // Number of players (e.g., 5)
    
    /* Global mix control */
    BlendMode blend_mode;            // Current blending mode
    float live_mix_level;            // Live input mix level [0.0, 1.0]
    
    /* MIDI clock sync */
    float bpm;                       // Current BPM (from MIDI or manual)
    int midi_clock_sync;             // 1 = sync to MIDI clock, 0 = free-running
    uint64_t last_clock_us;          // Last MIDI clock tick timestamp
    
    /* Output buffer (reused every frame) */
    PreprocessedImageData output_frame;
    
    /* Thread safety */
    pthread_mutex_t mutex;           // Protects all state (lightweight, < 10us)
    
    /* Statistics */
    uint64_t frames_processed;
    uint64_t total_process_time_us;
    
} ImageSequencer;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/* Initialization and cleanup */
ImageSequencer* image_sequencer_create(int num_players, float max_duration_s);
void image_sequencer_destroy(ImageSequencer *seq);

/* Player control - Recording */
int image_sequencer_start_recording(ImageSequencer *seq, int player_id);
int image_sequencer_stop_recording(ImageSequencer *seq, int player_id);

/* Player control - Playback */
int image_sequencer_start_playback(ImageSequencer *seq, int player_id);
int image_sequencer_stop_playback(ImageSequencer *seq, int player_id);
int image_sequencer_toggle_playback(ImageSequencer *seq, int player_id);

/* Player parameters */
void image_sequencer_set_speed(ImageSequencer *seq, int player_id, float speed);
void image_sequencer_set_offset(ImageSequencer *seq, int player_id, int offset_frames);
void image_sequencer_set_loop_mode(ImageSequencer *seq, int player_id, LoopMode mode);
void image_sequencer_set_trigger_mode(ImageSequencer *seq, int player_id, TriggerMode mode);
void image_sequencer_set_blend_level(ImageSequencer *seq, int player_id, float level);
void image_sequencer_set_playback_direction(ImageSequencer *seq, int player_id, int direction);

/* Player state control */
int image_sequencer_mute_player(ImageSequencer *seq, int player_id);
int image_sequencer_unmute_player(ImageSequencer *seq, int player_id);
int image_sequencer_toggle_mute(ImageSequencer *seq, int player_id);

/* ADSR control */
void image_sequencer_set_adsr(ImageSequencer *seq, int player_id, 
                              float attack_ms, float decay_ms, 
                              float sustain_level, float release_ms);
void image_sequencer_trigger_envelope(ImageSequencer *seq, int player_id);
void image_sequencer_release_envelope(ImageSequencer *seq, int player_id);

/* Global control */
void image_sequencer_set_blend_mode(ImageSequencer *seq, BlendMode mode);
void image_sequencer_set_live_mix_level(ImageSequencer *seq, float level);
void image_sequencer_set_bpm(ImageSequencer *seq, float bpm);
void image_sequencer_enable_midi_sync(ImageSequencer *seq, int enable);

/* MIDI clock integration */
void image_sequencer_midi_clock_tick(ImageSequencer *seq);
void image_sequencer_midi_clock_start(ImageSequencer *seq);
void image_sequencer_midi_clock_stop(ImageSequencer *seq);

/* Main processing function (called from UDP thread or dedicated thread) */
int image_sequencer_process_frame(
    ImageSequencer *seq,
    const PreprocessedImageData *live_input,
    PreprocessedImageData *output
);

/* MIDI callback registration */
void image_sequencer_register_midi_callbacks(ImageSequencer *seq);

/* Statistics and debugging */
void image_sequencer_get_stats(ImageSequencer *seq, 
                               uint64_t *frames_processed, 
                               float *avg_process_time_us);
void image_sequencer_print_status(ImageSequencer *seq);

#endif /* IMAGE_SEQUENCER_H */
```

### 5.2 Intégration dans main.c

Deux options architecturales possibles :

#### Option A : Traitement dans le thread UDP (synchrone) ✅ RECOMMANDÉ

```c
// Dans udpThread() après image_preprocess_frame()
PreprocessedImageData preprocessed_data;
image_preprocess_frame(R, G, B, &preprocessed_data);

// Nouveau: Traiter via le séquenceur
PreprocessedImageData sequencer_output;
if (image_sequencer_process_frame(g_sequencer, &preprocessed_data, &sequencer_output) == 0) {
    // Utiliser la sortie du séquenceur pour audio/DMX/display
    process_audio_with_data(&sequencer_output);
} else {
    // Fallback: utiliser directement les données prétraitées
    process_audio_with_data(&preprocessed_data);
}
```

**Avantages** :
- Simplicité d'intégration
- Pas de thread supplémentaire
- Latence minimale
- Synchronisation naturelle avec l'arrivée des données

**Inconvénients** :
- Charge CPU dans le thread UDP
- Moins d'isolation

#### Option B : Thread dédié séquenceur (asynchrone)

```
UDP Thread : [Receive] → [Preprocess] → [Ring Buffer] 
                                              ↓
                                    Sequencer Thread → [Audio Buffers]
```

**Avantages** :
- Isolation du traitement
- Priorité RT indépendante
- Possibilité de buffer plus important

**Inconvénients** :
- Complexité accrue
- Synchronisation supplémentaire
- Latence additionnelle

**Décision** : Commencer avec Option A, migrer vers B si nécessaire.

---

## 6. CONFIGURATION (sp3ctra.ini)

Ajouter une nouvelle section au fichier `sp3ctra.ini` :

```ini
# ============================================================================
# IMAGE SEQUENCER CONFIGURATION
# ============================================================================

[SEQUENCER]
# Enable/disable sequencer module
enabled=1

# Number of sequence players (1-10)
num_players=5

# Maximum duration per sequence in seconds (1.0-60.0)
max_duration_s=5.0

# Global blend mode: MIX, CROSSFADE, OVERLAY, MASK
blend_mode=MIX

# Live mix level (0.0 = no live, 1.0 = full live)
live_mix_level=0.5

# Player default parameters
default_loop_mode=LOOP_SIMPLE      # LOOP_SIMPLE, LOOP_PINGPONG, ONESHOT
default_trigger_mode=MANUAL        # MANUAL, AUTO, SYNC
default_speed=1.0                  # Playback speed multiplier

# ADSR envelope defaults (in milliseconds)
default_attack_ms=10.0
default_decay_ms=50.0
default_sustain_level=0.8          # 0.0 to 1.0
default_release_ms=100.0

# MIDI synchronization (see MIDI_SYSTEM_SPECIFICATION.md)
midi_clock_sync=1                  # 1 = sync to MIDI clock, 0 = free-running
default_bpm=120.0                  # Fallback BPM if no MIDI clock

# Quantization resolution for SYNC trigger mode
# Options: QUARTER (1/4), EIGHTH (1/8), SIXTEENTH (1/16), BAR (1 bar)
quantize_resolution=EIGHTH
```

---

## 7. AFFICHAGE VISUEL

### 7.1 Intégration avec le balayage existant

Le module `display.c` continue de fonctionner **sans modification** :
- Il reçoit le **mix final** produit par le séquenceur
- Le balayage montre la combinaison live + séquences actives
- Aucun changement d'interface utilisateur nécessaire

**Workflow** :
```
Séquenceur → output_frame → display.c → Affichage SFML
```

### 7.2 Monitoring optionnel (Phase future)

Possibilité d'ajouter un overlay pour afficher :
- État des players (⚫ IDLE / 🔴 REC / ▶️ PLAY / ⏸️ STOP)
- Niveau de chaque player (bargraph)
- Position de lecture (progress bar)
- Enveloppe ADSR actuelle (graphique)

---

## 8. PLAN DE DÉVELOPPEMENT

### Phase 1 : Infrastructure de base (Semaine 1)
- [ ] Créer `src/processing/image_sequencer.h` avec structures de données
- [ ] Créer `src/processing/image_sequencer.c` avec fonctions de base
- [ ] Implémenter `image_sequencer_create()` avec allocation statique
- [ ] Implémenter `image_sequencer_destroy()`
- [ ] Intégrer dans `main.c` (mode pass-through initial)
- [ ] Compiler et tester sur macOS et Raspberry Pi 5

**Livrables** :
- Code compilable
- Module initialisable sans erreur
- Pass-through fonctionnel (entrée = sortie)

### Phase 2 : Enregistrement/Lecture simple (Semaine 2)
- [ ] Implémenter `image_sequencer_start_recording()`
- [ ] Implémenter `image_sequencer_stop_recording()`
- [ ] Implémenter enregistrement dans `process_frame()`
- [ ] Implémenter `image_sequencer_start_playback()`
- [ ] Implémenter lecture à vitesse normale (×1)
- [ ] Implémenter mode loop simple

**Tests** :
- Enregistrer 5s de séquence
- Lire en boucle
- Vérifier intégrité des données

### Phase 3 : Contrôle de lecture avancé (Semaine 3)
- [ ] Vitesses variables (×0.25 à ×8)
- [ ] Interpolation pour vitesses fractionnaires
- [ ] Offset temporel
- [ ] Mode ping-pong
- [ ] Mode one-shot

**Tests** :
- Lecture à toutes les vitesses
- Transitions fluides
- Boundaries correctes

### Phase 4 : Fusion et mix (Semaine 4)
- [ ] Implémenter BLEND_MODE_MIX
- [ ] Implémenter BLEND_MODE_OVERLAY
- [ ] Implémenter BLEND_MODE_MASK
- [ ] Mix live + séquences
- [ ] Enveloppe ADSR

**Tests** :
- Mix de 5 séquences
- Transitions ADSR
- Niveaux corrects

### Phase 5 : Intégration MIDI (Semaine 5)
- [ ] Enregistrer callbacks auprès du système MIDI unifié
- [ ] Intégration MIDI clock
- [ ] Quantification temporelle
- [ ] Tests avec contrôleur

**Tests** :
- Contrôle via MIDI
- Sync MIDI clock
- Quantification précise

### Phase 6 : Configuration et optimisation (Semaine 6)
- [ ] Support complet `sp3ctra.ini`
- [ ] Optimisations ARM NEON (si nécessaire)
- [ ] Profiling et tuning
- [ ] Documentation finale

**Tests** :
- Benchmarks de performance
- Tests de charge 60s
- Validation zéro underrun

---

## 9. CRITÈRES DE SUCCÈS

### 9.1 Critères fonctionnels
✅ Enregistrement de 5 séquences de 5 secondes  
✅ Lecture à vitesses variables (0.25× à 8×)  
✅ 3 modes de boucle fonctionnels  
✅ Mix de 5 séquences simultanées  
✅ Enveloppe ADSR sur chaque player  
✅ Contrôle MIDI complet (via système unifié)  
✅ Sync MIDI clock avec quantification  

### 9.2 Critères de performance
✅ Latence < 1 ms par frame  
✅ CPU < 50% avec 5 players actifs (RPi5)  
✅ Zéro allocation dynamique en RT path  
✅ Zéro underrun audio sur 60s  
✅ Démarrage < 5s (chargement + allocation)  

### 9.3 Critères de qualité
✅ Code conforme aux .clinerules  
✅ Commentaires en anglais  
✅ Tests unitaires > 80% coverage  
✅ Documentation complète  
✅ Pas de warnings clang-tidy  

---

## 10. RÉFÉRENCES

- **MIDI_SYSTEM_SPECIFICATION.md** : Spécification du système MIDI unifié
- **config_instrument.h** : Définition de `CIS_MAX_PIXELS_NB` (3456 pixels)
- **image_preprocessor.h** : Structure `PreprocessedImageData`
- **sp3ctra.ini** : Configuration générale de l'application

---

## CHANGELOG

| Version | Date | Auteur | Modifications |
|---------|------|--------|---------------|
| 1.0
