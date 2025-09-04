# Recommandations de Configuration Neutre - Synthèse Additive

## Objectif
Configuration pour obtenir le rendu audio le plus neutre possible, minimisant les traitements et effets artificiels.

## Réglages Recommandés

### 1. Mode de Synthèse
```c
#define SYNTH_MODE SYNTH_MODE_MONO_WHITE_BG
```
**Justification :** Le mode mono évite la séparation stéréo artificielle basée sur les couleurs, qui peut introduire des artefacts de spatialisation non désirés.

### 2. Traitement d'Image et Contraste
```c
#define CONTRAST_MIN                 0.0f    // Pas de volume minimum artificiel
#define CONTRAST_ADJUSTMENT_POWER    1.0f    // Courbe linéaire (pas d'ajustement)
```
**Justification :** 
- `CONTRAST_MIN` à 0.0f permet une dynamique complète
- `CONTRAST_ADJUSTMENT_POWER` à 1.0f évite la compression/expansion non-linéaire

### 3. Mapping d'Intensité Non-Linéaire
```c
#define ENABLE_NON_LINEAR_MAPPING    0       // Désactiver le mapping non-linéaire
#define GAMMA_VALUE                  1.0f    // Gamma neutre (si activé)
```
**Justification :** Le mapping non-linéaire modifie la relation entre l'intensité visuelle et l'amplitude audio.

### 4. Lissage Temporel d'Image
```c
#define ENABLE_IMAGE_TEMPORAL_SMOOTHING 0    // Désactiver le lissage
```
**Justification :** Le lissage temporel introduit une latence et modifie la réponse transitoire.

### 5. Limiteur de Gap avec Prise en Compte de Phase
```c
#define ENABLE_PHASE_AWARE_GAP_LIMITER 0     // Utiliser le limiteur classique
```
**Justification :** Le limiteur classique est plus prévisible et introduit moins de modifications du signal.

### 6. Auto-Volume (Optionnel)
Pour une neutralité maximale, désactiver l'auto-volume :
```c
#define AUTO_VOLUME_ACTIVE_LEVEL     1.0f    // Volume constant maximum
#define AUTO_VOLUME_INACTIVE_LEVEL   1.0f    // Pas de réduction automatique
```

## Paramètres à Conserver (Déjà Neutres)

### Résolutions et Fréquences
```c
#define WAVE_AMP_RESOLUTION          (16777215)  // Haute résolution
#define VOLUME_AMP_RESOLUTION        (65535)     // Haute résolution
#define START_FREQUENCY              (65.41)     // Fréquence de base standard
```

### Incréments de Volume
```c
#define VOLUME_INCREMENT             (1)         // Pas fins
#define VOLUME_DECREMENT             (1)         // Pas fins
```

## Configuration Complète Recommandée

Voici les modifications à apporter au fichier `config_synth_additive.h` :

1. **Mode mono** pour éviter la spatialisation artificielle
2. **Désactiver le mapping non-linéaire** pour une relation directe intensité/amplitude
3. **Contraste minimum à 0.0f** pour une dynamique complète
4. **Puissance d'ajustement à 1.0f** pour une courbe linéaire
5. **Désactiver le lissage temporel** pour préserver les transitoires
6. **Utiliser le limiteur de gap classique** pour plus de prévisibilité

## Impact sur la Qualité Audio

Cette configuration privilégie :
- **Fidélité** : Relation directe entre image et son
- **Dynamique** : Plage dynamique maximale
- **Transitoires** : Préservation des attaques et des variations rapides
- **Prévisibilité** : Comportement déterministe et reproductible

## Compromis

Cette configuration neutre peut réduire :
- La richesse stéréophonique (mode mono)
- La correction automatique des images floues
- La stabilité temporelle (sans lissage)
- L'adaptation automatique du volume
