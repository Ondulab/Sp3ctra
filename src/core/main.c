// Standard library includes
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

// External library includes - SFML completely removed (core audio only)

// Internal project includes
#include "audio_c_api.h"
#include "config.h"
#include "config_instrument.h"
#include "config_loader.h"
#include "context.h"
#include "error.h"
#include "logger.h"
#include "multithreading.h"
#include "synth_luxstral.h"
#include "synth_luxstral_threading.h"
#include "synth_luxsynth.h"
#include "synth_luxwave.h"
#include "udp.h"
#include "../processing/image_preprocessor.h"
#include "../processing/image_sequencer.h"

// External MIDI function declarations (C-compatible)
extern void midi_Init(void);
extern void midi_Cleanup(void);
extern int midi_Connect(void);
extern int midi_ConnectAll(void);
extern int midi_ConnectByName(const char* deviceName);
extern int midi_GetConnectedDeviceCount(void);
extern void midi_SetupVolumeControl(void);
// Add declarations for the new C-API MIDI callback setters
extern void midi_set_note_on_callback(void (*callback)(int noteNumber,
                                                       int velocity));
extern void midi_set_note_off_callback(void (*callback)(int noteNumber));

// External unified MIDI system declarations
extern void midi_mapping_init(void);
extern int midi_mapping_load_parameters(const char *filename);
extern int midi_mapping_load_mappings(const char *filename);
extern void midi_callbacks_register_all(void);
extern int midi_mapping_apply_defaults(void);
extern void midi_mapping_validate(void);
extern void midi_mapping_cleanup(void);
extern const char* midi_mapping_get_device_name(void);
extern int midi_mapping_get_device_id(void);

// Global signal handler for the application
volatile sig_atomic_t app_running = 1;
Context *global_context =
    NULL; // Global context for signal handler

// External keepRunning flag used by synthesis threads
extern volatile int keepRunning;

// Global sequencer instance for MIDI callbacks
ImageSequencer *g_image_sequencer = NULL;

// Make signal handler visible to other modules (like dmx.c)
void signalHandler(int signal) {
  static volatile sig_atomic_t already_called = 0;

  // Avoid recursive calls to the handler
  if (already_called) {
    // If handler is called again, user is insisting with Ctrl+C,
    // so force exit immediately (but less brutally than SIGKILL)
    printf("\nForced immediate exit (second Ctrl+C)!\n");
    fflush(stdout);
    _exit(1);
    return;
  }

  already_called = 1;

  (void)signal;
  printf("\nSignal d'arrêt reçu. Arrêt en cours...\n");
  fflush(stdout);

  // Update stop flags
  app_running = 0;
  keepRunning = 0;  // Signal synthesis threads to stop
  if (global_context) {
    global_context->running = 0;
  }
  synth_luxwave_thread_stop(); // Signal LuxWave thread to stop immediately

  // NOTE: Let the main loop handle cleanup properly
  // Don't kill the process here - just set flags and return
}

int main(int argc, char **argv) {
  /* Initialize logging system (default INFO level) */
  logger_init(LOG_LEVEL_INFO);
  
  /* Load Sp3ctra configuration */
  log_info("CONFIG", "Loading Sp3ctra configuration...");
  if (load_luxstral_config("sp3ctra.ini") != 0) {
    log_error("CONFIG", "Failed to load configuration. Exiting.");
    return EXIT_FAILURE;
  }
  
  /* Apply log level from configuration */
  logger_init(g_sp3ctra_config.log_level);
  log_info("CONFIG", "Log level set from configuration");
  
  // Configure SIGINT signal handler (Ctrl+C)
  signal(SIGINT, signalHandler);
  
  /* Parse command-line arguments */
  int list_audio_devices = 0;      // Display audio devices
  int audio_device_id = -1;        // -1 = use default device
  char *audio_device_name = NULL;  // Name of requested audio device

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Sp3ctra Core - Real-time spectral audio synthesis (headless)\n\n");
      printf("Usage: %s [OPTIONS]\n\n", argv[0]);
      printf("OPTIONS:\n");
      printf("  --help, -h               Show this help message\n");
      printf("  --list-audio-devices     List available audio devices and exit\n");
      printf("  --audio-device=<ID>      Use specific audio device ID\n");
      printf("  --test-tone              Enable test tone mode (440Hz)\n");
      printf("\nExamples:\n");
      printf("  ./build/Sp3ctra --help                  # Show this help message\n");
      printf("  ./build/Sp3ctra --list-audio-devices    # List available audio devices\n");
      printf("  ./build/Sp3ctra --audio-device=3        # Use specific audio device ID\n");
      printf("  ./build/Sp3ctra --test-tone             # Enable test tone mode (440Hz)\n");
      return EXIT_SUCCESS;
    } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
      logger_init(LOG_LEVEL_DEBUG);
      log_info("MAIN", "Verbose logging enabled");
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
      setMinimalCallbackMode(1);
    } else {
      printf("Unknown option: %s\n", argv[i]);
      printf("Use --help for usage information\n");
      return EXIT_FAILURE;
    }
  }

  /* Initialize UDP and Audio */
  struct sockaddr_in si_other, si_me;

  // Process audio options BEFORE initializing the audio system
  if (list_audio_devices) {
    // Initialize audio temporarily just to list devices
    audio_Init();
    printAudioDevices();
    printf("Audio device listing complete. Exiting.\n");
    audio_Cleanup();
    return EXIT_SUCCESS;
  }

  // Configure audio device BEFORE initialization if specified
  if (audio_device_id >= 0) {
    setRequestedAudioDevice(audio_device_id);
    printf("Périphérique audio %d configuré pour l'initialisation.\n",
           audio_device_id);
  } else if (audio_device_name != NULL) {
    setRequestedAudioDeviceName(audio_device_name);
    printf("Périphérique audio '%s' configuré pour l'initialisation.\n",
           audio_device_name);
  }

  // Initialize audio (RtAudio) with the correct device
  audio_Init();

  // Determine synthesis modes based on configuration
  int enable_luxsynth_synth = 1;
  int enable_luxstral_synth = 1;
  int enable_midi = 1;
  int midi_connected = 0;

  // Check manual disable flags (highest priority)
#ifdef DISABLE_LUXSYNTH
  enable_luxsynth_synth = 0;
  printf("LuxSynth synthesis DISABLED by configuration\n");
#endif

#ifdef DISABLE_LUXSTRAL
  enable_luxstral_synth = 0;
  printf("LUXSTRAL synthesis DISABLED by configuration\n");
#endif

#if !ENABLE_MIDI_POLLING
  enable_midi = 0;
  printf("MIDI polling DISABLED by configuration\n");
#endif

  // Initialize MIDI if enabled (but don't register callbacks yet)
  if (enable_midi) {
    midi_Init();
    midi_SetupVolumeControl();

    // Initialize unified MIDI mapping system
    log_info("MIDI", "Initializing unified MIDI system...");
    midi_mapping_init();
    
    // Load MIDI parameter specifications from sp3ctra.ini
    if (midi_mapping_load_parameters("sp3ctra.ini") != 0) {
      log_warning("MIDI", "Failed to load MIDI parameter specifications from sp3ctra.ini");
    } else {
      log_info("MIDI", "Parameter specifications loaded");
    }
    
    // Load user MIDI mappings
    if (midi_mapping_load_mappings("midi_mapping.ini") != 0) {
      log_warning("MIDI", "Failed to load MIDI mappings");
    } else {
      log_info("MIDI", "User mappings loaded");
    }
    
    // Try to connect to MIDI controller using configured device_name
    const char* device_name = midi_mapping_get_device_name();
    log_info("MIDI", "Attempting to connect to MIDI device: '%s'", device_name);
    
    if (strcmp(device_name, "auto") == 0) {
      // Connect to ALL available MIDI devices (multi-device support)
      midi_connected = midi_ConnectAll();
      if (midi_connected) {
        int device_count = midi_GetConnectedDeviceCount();
        log_info("MIDI", "Multi-device mode: %d MIDI device(s) connected", device_count);
      }
    } else {
      // Use specific device name from configuration (single device mode)
      midi_connected = midi_ConnectByName(device_name);
    }
    
    if (midi_connected) {
      log_info("MIDI", "Controller connected");
      // Setup note callbacks if MIDI connected successfully
      midi_set_note_on_callback(synth_luxsynth_note_on);
      midi_set_note_off_callback(synth_luxsynth_note_off);
      log_info("MIDI", "Note On/Off callbacks for synth_luxsynth registered via C API");
    } else {
      log_info("MIDI", "No controller found");
    }
  }

  // Check automatic polyphonic disable based on MIDI presence
#if AUTO_DISABLE_LUXSYNTH_WITHOUT_MIDI
  if (!midi_connected && enable_luxsynth_synth) {
    enable_luxsynth_synth = 0;
    printf(
        "LuxSynth synthesis AUTO-DISABLED - no MIDI controller detected\n");
  }
#endif

  // Display final synthesis configuration
  log_info("CONFIG", "========== SYNTHESIS CONFIGURATION ==========");
  log_info("CONFIG", "LUXSTRAL synthesis: %s",
         enable_luxstral_synth ? "ENABLED" : "DISABLED");
  log_info("CONFIG", "LUXSYNTH synthesis: %s",
         enable_luxsynth_synth ? "ENABLED" : "DISABLED");
  log_info("CONFIG", "MIDI polling: %s", enable_midi ? "ENABLED" : "DISABLED");
  if (enable_midi) {
    log_info("CONFIG", "MIDI connected: %s", midi_connected ? "YES" : "NO");
  }
  log_info("CONFIG", "============================================");

  // Initialize image preprocessor module
  image_preprocess_init();
  
  // Initialize image sequencer (5 players, 5 seconds max duration)
  // IMPORTANT: Must be done BEFORE registering MIDI callbacks!
  ImageSequencer *imageSequencer = image_sequencer_create(5, 5.0f);
  if (!imageSequencer) {
    log_error("INIT", "Failed to initialize image sequencer");
  } else {
    log_info("INIT", "Image sequencer initialized successfully");
    g_image_sequencer = imageSequencer; // Make globally accessible for MIDI
    
    // Enable sequencer by default
    image_sequencer_set_enabled(imageSequencer, 1);
    log_info("INIT", "Image sequencer ENABLED");
  }
  
  // NOW register all MIDI callbacks (after sequencer is created)
  if (enable_midi) {
    midi_callbacks_register_all();
    log_info("MIDI", "Callbacks registered");
    
    // Apply default values to all parameters (triggers callbacks to initialize structures)
    int defaults_applied = midi_mapping_apply_defaults();
    if (defaults_applied > 0) {
      log_info("MIDI", "Applied default values to %d parameters", defaults_applied);
    }
    
    // Validate configuration
    midi_mapping_validate();
    log_info("MIDI", "Unified MIDI system initialized");
  }
  
  synth_IfftInit();
  synth_luxsynthMode_init(); // Initialize the polyphonic synth mode
  synth_luxwave_mode_init(); // Initialize the LuxWave synth mode
  synth_data_freeze_init();         // Initialize synth data freeze feature
  displayable_synth_buffers_init(); // Initialize displayable synth buffers

  int s = udp_Init(&si_other, &si_me);
  if (s < 0) {
    perror("Error initializing UDP");
    return EXIT_FAILURE;
  }

  int status = startAudioUnit(); // RtAudio now returns an int
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
    return EXIT_FAILURE;
  }

  /* Build global context structure */
  Context context = {0};
  context.window = NULL;  // No display
  context.socket = s;
  context.si_other = &si_other;
  context.si_me = &si_me;
  context.audioData = NULL;
  context.doubleBuffer = &db;
  context.audioImageBuffers = &audioImageBuffers;
  context.dmxCtx = NULL;  // No DMX
  context.running = 1;

  // Save context for signal handler
  global_context = &context;

  /* Create threads for UDP and Audio (core audio only) */
  pthread_t udpThreadId, audioThreadId, fftSynthThreadId;

  if (pthread_create(&udpThreadId, NULL, udpThread, (void *)&context) != 0) {
    perror("Error creating UDP thread");
    return EXIT_FAILURE;
  }
  
  if (pthread_create(&audioThreadId, NULL, audioProcessingThread,
                     (void *)&context) != 0) {
    perror("Error creating audio processing thread");
    return EXIT_FAILURE;
  }

  struct sched_param param;
  param.sched_priority = 50; // More moderate priority for Jetson Nano
  pthread_setschedparam(audioThreadId, SCHED_RR, &param);

  // Create and start the polyphonic synth thread conditionally
  int polyphonic_thread_created = 0;
  if (enable_luxsynth_synth) {
    if (pthread_create(&fftSynthThreadId, NULL,
                       synth_luxsynthMode_thread_func,
                       (void *)&context) != 0) {
      perror("Error creating polyphonic synth thread");
      return EXIT_FAILURE;
    }
    polyphonic_thread_created = 1;
    log_info("THREAD", "LuxSynth synthesis thread started successfully");
  } else {
    log_info("THREAD", "LuxSynth synthesis thread NOT created (disabled by configuration)");
  }

  // Create and start the LuxWave synth thread
  pthread_t photowaveThreadId;
  if (pthread_create(&photowaveThreadId, NULL,
                     synth_luxwave_thread_func,
                     (void *)&context) != 0) {
    perror("Error creating LuxWave synth thread");
    return EXIT_FAILURE;
  }
  log_info("THREAD", "LuxWave synthesis thread started successfully");
  
  // Apply LuxWave configuration from sp3ctra.ini
  synth_luxwave_apply_config(&g_luxwave_state);
  
  // LuxWave is now polyphonic and controlled via MIDI notes
  // No need to set a default frequency - it will respond to MIDI Note On events
  log_info("LUXWAVE", "LuxSynth photowave synthesis initialized (8 voices)");
  
  // Log initial mix level for debugging
  extern float getSynthLuxWaveMixLevel(void);
  log_info("LUXWAVE_DEBUG", "Initial mix level at startup: %.2f (%.0f%%)", 
           getSynthLuxWaveMixLevel(), getSynthLuxWaveMixLevel() * 100.0f);

  /* Main loop - Core audio only (headless) */
  log_info("SYNTH", "========================================================");
  log_info("SYNTH", "Sp3ctra Core Audio running (headless mode)");
  log_info("SYNTH", "Press Ctrl+C to stop the application");
  log_info("SYNTH", "========================================================");

  /* Simplified main loop - just wait for Ctrl+C */
  while (app_running && context.running) {
    usleep(10000);  // 10ms sleep to reduce CPU usage
  }

  log_info("MAIN", "========================================================");
  log_info("MAIN", "Application shutdown sequence initiated");
  log_info("MAIN", "========================================================");
  
  /* Step 1: Stop audio stream FIRST */
  log_info("MAIN", "Step 1/4: Stopping audio stream...");
  stopAudioUnit();
  log_info("MAIN", "Audio stream stopped");
  
  /* Step 2: Signal threads to stop */
  log_info("MAIN", "Step 2/4: Signaling threads to stop...");
  context.running = 0;
  app_running = 0;
  keepRunning = 0;  // Signal synthesis threads to stop
  synth_luxwave_thread_stop();
  
  /* Step 3: Join threads */
  log_info("MAIN", "Step 3/4: Joining threads...");
  pthread_join(udpThreadId, NULL);
  log_info("THREAD", "UDP thread terminated");
  
  pthread_join(audioThreadId, NULL);
  log_info("THREAD", "Audio processing thread terminated");

  if (polyphonic_thread_created) {
    pthread_join(fftSynthThreadId, NULL);
    log_info("THREAD", "LuxSynth synthesis thread terminated");
  }

  pthread_join(photowaveThreadId, NULL);
  log_info("THREAD", "LuxWave synthesis thread terminated");
  
  /* Step 4: Cleanup resources */
  log_info("MAIN", "Step 4/4: Cleaning up resources...");
  
  /* Shutdown LUXSTRAL thread pool FIRST (must release barrier-blocked workers) */
  synth_shutdown_thread_pool();
  log_info("CLEANUP", "LUXSTRAL thread pool shutdown complete");
  
  displayable_synth_buffers_cleanup();
  log_info("CLEANUP", "Displayable synth buffers cleaned up");
  
  synth_data_freeze_cleanup();
  log_info("CLEANUP", "Synth data freeze cleaned up");
  
  synth_luxwave_mode_cleanup();
  log_info("CLEANUP", "LuxWave synthesis cleaned up");
  
  if (imageSequencer) {
    image_sequencer_destroy(imageSequencer);
    log_info("CLEANUP", "Image sequencer destroyed");
  }
  
  cleanupDoubleBuffer(&db);
  log_info("CLEANUP", "Double buffer cleaned up");
  
  audio_image_buffers_cleanup(&audioImageBuffers);
  log_info("CLEANUP", "Audio image buffers cleaned up");
  
  udp_cleanup(context.socket);
  log_info("CLEANUP", "UDP socket closed");
  
  midi_mapping_cleanup();
  log_info("CLEANUP", "MIDI mapping cleaned up");
  
  midi_Cleanup();
  log_info("CLEANUP", "MIDI system cleaned up");

  audio_Cleanup();
  log_info("CLEANUP", "Audio system cleaned up");

  log_info("MAIN", "========================================================");
  log_info("MAIN", "Application terminated successfully");
  log_info("MAIN", "========================================================");
  
  // Restore default signal handler to allow clean process termination
  signal(SIGINT, SIG_DFL);
  
  return 0;
}
