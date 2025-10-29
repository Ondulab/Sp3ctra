# 🧹 PLAN DE NETTOYAGE COMPLET - Élimination des Duplications

**Date**: 29 octobre 2025  
**Contexte**: Après refactorisation architecturale, supprimer code redondant  
**Objectif**: Obtenir les 73% de gains CPU promis

---

## 📊 ÉTAT ACTUEL

### ✅ Ce qui fonctionne
- Module `image_preprocessor` créé et opérationnel
- Calculs effectués dans le thread UDP (1x par image ~50Hz)
- Données stockées dans `DoubleBuffer.preprocessed_active`
- Compilation réussie

### 🔴 Problème
Le code original dans `synth_AudioProcess()` **N'A PAS ÉTÉ SUPPRIMÉ** !  
Résultat : **Tous les calculs sont faits 2 fois** = consommation CPU augmentée au lieu de diminuée !

---

## 🎯 PLAN DE NETTOYAGE DÉTAILLÉ

### **ÉTAPE 1** : Supprimer bloc température colorimétrique

#### Fichier : `src/synthesis/additive/synth_additive.c`
#### Lignes : ~545-652 (environ 107 lignes)

**Code à supprimer** :
```c
  if (g_sp3ctra_config.stereo_mode_enabled) {
    // Calculate color temperature and pan positions for each oscillator
    // This is done once per image reception for efficiency
    for (int note = 0; note < get_current_number_of_notes(); note++) {
      // Calculate average color for this note's pixels
      uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
      uint32_t pixel_count = 0;
      
      for (int pix = 0; pix < g_sp3ctra_config.pixels_per_note; pix++) {
        uint32_t pixel_idx = note * g_sp3ctra_config.pixels_per_note + pix;
        if (pixel_idx < CIS_MAX_PIXELS_NB) {
          r_sum += buffer_R[pixel_idx];
          g_sum += buffer_G[pixel_idx];
          b_sum += buffer_B[pixel_idx];
          pixel_count++;
        }
      }
      
      if (pixel_count > 0) {
        // Calculate average RGB values
        uint8_t r_avg = r_sum / pixel_count;
        uint8_t g_avg = g_sum / pixel_count;
        uint8_t b_avg = b_sum / pixel_count;
        
        // Calculate color temperature and pan position
        float temperature = calculate_color_temperature(r_avg, g_avg, b_avg);
        waves[note].pan_position = temperature;
        
        // Use temporary variables to avoid volatile qualifier warnings
        float temp_left_gain, temp_right_gain;
        calculate_pan_gains(temperature, &temp_left_gain, &temp_right_gain);
        waves[note].left_gain = temp_left_gain;
        waves[note].right_gain = temp_right_gain;
        
        // Debug output for first few notes (limited frequency)
        if (log_counter % (LOG_FREQUENCY * 10) == 0 && note < 5) {
#ifdef DEBUG_RGB_TEMPERATURE
          printf("Note %d: RGB(%d,%d,%d) -> Temp=%.2f L=%.2f R=%.2f\n",
                 note, r_avg, g_avg, b_avg, temperature,
                 waves[note].left_gain, waves[note].right_gain);
#endif
        }
      } else {
        // Default to center if no pixels
        waves[note].pan_position = 0.0f;
        waves[note].left_gain = 0.707f;
        waves[note].right_gain = 0.707f;
      }
    }
    
    // Update lock-free pan gains system with calculated values
    // Prepare arrays for batch update (allocated once on first use)
    static float *left_gains = NULL;
    static float *right_gains = NULL;
    static float *pan_positions = NULL;
    
    // Allocate once on first use
    if (!left_gains) {
      int num_notes = get_current_number_of_notes();
      left_gains = (float*)calloc(num_notes, sizeof(float));
      right_gains = (float*)calloc(num_notes, sizeof(float));
      pan_positions = (float*)calloc(num_notes, sizeof(float));
      
      if (!left_gains || !right_gains || !pan_positions) {
        fprintf(stderr, "ERROR: Failed to allocate pan gain buffers\n");
        return;
      }
    }
    
    for (int note = 0; note < get_current_number_of_notes(); note++) {
      left_gains[note] = waves[note].left_gain;
      right_gains[note] = waves[note].right_gain;
      pan_positions[note] = waves[note].pan_position;
    }
    
    // Atomic update of all pan gains
    lock_free_pan_update(left_gains, right_gains, pan_positions, get_current_number_of_notes());
  }
```

**Remplacer par** :
```c
  // 🎯 REMOVED: Color temperature calculation - now done in preprocessing (image_preprocessor.c)
  // The stereo pan positions and gains are already calculated and stored in preprocessed data
  // TODO: Use db->preprocessed_active.stereo.pan_positions[] and gains[] when implementing preprocessed data usage
```

---

### **ÉTAPE 2** : Supprimer calcul de contraste

#### Fichier : `src/synthesis/additive/synth_additive.c`
#### Lignes : ~707-710

**Code à supprimer** :
```c
  // Calculate contrast factor based on the processed grayscale image
  // This optimization moves the contrast calculation from synth_IfftMode to here
  // for better performance (calculated once per image instead of per audio buffer)

  float contrast_factor = calculate_contrast(processed_grayScale, CIS_MAX_PIXELS_NB);
```

**Remplacer par** :
```c
  // 🎯 REMOVED: Contrast calculation - now done in preprocessing (image_preprocessor.c)
  // TODO: Get contrast from db->preprocessed_active.contrast_factor
  float contrast_factor = 1.0f; // TEMPORARY: Use preprocessed value once data flow is implemented
```

---

### **ÉTAPE 3** : Résoudre problème grayscale + freeze/fade

#### Problème
La variable `g_grayScale_live` n'est plus remplie (nous avons supprimé l'appel `greyScale()`), mais elle est utilisée par le système de freeze/fade (lignes 660-705).

#### Solution
**Option A** (Simple mais pas optimal) :
Restaurer temporairement l'appel `greyScale()` uniquement pour le freeze/fade :
```c
  // Temporary: Keep grayscale conversion for freeze/fade system
  // This will be removed once we use preprocessed grayscale data
  greyScale(buffer_R, buffer_G, buffer_B, g_grayScale_live, CIS_MAX_PIXELS_NB);
```

**Option B** (Optimal mais plus complexe) :
Utiliser les données prétraitées du double buffer pour le freeze/fade.  
Nécessite d'accéder au `DoubleBuffer` depuis `synth_AudioProcess()`.

**Recommandation** : Commencer par Option A pour garder le système fonctionnel.

---

### **ÉTAPE 4** : Utiliser données prétraitées (CRITIQUE)

#### Fichier : `src/synthesis/additive/synth_additive.c`
#### Fonction : `synth_AudioProcess()`

**Modifications nécessaires** :

1. **Ajouter accès au DoubleBuffer** :
```c
void synth_AudioProcess(uint8_t *buffer_R, uint8_t *buffer_G,
                        uint8_t *buffer_B, DoubleBuffer *db) {  // Nouveau paramètre
```

2. **Récupérer données prétraitées** :
```c
  // Lock mutex to access preprocessed data
  pthread_mutex_lock(&db->mutex);
  
  // Get preprocessed grayscale data
  memcpy(g_grayScale_live, db->preprocessed_active.grayscale, 
         CIS_MAX_PIXELS_NB * sizeof(float));
  
  // Get contrast factor
  float contrast_factor = db->preprocessed_active.contrast_factor;
  
  // Get stereo data if enabled
  if (g_sp3ctra_config.stereo_mode_enabled) {
    for (int note = 0; note < get_current_number_of_notes(); note++) {
      waves[note].pan_position = db->preprocessed_active.stereo.pan_positions[note];
      waves[note].left_gain = db->preprocessed_active.stereo.left_gains[note];
      waves[note].right_gain = db->preprocessed_active.stereo.right_gains[note];
    }
    
    // Update lock-free pan gains
    lock_free_pan_update(
      db->preprocessed_active.stereo.left_gains,
      db->preprocessed_active.stereo.right_gains,
      db->preprocessed_active.stereo.pan_positions,
      get_current_number_of_notes()
    );
  }
  
  pthread_mutex_unlock(&db->mutex);
```

3. **Mettre à jour tous les appels** :
Tous les appels à `synth_AudioProcess()` doivent passer le `DoubleBuffer *` :
- Dans `multithreading.c` : `audioProcessingThread()`
- Possiblement ailleurs

---

### **ÉTAPE 5** : Nettoyer DMX dans main.c

#### Fichier : `src/core/main.c`  
#### Lignes : ~645-655

**Code à supprimer** :
```c
      // Calcul de la couleur moyenne et mise à jour du contexte DMX
      // DMX utilise les données copiées local_main_R,G,B (qui sont les données
      // live de db.processingBuffer)
      if (use_dmx && dmxCtx->spots && dmxCtx->num_spots > 0) {
        computeAverageColorPerZone(local_main_R, local_main_G, local_main_B,
                                   CIS_MAX_PIXELS_NB, dmxCtx->spots, dmxCtx->num_spots);

        pthread_mutex_lock(&dmxCtx->mutex);
        dmxCtx->colorUpdated = 1;
        pthread_cond_signal(&dmxCtx->cond);
        pthread_mutex_unlock(&dmxCtx->mutex);
      }
```

**Remplacer par** :
```c
      // 🎯 Use preprocessed DMX zone averages instead of recalculating
      if (use_dmx && dmxCtx->spots && dmxCtx->num_spots > 0) {
        pthread_mutex_lock(&db.mutex);
        
        // Copy preprocessed DMX averages to DMX spots
        for (int i = 0; i < dmxCtx->num_spots && i < db.preprocessed_active.dmx.num_zones; i++) {
          dmxCtx->spots[i].data.rgb.red = db.preprocessed_active.dmx.zone_averages[i].r;
          dmxCtx->spots[i].data.rgb.green = db.preprocessed_active.dmx.zone_averages[i].g;
          dmxCtx->spots[i].data.rgb.blue = db.preprocessed_active.dmx.zone_averages[i].b;
        }
        
        pthread_mutex_unlock(&db.mutex);
        
        pthread_mutex_lock(&dmxCtx->mutex);
        dmxCtx->colorUpdated = 1;
        pthread_cond_signal(&dmxCtx->cond);
        pthread_mutex_unlock(&dmxCtx->mutex);
      }
```

---

## 🔧 ORDRE D'EXÉCUTION RECOMMANDÉ

### Phase 1 : Nettoyage sûr (ne casse rien)
1. ✅ Supprimer `greyScale()` - FAIT
2. Supprimer bloc température colorimétrique
3. Supprimer calcul contraste

### Phase 2 : Restauration temporaire (pour garder fonctionnel)
4. Re-ajouter `greyScale()` temporairement pour freeze/fade

### Phase 3 : Tests intermédiaires
5. Compiler et tester que le système fonctionne
6. Vérifier que stereo et contraste fonctionnent (avec valeurs par défaut)

### Phase 4 : Intégration données prétraitées
7. Modifier signature `synth_AudioProcess()` pour recevoir `DoubleBuffer*`
8. Récupérer et utiliser données prétraitées
9. Supprimer le `greyScale()` temporaire

### Phase 5 : Optimisation DMX
10. Modifier main.c pour utiliser DMX prétraité

### Phase 6 : Tests finaux
11. Compiler et tester système complet
12. Mesurer gains CPU avec profiler
13. Vérifier qualité audio/stéréo/DMX

---

## ✅ CRITÈRES DE SUCCÈS

1. **Compilation** : Aucune erreur, aucun warning
2. **Fonctionnel** : Audio, stéréo, DMX fonctionnent correctement
3. **Performance** : ~73% réduction CPU sur calculs d'image mesurée
4. **Qualité** : Aucune régression audio ou visuelle
5. **Code propre** : Aucun calcul dupliqué, commentaires clairs

---

## 📝 NOTES IMPORTANTES

### Synchronisation thread-safe
- Toujours utiliser `pthread_mutex_lock(&db->mutex)` avant d'accéder `preprocessed_active`
- Les données stéréo peuvent être accédées via `lock_free_pan` (déjà thread-safe)

### Tests de régression
- Comparer spectrogrammes audio avant/après
- Vérifier positions panoramiques stéréo
- Tester DMX avec visualiseur

### Documentation
- Mettre à jour README avec nouvelle architecture
- Documenter flux de données UDP → Preprocess → Audio
- Ajouter diagrammes si nécessaire

---

## 🚀 PROCHAINES ÉTAPES IMMÉDIATES

1. **Valider ce plan** avec l'équipe
2. **Créer branche git** : `feature/cleanup-preprocessing`
3. **Commencer Phase 1** (suppressions sûres)
4. **Commit atomiques** après chaque modification
5. **Tests** entre chaque phase

---

**Auteur** : Cline AI  
**Révision** : À valider par équipe dev
