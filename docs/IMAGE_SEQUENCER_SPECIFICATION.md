# CAHIER DES CHARGES - Séquenceur d'Images (Image Sequencer)

**Version:** 2.0  
**Date:** 30/10/2025  
**Auteur:** Sp3ctra Team  
**Statut:** Spécification révisée  
**Dépendances:** MIDI_SYSTEM_SPECIFICATION.md (pour les contrôles MIDI)

---

## 1. CONTEXTE ET OBJECTIF

### 1.1 Position dans l'architecture

Le module s'insère entre la réception UDP et le prétraitement des images :

```
[UDP Thread] → [IMAGE SEQUENCER] → [Image Preprocessor] → [Synthèses/DMX/Display]
     RGB raw      Mix RGB + Live       RGB → Grayscale         Ligne unifiée
                                        + Calcul Pan/DMX
```

**Architecture correcte** :
1. Le séquenceur reçoit et stocke les données RGB brutes (3 × 3456 pixels)
2. Le mixage se fait sur les données RGB (pas sur les données prétraitées)
3. Le preprocessing (grayscale, pan, contrast, DMX) s'applique APRÈS le mixage
4. Cela garantit que le calcul du pan stéréo est basé sur la température de couleur du résultat mixé

**Pourquoi cette architecture** :
- Le pan stéréo est calculé à partir de la température de couleur (couleurs chaudes → droite, froides → gauche)
- Mixer des pans précalculés donnerait un résultat incorrect : moyenne(pan_rouge, pan_bleu) ≠ pan(rouge + bleu)
- Exemple : Rouge (pan droite) + Bleu (pan gauche) = Violet (pan centre) ✓
- Mais : average(pan_droite, pan_gauche) = centre ✗ (fonctionne par hasard mais conceptuellement faux)

### 1.2 Fonction principale

Enregistrer, manipuler temporellement et mixer des séquences de lignes RGB brutes, puis transmettre le résultat mixé au module de prétraitement.

**Flux détaillé** :
1. **Enregistrement** : Capture des RGB bruts depuis le flux UDP live
2. **Manipulation** : Contrôle temporel (vitesse, direction, boucles, offset)
3. **Mixage** : Fusion pondérée de plusieurs séquences + live (si activé)
4. **Sortie** : RGB mixé transmis à `image_preprocessor` pour calcul du grayscale/pan/DMX

### 1.3 Valeur ajoutée

- **Performance créative** : Enregistrement et manipulation en temps réel de séquences visuelles
- **Contrôle temporel** : Lecture à vitesses variables, boucles, synchronisation
- **Expressivité** : Fusion de multiples séquences avec enveloppes ADSR
- **Intégration MIDI** : Contrôle via système MIDI unifié (voir MIDI_SYSTEM_SPECIFICATION.md)

---

## 2. SPÉCIFICATIONS TECHNIQUES

### 2.1 Données d'entrée/sortie

#### Entrée : RGB brut (Raw RGB Data)
- **Résolution** : **3456 pixels** exactement (400 DPI)
- **Source** : `CIS_MAX_PIXELS_NB` défini dans `config_instrument.h`
- **Format** : 3 buffers séparés
  - `buffer_R[3456]` : Canal rouge (uint8_t, [0-255])
  - `buffer_G[3456]` : Canal vert (uint8_t, [0-255])
  - `buffer_B[3456]` : Canal bleu (uint8_t, [0-255])
  - `timestamp_us` : Timestamp microseconde

**Structure de données** :
```c
typedef struct {
    uint8_t *buffer_R;      // 3456 pixels
    uint8_t *buffer_G;      // 3456 pixels
    uint8_t *buffer_B;      // 3456 pixels
    uint64_t timestamp_us;
} RawImageFrame;
```

#### Sortie : RGB mixé (Mixed RGB Data)
- **Format identique** : 3 buffers RGB (3456 pixels chacun)
- **Contenu** : Résultat du mix de toutes les sources actives (séquences + live)
- **Traitement ultérieur** : Ce RGB mixé est ensuite prétraité (grayscale, pan, contrast, DMX) avant d'être envoyé aux synthèses/DMX/affichage

**Avantages de stocker RGB** :
- **Légèreté** : 10.4 KB/frame vs 55 KB/frame (5× plus léger)
- **Correctness** : Pan calculé sur RGB mixé, pas moyenne de pans précalculés
- **Couleur préservée** : Affichage des vraies couleurs pendant le playback
- **Mémoire** : 260 MB totale vs 1.6 GB (6× économie)

### 2.2 Capacité de stockage

| Paramètre | Valeur | Configuration |
|-----------|--------|---------------|
| Nombre de séquences | 5 | `sequencer_max_sequences=5` |
| Durée max/séquence | 5 secondes | `sequencer_max_duration_s=5.0` |
| Fréquence d'acquisition | 1000 images/s | Fixe (UDP) |
| Mémoire/séquence | ~52 MB | 5000 frames × RawImageFrame |
| Mémoire totale | ~260 MB | Acceptable sur 4-8 Go RAM |

**Calcul mémoire détaillé** (nouveau format RGB) :
```c
sizeof(RawImageFrame) = 
    buffer_R[3456] * 1 byte     = 3.456 KB
    + buffer_G[3456] * 1 byte   = 3.456 KB
    + buffer_B[3456] * 1 byte   = 3.456 KB
    + timestamp_us (8 bytes)    = 0.008 KB
    ≈ 10.4 KB par frame

5 secondes @ 1000 fps = 5000 frames
5000 frames × 10.4 KB = 52 MB par séquence

5 séquences × 52 MB = 260 MB
+ Overhead système ~50 MB
Total ≈ 310 MB (confortable sur 4-8 GB RAM)
```

**Comparaison avec l'ancien format (PreprocessedImageData)** :
```c
Ancien format (PreprocessedImageData):
    55 KB/frame × 5000 frames = 275 MB/séquence
    5 séquences = 1.375 GB + overhead = 1.6 GB total

Nouveau format (RawImageFrame RGB):
    10.4 KB/frame × 5000 frames = 52 MB/séquence
    5 séquences = 260 MB + overhead = 310 MB total

Économie: 1.6 GB → 310 MB = 81% de réduction (5.2× plus léger)
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

#### 3.1.4 Enveloppe ADSR (Attack-Decay-Sustain-Release)

Chaque player possède une enveloppe de présence pour façonner son apparition/disparition dans le mix. L'enveloppe est **positionnelle** (basée sur la position dans la séquence) plutôt que temporelle, ce qui permet une adaptation automatique à la vitesse de lecture et aux boucles.

```
Présence
  1.0 │     ╱╲──────────────╲
      │    ╱  ╲              ╲
      │   ╱    ╲  Sustain     ╲
      │  ╱ Decay╲              ╲ Release
      │ ╱ Attack ╲              ╲
  0.0 │────────────────────────────╲───
      └──────────────────────────────────> Position normalisée [0.0, 1.0]
      0%  5% 10%           90%    100%
```

**Paramètres (en % de la séquence)** :
- **Attack** (A) : Ratio de montée - [0.0, 1.0] (0% = pas d'attack, 0.1 = 10% de la séquence)
- **Decay** (D) : Ratio de descente - [0.0, 1.0] (0% = pas de decay, 0.05 = 5% de la séquence)
- **Sustain** (S) : Niveau de maintien - [0.0, 1.0] (1.0 = présence totale)
- **Release** (R) : Ratio de relâchement - [0.0, 1.0] (0% = pas de release, 0.1 = 10% de la séquence)

**Calcul de l'enveloppe** :
```c
// Position normalisée dans la séquence [0.0, 1.0]
float norm_pos = playback_position / recorded_frames;
float attack_end = attack_ratio;
float decay_end = attack_end + decay_ratio;
float sustain_end = 1.0 - release_ratio;

if (norm_pos < attack_end) {
    // Phase d'attack (0 → 1.0)
    level = norm_pos / attack_ratio;
} else if (norm_pos < decay_end) {
    // Phase de decay (1.0 → sustain_level)
    float decay_pos = (norm_pos - attack_end) / decay_ratio;
    level = lerp(1.0, sustain_level, decay_pos);
} else if (norm_pos < sustain_end) {
    // Phase de sustain (niveau constant)
    level = sustain_level;
} else {
    // Phase de release (sustain_level → 0)
    float release_pos = (norm_pos - sustain_end) / release_ratio;
    level = lerp(sustain_level, 0.0, release_pos);
}
```

**Valeurs par défaut (pas d'enveloppe, présence immédiate à 100%)** :
```ini
default_attack_ratio=0.0      # Pas d'attack
default_decay_ratio=0.0       # Pas de decay
default_sustain_level=1.0     # Présence totale
default_release_ratio=0.0     # Pas de release
```

**Avantages de l'enveloppe positionnelle** :
- ✅ **Adaptation automatique** : S'adapte à la vitesse de lecture (speed)
- ✅ **Répétition naturelle** : Se répète automatiquement dans les boucles
- ✅ **Intuitivité** : "10% attack" = compréhensible visuellement
- ✅ **Direction inversée** : Fonctionne en reverse (release devient attack)
- ✅ **Pingpong** : S'inverse naturellement avec la direction
- ✅ **ADSR complet** : Contrôle fin avec les 4 phases classiques

**Comportements spéciaux** :
- **Mode ONESHOT** : L'enveloppe se déroule normalement sur toute la séquence
- **Mode SIMPLE loop** : L'enveloppe redémarre à chaque boucle
- **Mode PINGPONG** : L'enveloppe s'inverse avec la direction
- **Player STOPPED** : La frame reste figée au niveau actuel de l'enveloppe

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
- `attack` : ADSR attack ratio [0.0, 1.0] (% de la séquence)
- `decay` : ADSR decay ratio [0.0, 1.0] (% de la séquence)
- `sustain` : ADSR sustain level [0.0, 1.0]
- `release` : ADSR release ratio [0.0, 1.0] (% de la séquence)
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
