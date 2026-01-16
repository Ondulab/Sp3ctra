# Correctifs Qualité Audio - VST

## Problème: Grésillements / Clics sur le Test Tone

### Symptôme
Le La 440Hz généré par le VST grésille ou produit des clics/artefacts.

### Cause Racine
**Variables `static` dans `processBlock()`**

```cpp
// ❌ MAUVAIS: Variable static dans processBlock
static float phase = 0.0f;
```

**Problème:** Quand plusieurs instances du VST sont chargées, elles **partagent** la même variable `static`, causant:
- Sauts de phase entre instances
- Grésillements / clics
- Comportement imprévisible

### Solution Appliquée

```cpp
// ✅ BON: Variable membre de la classe
class Sp3ctraAudioProcessor {
private:
    float testTonePhase = 0.0f;  // Chaque instance a sa propre phase
};
```

**Avantages:**
- Chaque instance du VST a son propre état
- Pas d'interférence entre instances
- Audio propre et stable

## Autres Sources Potentielles de Grésillements

### 1. Buffer Size du DAW
Si le DAW utilise un buffer trop petit:
- **Symptôme:** Crépitements intermittents
- **Solution:** Augmenter le buffer size dans le DAW (512 ou 1024 samples recommandés)

### 2. Sample Rate Mismatch
Si le projet DAW tourne à une fréquence différente:
- **Symptôme:** Son distordu ou trop rapide/lent
- **Solution:** Vérifier que le projet est en 44.1kHz ou 48kHz

### 3. Underruns Audio
Si le CPU est surchargé:
- **Symptôme:** Dropouts / silences courts
- **Solution:** 
  - Fermer d'autres applications
  - Augmenter le buffer size
  - Réduire le nombre d'instances du plugin

## Tests de Qualité Audio

### Test 1: Son Propre
```bash
# Rebuild avec le fix
./scripts/build_vst.sh clean install

# Dans votre DAW:
# 1. Charger une seule instance de Sp3ctra
# 2. Écouter le La 440Hz
# ✅ Doit être propre, sans grésillements
```

### Test 2: Plusieurs Instances
```bash
# Dans votre DAW:
# 1. Charger 3-4 instances de Sp3ctra sur différentes pistes
# 2. Faire jouer toutes les pistes en même temps
# ✅ Toutes doivent produire un La propre et indépendant
```

### Test 3: Automation
```bash
# Dans votre DAW:
# 1. Créer une automation de volume sur la piste
# 2. Monter/descendre le volume
# ✅ Pas de clics/pops pendant les changements de volume
```

## Checklist Qualité Audio

Avant d'intégrer votre code de synthèse:

- [ ] Test tone sans grésillements (1 instance)
- [ ] Test tone sans grésillements (3+ instances)
- [ ] Pas de clics au démarrage/arrêt du DAW
- [ ] Pas de clics lors des changements de volume
- [ ] Latence acceptable (<10ms avec buffer 512@48kHz)

## Règles pour l'Intégration Future

### ✅ À FAIRE
```cpp
class Sp3ctraAudioProcessor {
private:
    // État persistant = variables membres
    float phase = 0.0f;
    std::vector<float> delayBuffer;
    MyState state;
};
```

### ❌ À NE PAS FAIRE
```cpp
void processBlock(...) {
    // JAMAIS de static dans processBlock!
    static float phase = 0.0f;          // ❌
    static std::vector<float> buffer;   // ❌
}
```

### Thread Safety
Vos synthés utilisent des threads. Dans le VST:
- Le `processBlock()` est appelé par le thread audio du DAW
- Vos threads UDP/synthesis peuvent tourner en parallèle
- **Utilisez des mutexes/atomics** pour protéger les données partagées

Exemple:
```cpp
std::atomic<float> sharedParameter{0.0f};  // Thread-safe
```

## Monitoring de la Qualité

### macOS Console.app
Les logs JUCE apparaissent dans Console.app:
```
Filters: "Sp3ctra"
```

### Xcode Instruments
Pour profiler les performances:
```bash
# Build en Debug
./scripts/build_vst.sh debug

# Ouvrir dans Instruments
instruments -t "Time Profiler" \
  vst/build/Sp3ctraVST_artefacts/Debug/Standalone/Sp3ctra.app
```

## Prochaines Étapes

Une fois le test tone confirmé propre:
1. Intégrer progressivement vos synthés
2. Tester chaque synthé indépendamment
3. Vérifier qu'aucun grésaillement n'apparaît
4. Optimiser les sections critiques si besoin
