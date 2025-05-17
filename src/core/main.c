#include "audio_c_api.h"
#include "config.h"
#include "context.h"
#include "display.h"
#include "dmx.h"
#include "error.h"
#include "multithreading.h"
#include "synth.h"
#include "synth_fft.h" // Added for the new FFT synth mode
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

#ifdef __LINUX__
// Vérifier si SFML est désactivé
#ifdef NO_SFML
// Structures de base pour les horloges
typedef struct {
  unsigned long long microseconds;
} sfTime;

typedef struct sfClock sfClock;
sfClock *sfClock_create(void) { return NULL; }
void sfClock_destroy(sfClock *clock) { (void)clock; }
sfTime sfClock_getElapsedTime(const sfClock *clock) {
  (void)clock;
  sfTime time = {0};
  return time;
}
void sfClock_restart(sfClock *clock) { (void)clock; }
#else
// SFML disponible sur Linux
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#endif
#else
// macOS a toujours SFML
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#endif
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
  int use_sfml_window = 0; // Par défaut, pas de fenêtre SFML en mode CLI

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--cli") == 0) {
      printf(
          "Running in CLI mode (no GUI window unless --sfml-window is used)\n");
#ifndef CLI_MODE
#define CLI_MODE
#endif
    } else if (strcmp(argv[i], "--sfml-window") == 0) {
      use_sfml_window = 1;
      printf("SFML window enabled (visual display will be shown)\n");
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
      audio_device_id = atoi(argv[i] + 15);
      printf("Using audio device: %d\n", audio_device_id);
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
  dmxCtx->fd = dmxFd;
  dmxCtx->running = 1;
  dmxCtx->colorUpdated = 0;
  pthread_mutex_init(&dmxCtx->mutex, NULL);
  pthread_cond_init(&dmxCtx->cond, NULL);

  /* Initialize CSFML */
  sfVideoMode mode = {WINDOWS_WIDTH, WINDOWS_HEIGHT, 32};
  sfRenderWindow *window = NULL;

#ifndef CLI_MODE
  // Mode GUI normal, toujours créer la fenêtre
  window =
      sfRenderWindow_create(mode, "CSFML Viewer", sfResize | sfClose, NULL);
  if (!window) {
    perror("Error creating CSFML window");
    close(dmxCtx->fd);
    free(dmxCtx);
    return EXIT_FAILURE;
  }
#else
  // Mode CLI, mais avec option de fenêtre SFML si demandée
  if (use_sfml_window) {
    window = sfRenderWindow_create(mode, "CISYNTH SFML Viewer",
                                   sfResize | sfClose, NULL);
    if (!window) {
      perror("Error creating CSFML window");
      close(dmxCtx->fd);
      free(dmxCtx);
      return EXIT_FAILURE;
    }
  }
#endif

  /* Initialize UDP and Audio */
  struct sockaddr_in si_other, si_me;

  // Initialiser l'audio (RtAudio)
  audio_Init();

  // Initialiser le contrôleur MIDI
  midi_Init();

  // Configurer le callback de volume MIDI
  midi_SetupVolumeControl();

  // Essayer de connecter au Launchkey Mini
  if (midi_Connect()) {
    printf("MIDI: Launchkey Mini connected\n");
    // Setup note callbacks if MIDI connected successfully
    // No need to check gMidiController here, the C-wrappers will do it.
    midi_set_note_on_callback(synth_fft_note_on);
    midi_set_note_off_callback(synth_fft_note_off);
    printf("MIDI: Note On/Off callbacks for synth_fft registered via C API.\n");
    // The following if(gMidiController) block is removed as it's C++ specific
    // and was causing compilation errors in C.
  } else {
    printf("MIDI: No Launchkey Mini device found\n");
    // Note: nous ne pouvons pas afficher la liste des périphériques ici car
    // nous n'avons pas accès direct à l'objet C++ depuis C
  }

  // Traiter les options audio
  if (list_audio_devices) {
    printAudioDevices();
    // Si on a demandé uniquement la liste des périphériques, on quitte
    if (argc == 2) {
      audio_Cleanup();
      close(dmxCtx->fd);
      free(dmxCtx);
      return EXIT_SUCCESS;
    }
  }

  // Sélectionner le périphérique audio si spécifié
  if (audio_device_id >= 0) {
    if (!setAudioDevice(audio_device_id)) {
      printf("Erreur: Impossible d'utiliser le périphérique audio %d. "
             "Utilisation du périphérique par défaut.\n",
             audio_device_id);
    } else {
      printf("Périphérique audio %d sélectionné avec succès.\n",
             audio_device_id);
    }
  }

  synth_IfftInit();
  synth_fftMode_init(); // Initialize the new FFT synth mode
  display_Init(window);
  // visual_freeze_init(); // Removed: Old visual-only freeze
  synth_data_freeze_init();         // Initialize synth data freeze feature
  displayable_synth_buffers_init(); // Initialize displayable synth buffers

  int s = udp_Init(&si_other, &si_me);
  if (s < 0) {
    perror("Error initializing UDP");
    sfRenderWindow_destroy(window);
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

  /* Build global context structure */
  Context context = {0};
  context.window = window;
  context.socket = s;
  context.si_other = &si_other;
  context.si_me = &si_me;
  context.audioData = NULL; // RtAudio gère maintenant le buffer audio
  context.doubleBuffer = &db;
  context.dmxCtx = dmxCtx;
  context.running = 1; // Flag de terminaison pour le contexte

  // Sauvegarde du contexte pour le gestionnaire de signaux
  global_context = &context;

  /* Create textures and sprites for rendering in main thread */
  sfTexture *backgroundTexture = NULL;
  sfTexture *foregroundTexture = NULL;
  sfSprite *backgroundSprite = NULL;
  sfSprite *foregroundSprite = NULL;

#ifndef CLI_MODE
  // Mode GUI normal, toujours créer les textures
  backgroundTexture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
  foregroundTexture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
  backgroundSprite = sfSprite_create();
  foregroundSprite = sfSprite_create();
  sfSprite_setTexture(backgroundSprite, backgroundTexture, sfTrue);
  sfSprite_setTexture(foregroundSprite, foregroundTexture, sfTrue);
#else
  // Mode CLI, mais avec option de fenêtre SFML si demandée
  if (use_sfml_window) {
    backgroundTexture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    foregroundTexture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    backgroundSprite = sfSprite_create();
    foregroundSprite = sfSprite_create();
    sfSprite_setTexture(backgroundSprite, backgroundTexture, sfTrue);
    sfSprite_setTexture(foregroundSprite, foregroundTexture, sfTrue);
  }
#endif

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
      sfRenderWindow_destroy(window);
      return EXIT_FAILURE;
    }
  }
#endif
  if (pthread_create(&udpThreadId, NULL, udpThread, (void *)&context) != 0) {
    perror("Error creating UDP thread");
    sfRenderWindow_destroy(window);
    return EXIT_FAILURE;
  }
  if (pthread_create(&audioThreadId, NULL, audioProcessingThread,
                     (void *)&context) != 0) {
    perror("Error creating audio processing thread");
    sfRenderWindow_destroy(window);
    return EXIT_FAILURE;
  }

  struct sched_param param;
  param.sched_priority = 50; // Priorité plus modérée pour le Jetson Nano
  pthread_setschedparam(audioThreadId, SCHED_RR, &param);

  // Create and start the FFT synth thread
  if (pthread_create(&fftSynthThreadId, NULL, synth_fftMode_thread_func,
                     (void *)&context) != 0) {
    perror("Error creating FFT synth thread");
    // Consider cleanup for other threads if this fails mid-startup
    sfRenderWindow_destroy(window);
    return EXIT_FAILURE;
  }
  // Optionally set scheduling parameters for fftSynthThreadId as well if needed

  /* Main loop (gestion des événements et rendu) */
  // sfEvent event; // Unused variable
  sfClock *clock = sfClock_create();
#ifdef PRINT_FPS
  unsigned int frameCount = 0;
#endif
  int running = 1;

#ifdef CLI_MODE
  /* En mode CLI, gérer les signaux pour l'arrêt propre (CTRL+C) */
  printf("========================================================\n");
  printf("Application running in CLI mode.\n");
  if (use_sfml_window) {
    printf("SFML window enabled for visual display.\n");
  } else {
    printf("No visual display (use --sfml-window to enable).\n");
  }
  printf("Press Ctrl+C to stop the application.\n");
  printf("========================================================\n");
  fflush(stdout); // S'assurer que tout est affiché immédiatement

  /* Boucle principale pour le mode CLI */
  uint8_t local_main_R[CIS_MAX_PIXELS_NB]; // Buffers locaux pour DMX
  uint8_t local_main_G[CIS_MAX_PIXELS_NB];
  uint8_t local_main_B[CIS_MAX_PIXELS_NB];
  int process_this_frame_main_loop;

  while (running && context.running && app_running) {
    process_this_frame_main_loop = 0;
    /* Gérer les événements SFML si la fenêtre est active */
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
      DMXSpot zoneSpots[DMX_NUM_SPOTS];
      computeAverageColorPerZone(local_main_R, local_main_G, local_main_B,
                                 CIS_MAX_PIXELS_NB, zoneSpots);

      pthread_mutex_lock(&dmxCtx->mutex);
      memcpy(dmxCtx->spots, zoneSpots, sizeof(zoneSpots));
      dmxCtx->colorUpdated = 1;
      pthread_cond_signal(&dmxCtx->cond);
      pthread_mutex_unlock(&dmxCtx->mutex);

#ifdef PRINT_FPS
      frameCount++; // Compter chaque trame traitée
#endif
    }

#ifdef PRINT_FPS
    float elapsedTime = 0.0f;
    /* Calcul du temps écoulé et affichage du taux de rafraîchissement */
    elapsedTime = sfClock_getElapsedTime(clock).microseconds / 1000000.0f;
    if (elapsedTime >= 1.0f) {
      float fps = frameCount / elapsedTime;
      (void)fps; // Mark fps as used to silence warning if printf is commented
      // printf("Processing rate: %.2f FPS\n", fps); // Supprimé ou commenté
      sfClock_restart(clock);
      frameCount = 0; // Réinitialiser frameCount ici
    }
#endif

    /* Petite pause pour limiter la charge CPU */
    usleep(100);
  }
#else
  /* Boucle principale avec affichage graphique */
  // uint8_t local_main_R,G,B sont déjà déclarés plus haut si CLI_MODE est
  // défini. S'il n'est pas défini, il faut les déclarer ici. Pour simplifier,
  // on les sort de la condition #ifdef. Déjà fait en dehors de la boucle
  // CLI_MODE.

  while (sfRenderWindow_isOpen(window)) {
    process_this_frame_main_loop = 0;
    sfEvent event; // Déplacé ici car non utilisé si CLI_MODE n'est pas défini
                   // et event n'est pas utilisé
    /* Gestion des événements dans le thread principal */
    while (sfRenderWindow_pollEvent(window, &event)) {
      if (event.type == sfEvtClosed) {
        sfRenderWindow_close(window);
        context.running = 0;
        dmxCtx->running = 0;
      }
    }

    /* Vérifier si le double buffer contient de nouvelles données */
    pthread_mutex_lock(&db.mutex);
    if (db.dataReady) {
      memcpy(local_main_R, db.processingBuffer_R, CIS_MAX_PIXELS_NB);
      memcpy(local_main_G, db.processingBuffer_G, CIS_MAX_PIXELS_NB);
      memcpy(local_main_B, db.processingBuffer_B, CIS_MAX_PIXELS_NB);
      db.dataReady = 0;
      process_this_frame_main_loop = 1;
    }
    pthread_mutex_unlock(&db.mutex);

    if (process_this_frame_main_loop) {
      /* Rendu de la nouvelle ligne à partir du buffer */
      pthread_mutex_lock(&g_displayable_synth_mutex);
      printImageRGB(window, g_displayable_synth_R, g_displayable_synth_G,
                    g_displayable_synth_B,
                    backgroundTexture, // Utilise les données de synth.c
                    foregroundTexture);
      pthread_mutex_unlock(&g_displayable_synth_mutex);

      /* Calcul de la couleur moyenne et mise à jour du contexte DMX */
      // DMX utilise les données copiées local_main_R,G,B (qui sont les données
      // live de db.processingBuffer)
      DMXSpot zoneSpots[DMX_NUM_SPOTS];
      computeAverageColorPerZone(local_main_R, local_main_G, local_main_B,
                                 CIS_MAX_PIXELS_NB, zoneSpots);

      pthread_mutex_lock(&dmxCtx->mutex);
      memcpy(dmxCtx->spots, zoneSpots, sizeof(zoneSpots));
      dmxCtx->colorUpdated = 1;
      pthread_cond_signal(&dmxCtx->cond);
      pthread_mutex_unlock(&dmxCtx->mutex);

#ifdef PRINT_FPS
      frameCount++; // Compter chaque image affichée
#endif
    }

#ifdef PRINT_FPS
    float elapsedTime = 0.0f;
    /* Calcul du temps écoulé et affichage du taux de rafraîchissement */
    elapsedTime = sfClock_getElapsedTime(clock).microseconds / 1000000.0f;
    if (elapsedTime >= 1.0f) {
      float fps = frameCount / elapsedTime;
      (void)fps; // Mark fps as used to silence warning if printf is commented
      // printf("Refresh rate: %.2f FPS\n", fps); // Supprimé ou commenté
      sfClock_restart(clock);
      frameCount = 0; // Réinitialiser frameCount ici
    }
#endif

    /* Petite pause pour limiter la charge CPU */
    usleep(100);
  }
#endif

  sfClock_destroy(clock);

  printf("\nTerminaison des threads et nettoyage...\n");
  /* Terminaison et synchronisation */
  context.running = 0;
  dmxCtx->running = 0;
  keepRunning = 0; // Variable globale du module DMX

  pthread_join(udpThreadId, NULL);
  pthread_join(audioThreadId, NULL);
  pthread_join(fftSynthThreadId, NULL); // Join the FFT synth thread
#ifdef USE_DMX
  if (use_dmx && dmxFd >= 0) {
    pthread_join(dmxThreadId, NULL);
  }
#endif

  // Nettoyage MIDI et audio
  // visual_freeze_cleanup(); // Removed: Old visual-only freeze
  displayable_synth_buffers_cleanup(); // Cleanup displayable synth buffers
  synth_data_freeze_cleanup();         // Cleanup synth data freeze resources
  midi_Cleanup();
  audio_Cleanup(); // Nettoyage de RtAudio

  /* Nettoyage des ressources graphiques */
#ifndef CLI_MODE
  // Mode GUI normal, toujours nettoyer
  sfTexture_destroy(backgroundTexture);
  sfTexture_destroy(foregroundTexture);
  sfSprite_destroy(backgroundSprite);
  sfSprite_destroy(foregroundSprite);
  sfRenderWindow_destroy(window);
#else
  // Mode CLI, nettoyer seulement si la fenêtre SFML était utilisée
  if (use_sfml_window) {
    sfTexture_destroy(backgroundTexture);
    sfTexture_destroy(foregroundTexture);
    sfSprite_destroy(backgroundSprite);
    sfSprite_destroy(foregroundSprite);
    sfRenderWindow_destroy(window);
  }
#endif

  return 0;
}
