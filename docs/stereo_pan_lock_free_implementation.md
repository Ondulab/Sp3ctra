# Implémentation Lock-Free pour Stereo Pan

## Objectif
Éliminer les race conditions entre le calcul de température (thread UDP) et la synthèse audio (thread pool) sans introduire de blocage.

## Architecture Lock-Free

### 1. Structure de Double Buffering
```c
typedef struct {
    // Double buffers pour les gains de panoramisation
    float left_gain_A[NUMBER_OF_NOTES];
    float right_gain_A[NUMBER_OF_NOTES];
    float left_gain_B[NUMBER_OF_NOTES];
    float right_gain_B[NUMBER_OF_NOTES];
    
    // Pointeurs atomiques pour swap instantané
    _Atomic(float*) read_left_ptr;
    _Atomic(float*) read_right_ptr;
    _Atomic(float*) write_left_ptr;
    _Atomic(float*) write_right_ptr;
    
    // Version counter pour détecter les mises à jour
    _Atomic(uint32_t) version;
} LockFreePanGains;
```

### 2. Flux de données

```
Thread UDP (synth_AudioProcess)
    ↓
Calculate temperature & gains → Write Buffer
    ↓
Atomic pointer swap
    ↓
Thread Pool Audio → Read Buffer (lock-free)
```

## Modifications nécessaires

### Phase 1: Ajout de la structure lock-free
- Créer `src/audio/pan/lock_free_pan.h` et `.c`
- Initialiser dans `synth_IfftInit()`
- Cleanup dans `synth_shutdown_thread_pool()`

### Phase 2: Modification du calcul de température
- Dans `synth_AudioProcess()`: écrire dans le buffer d'écriture
- Effectuer un swap atomique après calcul complet
- Supprimer l'écriture directe dans `waves[]`

### Phase 3: Modification de la synthèse
- Dans `synth_precompute_wave_data()`: lire depuis les pointeurs atomiques
- Supprimer la lecture directe depuis `waves[]`
- Garantir la cohérence avec memory ordering

## Avantages de cette approche

1. **Performance audio garantie**
   - Zéro mutex, zéro blocage
   - Latence déterministe
   - Parallélisme préservé

2. **Cohérence des données**
   - Swap atomique = transition instantanée
   - Pas de valeurs partiellement mises à jour
   - Version counter pour débogage

3. **Simplicité d'implémentation**
   - ~200 lignes de code
   - Testable unitairement
   - Compatible avec l'architecture existante

## Plan d'implémentation

1. **Étape 1**: Créer la structure lock-free et les fonctions d'accès
2. **Étape 2**: Intégrer dans synth_additive.c
3. **Étape 3**: Tests de non-régression
4. **Étape 4**: Validation des performances

## Métriques de succès

- ✅ Aucune race condition détectée par ThreadSanitizer
- ✅ Latence audio < 5ms (identique à l'actuel)
- ✅ CPU usage inchangé
- ✅ Transition pan fluide sans clicks
