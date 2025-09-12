#include "audio_c_api.h"
#include "auto_volume.h"
#include "config.h"
#include "context.h"
#include "display.h"
#include "dmx.h"
#include "error.h"
#include "image_debug.h"
#include "multithreading.h"
#include "synth_additive.h"
#include "synth_polyphonic.h" // Added for the new FFT synth mode
#include "udp.h"

// Declaration des fonctions MIDI externes (C-compatible)
extern void midi_Init(void);
extern void midi_Cleanup(void);
extern int midi_Connect(void);
extern void midi_SetupVolumeControl(void);
// Add declarations for the new C-API MIDI callback setters
extern void midi_set_note_on_callback(void (*callback)(int noteNumber,
                                                       int velocity));
extern void midi_set_note_off_callback(void (*callback)(int noteNumber));

#ifdef NO_SFML
// Si SFML est désactivé, fournir des stubs/déclarations anticipées pour les
// types SFML utilisés. Ceci est global pour main.c lorsque NO_SFML est défini.
typedef struct {
  unsigned long long microseconds;
} sfTime;

typedef struct sfClock sfClock; // Déclaration anticipée pour sfClock

// Stubs pour les fonctions sfClock si elles sont appelées (elles ne devraient
// pas l'être grâce aux gardes #ifndef NO_SFML plus bas) Mais le compilateur a
// besoin de les voir si PRINT_FPS est actif et qu'il essaie de compiler les
// appels. Les gardes autour des appels sfClock_* dans la boucle principale
// devraient empêcher cela. Cependant, pour que `sfClock *clock = NULL;` soit
// valide, la déclaration de type est suffisante. Les définitions de fonctions
// ici sont pour la complétude si jamais elles étaient appelées par erreur.
sfClock *sfClock_create(void) { return NULL; }
void sfClock_destroy(sfClock *clock) { (void)clock; }
sfTime sfClock_getElapsedTime(const sfClock *clock) {
  (void)clock;
  sfTime t = {0};
  return t;
}
void sfClock_restart(sfClock *clock) { (void)clock; }

// D'autres types SFML pourraient nécessiter des déclarations anticipées ici
// s'ils sont utilisés dans main.c en dehors des blocs #ifndef NO_SFML et ne
// sont pas déjà couverts par context.h/display.h Par exemple: typedef struct
// sfVideoMode sfVideoMode; // Si utilisé directement Mais sfVideoMode mode =
// ... est déjà dans un bloc #ifndef NO_SFML

#else // NO_SFML n'est PAS défini, donc SFML est activé
// Inclure les vrais en-têtes SFML
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#endif // NO_SFML

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

// Gestionnaire de signal global pour l'application
volatile sig_atomic_t app_running = 1;
Context *global_context =
    NULL; // Contexte global pour le gestionnaire de signaux

// Rendre le gestionnaire de signal visible pour les autres modules (comme
// dmx.c)
void signalHandler(int signal) {
  static volatile sig_atomic_t already_called = 0;

  // Éviter les appels récursifs au gestionnaire
  if (already_called) {
    // Si le gestionnaire est rappelé, c'est que l'utilisateur
    // insiste avec les Ctrl+C, donc on force vraiment la sortie
    kill(getpid(), SIGKILL);
    return;
  }

  already_called = 1;

  (void)signal;
  printf("\nSignal d'arrêt reçu. Arrêt en cours...\n");
  fflush(stdout);

  // Mettre à jour les flags d'arrêt
  app_running = 0;
  if (global_context) {
    global_context->running = 0;
    if (global_context->dmxCtx) {
      global_context->dmxCtx->running = 0;
    }
    // Cleanup UDP socket immediately to prevent port conflicts
    udp_cleanup(global_context->socket);
  }
  keepRunning = 0; // Variable globale du module DMX

  // Forcer la terminaison immédiate sans attendre les threads
  printf("\nForced exit!\n");
  fflush(stdout);

  // Tuer le processus avec SIGKILL (ne peut pas être ignoré ou bloqué)
  kill(getpid(), SIGKILL);
}

int main(int argc, char **argv) {
  // Configurez le gestionnaire de signaux SIGINT (Ctrl+C)
  signal(SIGINT, signalHandler);
  /* Parse command-line arguments */
  int use_dmx = 1;                 // Par défaut, on active le DMX
  int silent_dmx = 0;              // Par défaut, on affiche les messages DMX
  const char *dmx_port = DMX_PORT; // Port DMX par défaut
  int list_audio_devices = 0;      // Afficher les périphériques audio
  int audio_device_id = -1;        // -1 = utiliser le périphérique par défaut
  char *audio_device_name = NULL;  // Nom du périphérique audio recherché
  int use_sfml_window = 0; // Par défaut, pas de fenêtre SFML en mode CLI

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Sp3ctra - Real-time audio synthesis application\n\n");
      printf("Usage: %s [OPTIONS]\n\n", argv[0]);
      printf("OPTIONS:\n");
      printf("  --help, -h               Show this help message\n");
      printf("  --display                Enable visual scanner display\n");
      printf("  --list-audio-devices     List available audio devices and exit\n");
      printf("  --audio-device=<ID>      Use specific audio device ID\n");
      printf("  --no-dmx                 Disable DMX lighting output\n");
      printf("  --dmx-port=<PORT>        Specify DMX serial port (default: %s)\n",
             DMX_PORT);
      printf("  --silent-dmx             Suppress DMX error messages\n");
      printf("  --test-tone              Enable test tone mode (440Hz)\n");
      printf("  --debug-image            Enable image transformation debug visualization\n");
      printf("\nExamples:\n");
      printf("  %s --audio-device=3                 # Use audio device 3\n",
             argv[0]);
      printf("  %s --list-audio-devices             # List all audio devices\n",
             argv[0]);
      printf("  %s --no-dmx                         # Run without DMX\n",
             argv[0]);
      printf("  %s --display --audio-device=1       # Run with visual display\n",
             argv[0]);
      printf("\nFor Pi Module 5 optimization, use: "
             "./launch_cisynth_optimized.sh\n");
      return EXIT_SUCCESS;
    // Option --cli supprimée car redondante (mode CLI est le mode par défaut)
    } else if (strcmp(argv[i], "--sfml-window") == 0 || strcmp(argv[i], "--show-display") == 0 || strcmp(argv[i], "--display") == 0) {
      use_sfml_window = 1;
      printf("Visual scanner display enabled\n");
    } else if (strcmp(argv[i], "--no-dmx") == 0) {
      use_dmx = 0;
      printf("DMX disabled\n");
    } else if (strncmp(argv[i], "--dmx-port=", 11) == 0) {
      dmx_port = argv[i] + 11;
      printf("Using DMX port: %s\n", dmx_port);
    } else if (strcmp(argv[i], "--silent-dmx") == 0) {
      silent_dmx = 1;
      printf("DMX messages silenced\n");
    } else if (strcmp(argv[i], "--list-audio-devices") == 0) {
      list_audio_devices = 1;
      printf("Will list audio devices\n");
    } else if (strncmp(argv[i], "--audio-device=", 15) == 0) {
      const char *device_param = argv[i] + 15;
      
      // Check if it's a numeric ID or device name
      char *endptr;
      long device_id_long = strtol(device_param, &endptr, 10);
      
      if (*endptr == '\0' && device_id_long >= 0) {
        // It's a valid numeric ID
        audio_device_id = (int)device_id_long;
        printf("Using audio device ID: %d\n", audio_device_id);
      } else {
        // It's a device name (could contain spaces)
        audio_device_name = strdup(device_param);
        printf("Using audio device name: '%s'\n", audio_device_name);
      }
    } else if (strcmp(argv[i], "--test-tone") == 0) {
      printf("🎵 Test tone mode enabled (440Hz)\n");
      // Enable minimal callback mode for testing
      setMinimalCallbackMode(1);
    } else if (strcmp(argv[i], "--debug-image") == 0) {
      printf("🔧 Image transformation debug enabled\n");
      // Enable runtime image debug
      image_debug_enable_runtime(1);
    } else {
      printf("Unknown option: %s\n", argv[i]);
      printf("Use --help for usage information\n");
      return EXIT_FAILURE;
    }
  }

  int dmxFd = -1;
  if (use_dmx) {
#ifdef USE_DMX
    dmxFd = init_Dmx(dmx_port, silent_dmx);
    if (dmxFd < 0) {
      if (!silent_dmx) {
        printf("Failed to initialize DMX. Continuing without DMX support.\n");
      }
      // Si l'initialisation DMX a échoué, on désactive le DMX complètement
      use_dmx = 0;
    }
#endif
  }

  DMXContext *dmxCtx = malloc(sizeof(DMXContext));
  if (dmxCtx == NULL) {
    perror("Error allocating DMXContext");
    close(dmxFd);
    // return EXIT_FAILURE;
  }
  
  // Initialize DMXContext with flexible system
  dmxCtx->fd = dmxFd;
  dmxCtx->running = 1;
  dmxCtx->colorUpdated = 0;
  dmxCtx->spots = NULL;
  dmxCtx->num_spots = 0;
  dmxCtx->use_libftdi = 0;
#ifdef __linux__
  dmxCtx->ftdi = NULL;
#endif
  pthread_mutex_init(&dmxCtx->mutex, NULL);
  pthread_cond_init(&dmxCtx->cond, NULL);

  // Initialize flexible DMX configuration
  if (use_dmx) {
    printf("🔧 Initializing flexible DMX configuration...\n");
    dmxCtx->spots = malloc(DMX_NUM_SPOTS * sizeof(DMXSpot));
    if (dmxCtx->spots == NULL) {
      printf("❌ Failed to allocate DMX spots array\n");
      use_dmx = 0;
    } else {
      dmxCtx->num_spots = DMX_NUM_SPOTS;
      // Generate channel mapping using flexible system
      dmx_generate_channel_mapping(dmxCtx->spots, DMX_NUM_SPOTS, DMX_SPOT_TYPE, DMX_START_CHANNEL);
      printf("✅ DMX flexible system initialized: %d spots, type=%d, start_channel=%d\n",
             DMX_NUM_SPOTS, DMX_SPOT_TYPE, DMX_START_CHANNEL);
    }
  }

  /* Initialize CSFML */
  sfRenderWindow *window = NULL;
#ifndef NO_SFML
  // Tout ce bloc ne s'exécute que si SFML est activé
  sfVideoMode mode = {WINDOWS_WIDTH, WINDOWS_HEIGHT, 32};

  // Créer la fenêtre SFML uniquement si l'option est activée
  if (use_sfml_window) {
    window = sfRenderWindow_create(mode, "Sp3ctra SFML Viewer",
                                   sfResize | sfClose, NULL);
    if (!window) {
      perror("Error creating CSFML window");
      close(dmxCtx->fd);
      free(dmxCtx);
      return EXIT_FAILURE;
    }
  }
#endif // NO_SFML

  /* Initialize UDP and Audio */
  struct sockaddr_in si_other, si_me;

  // Traiter les options audio AVANT d'initialiser le système audio
  if (list_audio_devices) {
    // Initialiser temporairement l'audio juste pour lister les périphériques
    audio_Init();
    printAudioDevices();
    // Si --list-audio-devices est spécifié, on nettoie et on quitte,
    // peu importe les autres arguments.
    printf("Audio device listing complete. Exiting.\n");
    audio_Cleanup();
    midi_Cleanup();          // Assurer le nettoyage de MIDI aussi
    if (dmxCtx) {            // Vérifier si dmxCtx a été alloué
      if (dmxCtx->fd >= 0) { // Vérifier si le fd est valide avant de fermer
        close(dmxCtx->fd);
      }
      pthread_mutex_destroy(&dmxCtx->mutex); // Nettoyer mutex et cond
      pthread_cond_destroy(&dmxCtx->cond);
      free(dmxCtx);
      dmxCtx = NULL; // Éviter double free ou utilisation après libération
    }
    return EXIT_SUCCESS;
  }

  // Configurer le périphérique audio AVANT l'initialisation si spécifié
  if (audio_device_id >= 0) {
    setRequestedAudioDevice(audio_device_id);
    printf("Périphérique audio %d configuré pour l'initialisation.\n",
           audio_device_id);
  } else if (audio_device_name != NULL) {
    setRequestedAudioDeviceName(audio_device_name);
    printf("Périphérique audio '%s' configuré pour l'initialisation.\n",
           audio_device_name);
  }

  // Initialiser l'audio (RtAudio) avec le bon périphérique
  audio_Init();

  // Determine synthesis modes based on configuration
  int enable_polyphonic_synth = 1;
  int enable_additive_synth = 1;
  int enable_midi = 1;
  int midi_connected = 0;

  // Check manual disable flags (highest priority)
#ifdef DISABLE_POLYPHONIC
  enable_polyphonic_synth = 0;
  printf("Polyphonic synthesis DISABLED by configuration\n");
#endif

#ifdef DISABLE_ADDITIVE
  enable_additive_synth = 0;
  printf("ADDITIVE synthesis DISABLED by configuration\n");
#endif

#if !ENABLE_MIDI_POLLING
  enable_midi = 0;
  printf("MIDI polling DISABLED by configuration\n");
#endif

  // Initialize MIDI if enabled
  if (enable_midi) {
    midi_Init();
    midi_SetupVolumeControl();

    // Try to connect to MIDI controller
    midi_connected = midi_Connect();
    if (midi_connected) {
      printf("MIDI: Controller connected\n");
      // Setup note callbacks if MIDI connected successfully
      midi_set_note_on_callback(synth_polyphonic_note_on);
      midi_set_note_off_callback(synth_polyphonic_note_off);
      printf("MIDI: Note On/Off callbacks for synth_polyphonic registered via "
             "C API.\n");
    } else {
      printf("MIDI: No controller found\n");
    }
  }

  // Check automatic polyphonic disable based on MIDI presence
#if AUTO_DISABLE_POLYPHONIC_WITHOUT_MIDI
  if (!midi_connected && enable_polyphonic_synth) {
    enable_polyphonic_synth = 0;
    printf(
        "Polyphonic synthesis AUTO-DISABLED - no MIDI controller detected\n");
  }
#endif

  // Display final synthesis configuration
  printf("========== SYNTHESIS CONFIGURATION ==========\n");
  printf("ADDITIVE synthesis: %s\n",
         enable_additive_synth ? "ENABLED" : "DISABLED");
  printf("POLYPHONIC synthesis:  %s\n",
         enable_polyphonic_synth ? "ENABLED" : "DISABLED");
  printf("MIDI polling:   %s\n", enable_midi ? "ENABLED" : "DISABLED");
  if (enable_midi) {
    printf("MIDI connected: %s\n", midi_connected ? "YES" : "NO");
  }
  printf("============================================\n");

  synth_IfftInit();
  synth_polyphonicMode_init(); // Initialize the polyphonic synth mode
  display_Init(window);
  // visual_freeze_init(); // Removed: Old visual-only freeze
  synth_data_freeze_init();         // Initialize synth data freeze feature
  displayable_synth_buffers_init(); // Initialize displayable synth buffers

  int s = udp_Init(&si_other, &si_me);
  if (s < 0) {
    perror("Error initializing UDP");
#ifndef NO_SFML
    if (window)
      sfRenderWindow_destroy(window);
#endif
    close(dmxCtx->fd);
    free(dmxCtx);
    return EXIT_FAILURE;
  }

  int status = startAudioUnit(); // RtAudio renvoie un int maintenant
  if (status != 0) {
    printf("Erreur lors du démarrage audio: %d\n", status);
  }

  /* Create double buffer */
  DoubleBuffer db;
  initDoubleBuffer(&db);

  /* Create new audio image buffers for continuous audio processing */
  AudioImageBuffers audioImageBuffers;
  if (audio_image_buffers_init(&audioImageBuffers) != 0) {
    printf("Erreur lors de l'initialisation des buffers audio-image\n");
#ifndef NO_SFML
    if (window)
      sfRenderWindow_destroy(window);
#endif
    close(dmxCtx->fd);
    free(dmxCtx);
    return EXIT_FAILURE;
  }

  /* Build global context structure */
  Context context = {0};
  context.window = window;
  context.socket = s;
  context.si_other = &si_other;
  context.si_me = &si_me;
  context.audioData = NULL;   // RtAudio gère maintenant le buffer audio
  context.doubleBuffer = &db; // Legacy double buffer (for display)
  context.audioImageBuffers =
      &audioImageBuffers; // New dual buffer system for audio
  context.dmxCtx = dmxCtx;
  context.running = 1; // Flag de terminaison pour le contexte

  // Sauvegarde du contexte pour le gestionnaire de signaux
  global_context = &context;

  /* Initialize auto-volume controller (reads IMU X from UDP thread and
     adjusts master volume). Instance is stored in auto_volume.c as
     gAutoVolumeInstance. */
  printf("[INIT] Initializing auto-volume controller...\n");
  printf("[INIT] Auto-volume config: threshold=%.3f, timeout=%ds, fade=%dms\n",
         IMU_ACTIVE_THRESHOLD_X, IMU_INACTIVITY_TIMEOUT_S, AUTO_VOLUME_FADE_MS);
  printf("[INIT] Volume levels: active=%.3f, inactive=%.3f\n",
         AUTO_VOLUME_ACTIVE_LEVEL, AUTO_VOLUME_INACTIVE_LEVEL);

  gAutoVolumeInstance = auto_volume_create(&context);
  if (!gAutoVolumeInstance) {
    printf("[INIT] ERROR: Failed to initialize auto-volume controller\n");
  } else {
    printf("[INIT] Auto-volume controller initialized successfully\n");
  }

  /* Create textures and sprites for rendering in main thread */
  sfTexture *backgroundTexture = NULL;
  sfTexture *foregroundTexture = NULL;
  sfSprite *backgroundSprite = NULL;
  sfSprite *foregroundSprite = NULL;

#ifndef NO_SFML
  // Ce bloc ne s'exécute que si SFML est activé
  // Créer les textures uniquement si la fenêtre SFML est demandée
  if (use_sfml_window) {
    backgroundTexture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    foregroundTexture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    backgroundSprite = sfSprite_create();
    foregroundSprite = sfSprite_create();
    sfSprite_setTexture(backgroundSprite, backgroundTexture, sfTrue);
    sfSprite_setTexture(foregroundSprite, foregroundTexture, sfTrue);
  }
#endif // NO_SFML

  /* Create threads for UDP, Audio, and DMX (pas de thread d'affichage) */
  pthread_t udpThreadId, audioThreadId, dmxThreadId,
      fftSynthThreadId; // Added fftSynthThreadId

#ifdef USE_DMX
  if (use_dmx && dmxFd >= 0) {
    if (pthread_create(&dmxThreadId, NULL, dmxSendingThread,
                       (void *)context.dmxCtx) != 0) {
      perror("Error creating DMX thread");
      close(dmxCtx->fd);
      free(dmxCtx);
#ifndef NO_SFML
      if (window)
        sfRenderWindow_destroy(window);
#endif
      return EXIT_FAILURE;
    }
  }
#endif
  if (pthread_create(&udpThreadId, NULL, udpThread, (void *)&context) != 0) {
    perror("Error creating UDP thread");
#ifndef NO_SFML
    if (window)
      sfRenderWindow_destroy(window);
#endif
    return EXIT_FAILURE;
  }
  if (pthread_create(&audioThreadId, NULL, audioProcessingThread,
                     (void *)&context) != 0) {
    perror("Error creating audio processing thread");
#ifndef NO_SFML
    if (window)
      sfRenderWindow_destroy(window);
#endif
    return EXIT_FAILURE;
  }

  struct sched_param param;
  param.sched_priority = 50; // Priorité plus modérée pour le Jetson Nano
  pthread_setschedparam(audioThreadId, SCHED_RR, &param);

  // Create and start the polyphonic synth thread conditionally
  int polyphonic_thread_created = 0;
  if (enable_polyphonic_synth) {
    if (pthread_create(&fftSynthThreadId, NULL,
                       synth_polyphonicMode_thread_func,
                       (void *)&context) != 0) {
      perror("Error creating polyphonic synth thread");
      // Consider cleanup for other threads if this fails mid-startup
#ifndef NO_SFML
      if (window)
        sfRenderWindow_destroy(window);
#endif
      return EXIT_FAILURE;
    }
    polyphonic_thread_created = 1;
    printf("Polyphonic synthesis thread started successfully\n");
    // Optionally set scheduling parameters for fftSynthThreadId as well if
    // needed
  } else {
    printf("Polyphonic synthesis thread NOT created (disabled by "
           "configuration)\n");
  }

  /* Main loop (gestion des événements et rendu) */
  // sfEvent event; // Unused variable
  sfClock *clock = NULL;
#ifndef NO_SFML
  // Créer l'horloge uniquement si SFML est utilisé
  clock = sfClock_create();
#endif // NO_SFML

#ifdef PRINT_FPS
  unsigned int frameCount = 0;
#endif
  int running = 1;

  /* Boucle principale */
  printf("========================================================\n");
  printf("Application running.\n");
  if (use_sfml_window) {
    printf("Visual scanner display enabled.\n");
  } else {
    printf("No visual display (use --display to enable).\n");
  }
  printf("Press Ctrl+C to stop the application.\n");
  printf("========================================================\n");
  fflush(stdout); // S'assurer que tout est affiché immédiatement

  /* Boucle principale */
  uint8_t local_main_R[CIS_MAX_PIXELS_NB]; // Buffers locaux pour DMX
  uint8_t local_main_G[CIS_MAX_PIXELS_NB];
  uint8_t local_main_B[CIS_MAX_PIXELS_NB];
  int process_this_frame_main_loop;

  while (running && context.running && app_running) {
    process_this_frame_main_loop = 0;
    /* Gérer les événements SFML si la fenêtre est active et si SFML est compilé
     */
#ifndef NO_SFML
    if (use_sfml_window && window) {
      sfEvent event;
      while (sfRenderWindow_pollEvent(window, &event)) {
        if (event.type == sfEvtClosed) {
          sfRenderWindow_close(window);
          context.running = 0;
          dmxCtx->running = 0;
        }
      }
    }
#else
    // Si NO_SFML est défini, mais que use_sfml_window est vrai,
    // cela indique une incohérence de configuration.
    // On pourrait ajouter un avertissement ici, ou simplement ne rien faire.
    if (use_sfml_window && window) {
      // Ce bloc ne devrait pas être atteint si NO_SFML est défini et que window
      // est NULL. Si window est non-NULL ici, c'est une erreur de logique dans
      // la création de la fenêtre.
    }
#endif // NO_SFML

    /* Vérifier si le double buffer contient de nouvelles données */
    pthread_mutex_lock(&db.mutex);
    if (db.dataReady) {
      // Copier les données pour DMX pendant que le mutex est verrouillé
      memcpy(local_main_R, db.processingBuffer_R, CIS_MAX_PIXELS_NB);
      memcpy(local_main_G, db.processingBuffer_G, CIS_MAX_PIXELS_NB);
      memcpy(local_main_B, db.processingBuffer_B, CIS_MAX_PIXELS_NB);
      db.dataReady = 0; // Marquer comme consommé par la boucle principale
      process_this_frame_main_loop = 1;
    }
    pthread_mutex_unlock(&db.mutex);

    if (process_this_frame_main_loop) {
      /* Rendu de la nouvelle ligne si SFML est activé */
      if (use_sfml_window && window) {
        // Lock mutex before accessing displayable synth buffers
        pthread_mutex_lock(&g_displayable_synth_mutex);
        printImageRGB(window, g_displayable_synth_R, g_displayable_synth_G,
                      g_displayable_synth_B, backgroundTexture,
                      foregroundTexture); // Utilise les données de synth.c
        pthread_mutex_unlock(&g_displayable_synth_mutex);
      }

      /* Calcul de la couleur moyenne et mise à jour du contexte DMX */
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

#ifdef PRINT_FPS
      frameCount++; // Compter chaque trame traitée
#endif
    }

#ifdef PRINT_FPS
    float elapsedTime = 0.0f;
#ifndef NO_SFML
    if (clock) { // Vérifier si clock a été initialisé
      elapsedTime = sfClock_getElapsedTime(clock).microseconds / 1000000.0f;
      if (elapsedTime >= 1.0f) {
        float fps = frameCount / elapsedTime;
        (void)fps; // Mark fps as used to silence warning if printf is commented
        // printf("Processing rate: %.2f FPS\n", fps); // Supprimé ou commenté
        sfClock_restart(clock);
        frameCount = 0; // Réinitialiser frameCount ici
      }
    }
#else
    // Alternative pour le timing si NO_SFML est défini et que PRINT_FPS est
    // actif (nécessiterait une implémentation de clock non-SFML, comme celles
    // au début du fichier) Pour l'instant, on ne fait rien pour le FPS si
    // NO_SFML.
    (void)elapsedTime; // Supprimer l'avertissement unused
#endif // NO_SFML
#endif // PRINT_FPS

    /* Petite pause pour limiter la charge CPU */
    usleep(100);
  }

#ifndef NO_SFML
  if (clock) {
    sfClock_destroy(clock);
  }
#endif // NO_SFML

  printf("\nTerminaison des threads et nettoyage...\n");
  /* Terminaison et synchronisation */
  context.running = 0;
  dmxCtx->running = 0;
  keepRunning = 0; // Variable globale du module DMX

  pthread_join(udpThreadId, NULL);
  pthread_join(audioThreadId, NULL);

  // Join the polyphonic synth thread only if it was created
  if (polyphonic_thread_created) {
    pthread_join(fftSynthThreadId, NULL);
    printf("Polyphonic synthesis thread terminated\n");
  }

#ifdef USE_DMX
  if (use_dmx && dmxFd >= 0) {
    pthread_join(dmxThreadId, NULL);
  }
#endif

  // Nettoyage MIDI et audio
  // visual_freeze_cleanup(); // Removed: Old visual-only freeze
  displayable_synth_buffers_cleanup(); // Cleanup displayable synth buffers
  synth_data_freeze_cleanup();         // Cleanup synth data freeze resources
  cleanupDoubleBuffer(&db);            // Cleanup DoubleBuffer resources
  audio_image_buffers_cleanup(
      &audioImageBuffers);     // Cleanup new audio image buffers
  udp_cleanup(context.socket); // Cleanup UDP socket to prevent port conflicts
  midi_Cleanup();

  /* Destroy auto-volume controller (if created) before audio cleanup */
  if (gAutoVolumeInstance) {
    auto_volume_destroy(gAutoVolumeInstance);
    gAutoVolumeInstance = NULL;
  }

  audio_Cleanup(); // Nettoyage de RtAudio

  /* Nettoyage des ressources graphiques */
#ifndef NO_SFML
  // Ce bloc ne s'exécute que si SFML est activé
  // Nettoyer seulement si la fenêtre SFML était utilisée
  if (use_sfml_window &&
      window) { // window ne sera non-NULL que si use_sfml_window était vrai ET
                // la création a réussi
    if (backgroundTexture)
      sfTexture_destroy(backgroundTexture);
    if (foregroundTexture)
      sfTexture_destroy(foregroundTexture);
    if (backgroundSprite)
      sfSprite_destroy(backgroundSprite);
    if (foregroundSprite)
      sfSprite_destroy(foregroundSprite);
    sfRenderWindow_destroy(window); // window est garanti non-NULL ici
  }
#endif // NO_SFML

  return 0;
}
