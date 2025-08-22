Spéfication technique — Auto-dimming basé sur l'accéléromètre X
================================================================

Version
-------
- 1.0 — 2025-08-21 — Initial implementation and specification

Contexte et objectif
--------------------
Dans le démonstrateur muséal, la synthèse sonore doit diminuer fortement (et de manière douce) lorsque l'instrument n'est plus utilisé, en se basant uniquement sur l'axe X de l'accéléromètre intégré à la centrale inertielle du scanner. Les données IMU (accéléromètre + gyroscope) sont envoyées par UDP en même temps que l'image. Le système doit :
- détecter l'absence d'utilisation de l'instrument,
- réduire le volume vers un niveau de repos configurable (ex : 5%),
- restaurer le volume quand l'activité reprend,
- éviter tout traitement bloquant dans le chemin temps réel audio.

Contraintes
-----------
- Ne pas effectuer d'opérations bloquantes (I/O, allocs lentes, locks longs) dans le callback audio RT (RtAudio).
- Traitement léger et déterministe (quelques flottants, expf) dans les threads non-RT.
- Respecter les conventions du projet : documentation en français, code et commentaires de code en anglais.
- Respecter l’architecture existante (udpThread, audioProcessingThread, RtAudio callback, double buffers).

Terminologie
-----------
- IMU packet: structure packet_IMU (définie dans multithreading.h), header IMU_DATA_HEADER (0x13).
- imu_x: composante X de l'accéléromètre, récupérée via packet_IMU.acc[0].
- ctx / Context: structure partagée du programme (src/core/context.h).
- gAudioSystem: instance globale du wrapper RtAudio (audio_rtaudio.cpp/h).
- AutoVolume: module implémentant la logique d'auto-dim.

Exigences fonctionnelles (FR)
-----------------------------
1. Détection d'activité
   - Utiliser uniquement imu.acc[0] (axe X) pour décider activité/inactivité.
   - Appliquer un filtre passe-bas exponentiel sur imu_x pour réduire le bruit.
   - Considérer actif si |imu_x_filtered| >= seuil configurable.

2. Réduction du volume
   - Quand inactif, la cible de volume doit être AUTO_VOLUME_INACTIVE_LEVEL (configurable, ex. 0.05).
   - Quand actif, la cible de volume doit être AUTO_VOLUME_ACTIVE_LEVEL (configurable, ex. 1.0).
   - Transitions lissées par un fade temporel configuré (AUTO_VOLUME_FADE_MS).

3. Robustesse
   - Tolérer des pertes temporaires de paquets IMU (tolérance par timeout).
   - Option pour désactiver auto-dim si contrôleur MIDI connecté (pour éviter conflits).

4. Non-régression audio
   - Pas d’impact perceptible sur le rendu audio (éviter blocages, mutex longs, allocations fréquentes).

Exigences non-fonctionnelles (NF)
---------------------------------
- Latence : calculs de l’auto-volume doivent être exécutés au plus toutes les AUTO_VOLUME_POLL_MS ms (ex 50ms).
- Charge CPU : ajouter une charge négligeable (<1% sur cible Pi en conditions normales).
- Thread-safety : accès partagé via mutex courts (pthread_mutex_t).
- Observabilité : logs optionnels pour calibration.

Architecture proposée
---------------------
Composants impliqués :
- udpThread (src/core/multithreading.c) : décode les paquets IMU et met à jour Context. Doit rester très léger.
- AutoVolume module (src/core/auto_volume.h / .c) : lit Context, calcule target et current volume, applique via gAudioSystem->setMasterVolume().
- audioProcessingThread (src/core/multithreading.c) : appelle périodiquement auto_volume_step() (non-RT).
- RtAudio callback (audio_rtaudio.cpp) : lit masterVolume et mixe; aucun changement dans le callback.

Flux de données
---------------
1. Scanner -> UDP : envoi paquets IMAGE et IMU.
2. udpThread reçoit packet_IMU, extrait imu.acc[0], met à jour ctx->imu_x_filtered (filtre exponentiel) et ctx->last_imu_time sous mutex.
3. audioProcessingThread, toutes les AUTO_VOLUME_POLL_MS, appelle auto_volume_step() qui :
   - lit ctx->imu_x_filtered et ctx->last_imu_time sous mutex,
   - calcule état actif/inactif,
   - calcule target volume,
   - lisse la valeur current -> apply via gAudioSystem->setMasterVolume(current),
   - met à jour des champs d'observabilité dans Context.
4. RtAudio callback utilise masterVolume (déjà intégré) pour appliquer attenuation.

Algorithmes détaillés
---------------------
1) Filtre exponentiel (udpThread)
- Implémentation :
  if (!ctx->imu_has_value) ctx->imu_x_filtered = raw_x;
  else ctx->imu_x_filtered = alpha * raw_x + (1-alpha) * ctx->imu_x_filtered;
- Paramètre : alpha = IMU_FILTER_ALPHA_X (0..1). Valeurs recommandées : 0.15 - 0.4.

2) Détection d'activité (auto_volume_step)
- active = 1 si (ctx->imu_has_value && fabs(imu_x_filtered) >= IMU_ACTIVE_THRESHOLD_X)
- sinon active = 1 si (now - ctx->last_imu_time) <= IMU_INACTIVITY_TIMEOUT_S
- target = active ? AUTO_VOLUME_ACTIVE_LEVEL : AUTO_VOLUME_INACTIVE_LEVEL

3) Lissage (exponential ramp)
- tau = AUTO_VOLUME_FADE_MS (ms)
- dt = temps écoulé depuis dernier appel (ms)
- alpha_step = 1 - exp(-dt / tau)
- current += (target - current) * alpha_step
- on appelle gAudioSystem->setMasterVolume(current) si gAudioSystem disponible

Paramètres de configuration (déjà ajoutés dans src/core/config.h)
------------------------------------------------------------------
- IMU_ACTIVE_THRESHOLD_X (float) — default 0.15f
- IMU_FILTER_ALPHA_X (float) — default 0.25f
- IMU_INACTIVITY_TIMEOUT_S (int) — default 5
- AUTO_VOLUME_INACTIVE_LEVEL (float) — default 0.05f
- AUTO_VOLUME_ACTIVE_LEVEL (float) — default 1.0f
- AUTO_VOLUME_FADE_MS (int) — default 600
- AUTO_VOLUME_POLL_MS (int) — default 50
- AUTO_VOLUME_DISABLE_WITH_MIDI (0/1) — default 1

Interfaces et modifications de fichiers
--------------------------------------
Fichiers modifiés (liste courte) :
- src/core/config.h
  - Ajout des defines de configuration auto-volume.
- src/core/context.h
  - Ajout des champs :
    - pthread_mutex_t imu_mutex;
    - float imu_x_filtered;
    - time_t last_imu_time;
    - int imu_has_value;
    - float auto_volume_current;
    - float auto_volume_target;
    - time_t auto_last_activity_time;
    - int auto_is_active;

- src/core/multithreading.c
  - udpThread : handling IMU packets (update imu_x_filtered and last_imu_time).
  - audioProcessingThread : periodic call to auto_volume_step().

- src/core/main.c
  - Création : gAutoVolumeInstance = auto_volume_create(&context);
  - Destruction : auto_volume_destroy(gAutoVolumeInstance) avant audio_Cleanup().

Nouveaux fichiers :
- src/core/auto_volume.h
  - API :
    AutoVolume *auto_volume_create(Context *ctx);
    void auto_volume_destroy(AutoVolume *av);
    void auto_volume_step(AutoVolume *av, unsigned int dt_ms);
  - extern AutoVolume *gAutoVolumeInstance;

- src/core/auto_volume.c
  - Implémentation décrite précédemment.

Extraits de code importants (conserver en anglais dans les fichiers sources)
------------------------------------------------------------------------
- udpThread IMU handling (extrait) :
```c
if (packet.type == IMU_DATA_HEADER) {
    struct packet_IMU *imu = (struct packet_IMU *)&packet;
    pthread_mutex_lock(&ctx->imu_mutex);
    float raw_x = imu->acc[0];
    if (!ctx->imu_has_value) {
        ctx->imu_x_filtered = raw_x;
        ctx->imu_has_value = 1;
    } else {
        ctx->imu_x_filtered = IMU_FILTER_ALPHA_X * raw_x +
                              (1.0f - IMU_FILTER_ALPHA_X) * ctx->imu_x_filtered;
    }
    ctx->last_imu_time = time(NULL);
    pthread_mutex_unlock(&ctx->imu_mutex);
    continue;
}
```

- auto_volume_step (extrait logique) :
```c
/* read under mutex */
pthread_mutex_lock(&ctx->imu_mutex);
float imu_x = ctx->imu_x_filtered;
time_t last_imu_time = ctx->last_imu_time;
int has = ctx->imu_has_value;
pthread_mutex_unlock(&ctx->imu_mutex);

/* detection */
int active = 0;
if (has && fabsf(imu_x) >= IMU_ACTIVE_THRESHOLD_X) active = 1;
else if (difftime(time(NULL), last_imu_time) <= IMU_INACTIVITY_TIMEOUT_S) active = 1;

/* smoothing */
float tau = (float)AUTO_VOLUME_FADE_MS;
float alpha = 1.0f - expf(- (float)dt_ms / fmaxf(1.0f, tau));
av->auto_volume_current += (target - av->auto_volume_current) * alpha;

/* apply */
if (gAudioSystem) gAudioSystem->setMasterVolume(av->auto_volume_current);
```

Impacts thread & synchronisation
--------------------------------
- udpThread écrit seulement 3 éléments sous le mutex : imu_x_filtered, last_imu_time, imu_has_value. Verrouillage bref.
- audioProcessingThread lit ces champs sous mutex également, puis effectue calculs hors du mutex (no heavy locking).
- setMasterVolume() est appelé depuis audioProcessingThread (non-RT) — pas de contention dans callback RT.
- Pas de modifications du callback RtAudio pour préserver la latence critique.

Plan de tests (unit / integration / terrain)
--------------------------------------------
1. Tests unitaires rapides
   - Simulation d'une suite de valeurs imu_x injectées : vérifier filtrage exponentiel et détection active/inactive.
   - Tester comportement du lissage pour différents dt_ms.

2. Tests d'intégration locaux
   - Compiler et lancer l'application.
   - Injecter paquets IMU simulés (outil UDP) et vérifier que gAudioSystem->setMasterVolume est appelé avec des valeurs attendues.
   - Vérifier qu'aucune API bloquante ou error n'apparaît dans les logs.

3. Calibration terrain (sur scanner réel)
   - Activer DEBUG_AUTO_VOLUME (printf dans auto_volume.c).
   - Relever imu_x_filtered en état repos pour estimer le bruit : définir IMU_ACTIVE_THRESHOLD_X légèrement au-dessus.
   - Jouer avec IMU_FILTER_ALPHA_X pour équilibrer réactivité vs stabilité.
   - Vérifier que le fade (AUTO_VOLUME_FADE_MS) est acceptable pour l'expérience utilisateur (éviter pops/cuts).
   - Scénarios : mouvements lents, manipulations rapides, perte de paquets IMU, présence d'un contrôleur MIDI.

Critères d'acceptation
----------------------
- Quand l'instrument est immobile pendant IMU_INACTIVITY_TIMEOUT_S, le volume doit atteindre une valeur proche d'AUTO_VOLUME_INACTIVE_LEVEL (±5%) après un fade de durée ≈ AUTO_VOLUME_FADE_MS.
- Quand l'instrument est manipulé, le volume doit revenir à AUTO_VOLUME_ACTIVE_LEVEL dans un temps proche de AUTO_VOLUME_FADE_MS.
- Aucune régression audible / glitches introduits par la fonctionnalité.
- Charge CPU raisonnable et absence d'erreurs liées aux mutex/threads.

Journal des modifications
-------------------------
- 2025-08-21 : v1.0 — Ajout de la fonctionnalité AutoVolume, modifications de Context, udpThread, audioProcessingThread, création auto_volume.{h,c}, entrées config.h.

Checklist d'implémentation (état courant)
-----------------------------------------
- [x] Ajouter defines dans config.h
- [x] Étendre Context et initialiser mutex dans main.c
- [x] Créer src/core/auto_volume.h et auto_volume.c
- [x] Modifier udpThread pour stocker imu_x_filtered
- [x] Instancier AutoVolume dans main.c
- [x] Appeler auto_volume_step périodiquement depuis audioProcessingThread
- [ ] Réaliser tests locaux et calibration sur site
- [ ] Ajuster seuils et paramètres selon mesures

Annexes : logs et exemples
--------------------------
Exemples de lines de debug (activer DEBUG_AUTO_VOLUME):
```
[AUTO_VOL] imu_x=0.012345 smoothed=0.014567 active=0 target=0.05 current=0.148 dt=50ms
[AUTO_VOL] imu_x=0.345678 smoothed=0.178900 active=1 target=1.00 current=0.472 dt=50ms
```

Dépannage
---------
- Si le comportement est trop sensible : augmenter IMU_ACTIVE_THRESHOLD_X et/ou augmenter IMU_FILTER_ALPHA_X.
- Si trop lent : diminuer IMU_FILTER_ALPHA_X (plus réactif) ou réduire AUTO_VOLUME_FADE_MS.
- Si le module interfère avec un contrôleur MIDI : activer AUTO_VOLUME_DISABLE_WITH_MIDI.
- Si pas de paquets IMU reçus (scanner débranché) : vérifier réseau UDP, bind, et logs du thread udpThread.

Prochaines améliorations possibles
---------------------------------
- Exposer une option runtime pour activer/désactiver auto-dim (CLI flag ou config file).
- Ajouter télémetrie/endpoint pour visualiser imu_x_filtered et volume en temps réel pour calibration.
- Support multi-axe (combine X,Y,Z) pour détection plus robuste.
- Ajouter hysteresis ou état "pré-alert" pour éviter toggles fréquents à la limite du seuil.

Fichiers clés (récapitulatif)
-----------------------------
- src/core/config.h — defines
- src/core/context.h — nouveaux champs et mutex
- src/core/multithreading.c — udpThread (IMU handling), audioProcessingThread (auto step)
- src/core/auto_volume.h / auto_volume.c — module
- src/core/main.c — création et destruction de l'instance

Contact et notes
----------------
- Document écrit par : dev assistant (Cline)
- Respect des conventions : documentation en français ; code et commentaires de code en anglais.
- Pour appliquer automatiquement un jeu de tests ou générer un patch, indiquer la commande souhaitée.
