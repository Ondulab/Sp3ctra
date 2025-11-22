# Activation du système de threading déterministe RT

## ✅ Ce qui a été implémenté (Phases 1 & 2)

### Phase 1 : Priorités temps réel
- ✅ Fonction `synth_set_rt_priority()` avec support SCHED_FIFO (Linux)
- ✅ Support cross-platform (Linux/macOS)
- ✅ Priorités configurables 1-99

### Phase 2 : Barrières de synchronisation
- ✅ Implémentation custom `barrier_t` pour macOS
- ✅ Support natif `pthread_barrier_t` pour Linux
- ✅ Fonctions `synth_init_barriers()` et `synth_cleanup_barriers()`
- ✅ Wrapper `synth_barrier_wait()`
- ✅ Initialisation des barrières dans `synth_init_thread_pool()`

## 🔧 Ce qui reste à faire pour activation complète

### 1. Activer les priorités RT dans `synth_start_worker_threads()`

Ajouter après la création de chaque thread :

```c
// Phase 1: Set RT priority for deterministic execution
#ifdef __linux__
if (synth_set_rt_priority(worker_threads[i], 80) != 0) {
  log_warning("SYNTH", "Failed to set RT priority for worker %d", i);
}
#endif
```

### 2. Modifier la boucle des workers pour utiliser les barrières

Dans `synth_persistent_worker_thread()`, remplacer :

```c
// ANCIEN CODE (condition variables)
while (!synth_pool_shutdown) {
  pthread_mutex_lock(&worker->work_mutex);
  while (!worker->work_ready && !synth_pool_shutdown) {
    pthread_cond_wait(&worker->work_cond, &worker->work_mutex);
  }
  pthread_mutex_unlock(&worker->work_mutex);
  
  if (synth_pool_shutdown) break;
  
  synth_process_worker_range(worker);
  
  pthread_mutex_lock(&worker->work_mutex);
  worker->work_done = 1;
  worker->work_ready = 0;
  pthread_mutex_unlock(&worker->work_mutex);
}
```

Par :

```c
// NOUVEAU CODE (barrières déterministes)
while (!synth_pool_shutdown) {
  if (g_use_barriers) {
    // Wait at start barrier (all workers + main thread)
    synth_barrier_wait(&g_worker_start_barrier);
    
    if (synth_pool_shutdown) break;
    
    // Process work
    synth_process_worker_range(worker);
    
    // Wait at end barrier (synchronize completion)
    synth_barrier_wait(&g_worker_end_barrier);
  } else {
    // Fallback to condition variables
    pthread_mutex_lock(&worker->work_mutex);
    while (!worker->work_ready && !synth_pool_shutdown) {
      pthread_cond_wait(&worker->work_cond, &worker->work_mutex);
    }
    pthread_mutex_unlock(&worker->work_mutex);
    
    if (synth_pool_shutdown) break;
    
    synth_process_worker_range(worker);
    
    pthread_mutex_lock(&worker->work_mutex);
    worker->work_done = 1;
    worker->work_ready = 0;
    pthread_mutex_unlock(&worker->work_mutex);
  }
}
```

### 3. Modifier `synth_IfftMode()` pour utiliser les barrières

Dans `synth_additive.c`, remplacer la section de lancement des workers :

```c
// ANCIEN CODE
for (int i = 0; i < num_workers; i++) {
  pthread_mutex_lock(&thread_pool[i].work_mutex);
  thread_pool[i].work_ready = 1;
  thread_pool[i].work_done = 0;
  pthread_cond_signal(&thread_pool[i].work_cond);
  pthread_mutex_unlock(&thread_pool[i].work_mutex);
}

// Wait for completion
for (int i = 0; i < num_workers; i++) {
  pthread_mutex_lock(&thread_pool[i].work_mutex);
  while (!thread_pool[i].work_done) {
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_nsec += 1000000;
    if (timeout.tv_nsec >= 1000000000) {
      timeout.tv_sec += 1;
      timeout.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(&thread_pool[i].work_cond, &thread_pool[i].work_mutex, &timeout);
  }
  pthread_mutex_unlock(&thread_pool[i].work_mutex);
}
```

Par :

```c
// NOUVEAU CODE (barrières)
if (g_use_barriers) {
  // Signal start to all workers (deterministic)
  synth_barrier_wait(&g_worker_start_barrier);
  
  // Wait for all workers to complete (deterministic)
  synth_barrier_wait(&g_worker_end_barrier);
} else {
  // Fallback to condition variables
  for (int i = 0; i < num_workers; i++) {
    pthread_mutex_lock(&thread_pool[i].work_mutex);
    thread_pool[i].work_ready = 1;
    thread_pool[i].work_done = 0;
    pthread_cond_signal(&thread_pool[i].work_cond);
    pthread_mutex_unlock(&thread_pool[i].work_mutex);
  }
  
  for (int i = 0; i < num_workers; i++) {
    pthread_mutex_lock(&thread_pool[i].work_mutex);
    while (!thread_pool[i].work_done) {
      struct timespec timeout;
      clock_gettime(CLOCK_REALTIME, &timeout);
      timeout.tv_nsec += 1000000;
      if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
      }
      pthread_cond_timedwait(&thread_pool[i].work_cond, &thread_pool[i].work_mutex, &timeout);
    }
    pthread_mutex_unlock(&thread_pool[i].work_mutex);
  }
}
```

### 4. Cleanup des barrières dans `synth_shutdown_thread_pool()`

Ajouter avant le cleanup final :

```c
// Cleanup barriers if they were initialized
if (g_use_barriers) {
  synth_cleanup_barriers();
}
```

## 🎯 Bénéfices attendus

Une fois activé, le système offrira :

1. **Déterminisme complet** : Tous les workers démarrent et finissent exactement au même moment
2. **Latence prévisible** : Priorités RT garantissent l'exécution sans préemption (Linux)
3. **Variance réduite** : Temps d'exécution constant et prévisible
4. **Performance optimale** : Synchronisation ultra-rapide sans polling

## 📊 Configuration

Le système peut être désactivé en mettant `g_use_barriers = 0` dans le code, ce qui fait retomber sur les condition variables classiques.

## ⚠️ Notes importantes

### Linux (Raspberry Pi)
- Les priorités RT nécessitent `CAP_SYS_NICE` ou configuration dans `/etc/security/limits.conf` :
  ```
  @audio - rtprio 99
  @audio - memlock unlimited
  ```

### macOS
- Les priorités RT ne sont pas complètement supportées
- Les barrières custom fonctionnent correctement
- Le déterminisme est garanti par les barrières même sans priorités RT

## 🧪 Tests recommandés

1. Compiler et tester sur macOS d'abord (barrières uniquement)
2. Tester sur Raspberry Pi avec priorités RT
3. Mesurer la variance du temps d'exécution avant/après
4. Vérifier l'absence d'underruns audio
