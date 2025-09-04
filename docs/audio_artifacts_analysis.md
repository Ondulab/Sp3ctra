# Analyse des artefacts sonores dans la synthèse additive

## Sources principales d'artefacts identifiées

### 1. Fluctuations brutales des données d'image
**Problème :** Les variations rapides des valeurs de pixels créent des discontinuités dans les paramètres de synthèse, générant des clics et pops audibles.

**Localisation :** 
- `synth_AudioProcess()` et `synth_AudioProcessStereo()` - conversion directe RGB vers niveaux de gris
- `synth_AdditiveMode()` - traitement des données d'image sans lissage temporel

### 2. Absence de lissage temporel des paramètres de synthèse
**Problème :** Les changements instantanés de volume et de fréquence créent des artefacts audibles, particulièrement visibles en mode couleur où les canaux chaud/froid peuvent varier indépendamment.

**Localisation :**
- `GAP_LIMITER` existe mais insuffisant pour les variations rapides
- Pas de lissage inter-frames des données d'image

### 3. Normalisation différentielle entre plateformes
**Problème :** Le code applique une division par 3 sur Linux/Pi mais pas sur Mac, créant des différences de comportement.

**Localisation :**
```c
#ifdef __linux__
    // Pi/Linux : Diviser par 3 (BossDAC/ALSA amplifie naturellement)
    scale_float(additiveBuffer, 1.0f / 3.0f, AUDIO_BUFFER_SIZE);
#else
    // Mac : Pas de division
#endif
```

### 4. Contention des threads en mode stéréo
**Problème :** Les threads accèdent simultanément aux structures `waves[]` globales, créant des conditions de course.

**Localisation :**
- `synth_process_worker_range()` - accès concurrent aux `waves[note].current_volume`
- Solution partielle avec `synth_AdditiveMode_Stateless()` mais pas utilisée partout

### 5. Quantification et résolution limitée
**Problème :** Les conversions entre différentes résolutions (8-bit RGB → 16-bit → float) introduisent des erreurs de quantification.

**Localisation :**
- `greyScale()` : conversion 8-bit → 16-bit avec perte de précision
- `VOLUME_AMP_RESOLUTION (65535)` vs `WAVE_AMP_RESOLUTION (16777215)`

## Impact en mode couleur

Le mode couleur amplifie ces problèmes car :
1. **Séparation perceptuelle** : Les canaux chaud/froid peuvent varier indépendamment
2. **Double traitement** : Canal gauche (optimisé) + canal droit (stateless) = comportements différents
3. **Complexité algorithmique** : Plus de calculs = plus d'opportunités d'artefacts

## Recommandations d'amélioration

### Priorité 1 : Lissage temporel des données d'image
### Priorité 2 : Unification du traitement stéréo
### Priorité 3 : Amélioration de la résolution numérique
### Priorité 4 : Optimisation des transitions de volume
### Priorité 5 : Filtrage adaptatif basé sur le contenu
