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

// External library includes
#ifdef NO_SFML
// If SFML is disabled, provide stubs/forward declarations for SFML types used.
// This is global for main.c when NO_SFML is defined.
typedef struct {
  unsigned long long microseconds;
} sfTime;

typedef struct sfClock sfClock; // Forward declaration for sfClock

// Stubs for sfClock functions if they are called (they shouldn't be thanks to
// #ifndef NO_SFML guards below). But the compiler needs to see them if PRINT_FPS
// is active and tries to compile the calls. The guards around sfClock_* calls in
// the main loop should prevent this. However, for `sfClock *clock = NULL;` to be
// valid, the type declaration is sufficient. Function definitions here are for
// completeness if they were ever called by mistake.
sfClock *sfClock_create(void) { return NULL; }
void sfClock_destroy(sfClock *clock) { (void)clock; }
sfTime sfClock_getElapsedTime(const sfClock *clock) {
  (void)clock;
  sfTime t = {0};
  return t;
}
void sfClock_restart(sfClock *clock) { (void)clock; }

// Other SFML types might need forward declarations here if they are used in
// main.c outside of #ifndef NO_SFML blocks and are not already covered by
// context.h/display.h. For example: typedef struct sfVideoMode sfVideoMode;
// If used directly. But sfVideoMode mode = ... is already in a #ifndef NO_SFML block

#else // NO_SFML is NOT defined, so SFML is enabled
// Include real SFML headers
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#endif // NO_SFML

// Internal project includes
#include "audio_c_api.h"
#include "auto_volume.h"
#include "config.h"
#include "config_instrument.h"
#include "config_loader.h"
#include "context.h"
#include "display.h"
#include "dmx.h"
#include "error.h"
#include "image_debug.h"
#include "logger.h"
#include "multithreading.h"
#include "synth_additive.h"
#include "synth_polyphonic.h" // Added for the new FFT synth mode
#include "synth_photowave.h"  // Added for Photowave synthesis
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

// Global sequencer instance for MIDI callbacks
ImageSequencer *g_image_sequencer = NULL;

// Make signal handler visible to other modules (like dmx.c)
void signalHandler(int signal) {
  static volatile sig_atomic_t already_called = 0;

  // Avoid recursive calls to the handler
  if (already_called) {
    // If handler is called again, user is insisting with Ctrl+C,
    // so force exit immediately
    kill(getpid(), SIGKILL);
    return;
  }

  already_called = 1;

  (void)signal;
  printf("\nSignal d'arrêt reçu. Arrêt en cours...\n");
  fflush(stdout);

  // Update stop flags
  app_running = 0;
  if (global_context) {
    global_context->running = 0;
    if (global_context->dmxCtx) {
      global_context->dmxCtx->running = 0;
    }
    // Cleanup UDP socket immediately to prevent port conflicts
    udp_cleanup(global_context->socket);
  }
  keepRunning = 0; // Global variable from DMX module

  // Force immediate termination without waiting for threads
  printf("\nForced exit!\n");
  fflush(stdout);

  // Kill process with SIGKILL (cannot be ignored or blocked)
  kill(getpid(), SIGKILL);
}

int main(int argc, char **argv) {
  /* Initialize logging system (default INFO level) */
  logger_init(LOG_LEVEL_INFO);
  
  /* Load Sp3ctra configuration */
  log_info("CONFIG", "Loading Sp3ctra configuration...");
  if (load_additive_config("sp3ctra.ini") != 0) {
    log_error("CONFIG", "Failed to load configuration. Exiting.");
    return EXIT_FAILURE;
  }
  
  /* Apply log level from configuration */
  logger_init(g_sp3ctra_config.log_level);
  log_info("CONFIG", "Log level set from configuration");
  
  // Configure SIGINT signal handler (Ctrl+C)
  signal(SIGINT, signalHandler);
  /* Parse command-line arguments */
  int use_dmx = 0;                 // Default: DMX disabled
  int verbose_dmx = 0;             // Default: normal DMX messages
  const char *dmx_port = DMX_PORT; // Default DMX port
  int list_audio_devices = 0;      // Display audio devices
  int audio_device_id = -1;        // -1 = use default device
  char *audio_device_name = NULL;  // Name of requested audio device
  int use_sfml_window = 0; // Default: no SFML window in CLI mode

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Sp3ctra - Real-time audio synthesis application\n\n");
      printf("Usage: %s [OPTIONS]\n\n", argv[0]);
      printf("OPTIONS:\n");
      printf("  --help, -h               Show this help message\n");
      printf("  --display                Enable visual scanner display\n");
      printf("  --list-audio-devices     List available audio devices and exit\n");
      printf("  --audio-device=<ID>      Use specific audio device ID\n");
      printf("  --dmx[=<PORT>[,v]]       Enable DMX with default port (%s) or specific port (v=verbose)\n", DMX_PORT);
      printf("  --test-tone              Enable test tone mode (440Hz)\n");
      printf("  --debug-image[=LINES]    Enable raw scanner capture debug (default: 1000 lines)\n");
      printf("  --debug-additive-osc-image[=SAMPLES[,m]] Enable oscillator volume capture debug (default: 48000 samples, m=markers)\n");
      printf("  --debug-additive-osc=<N|N-M> Debug one or a range of additive oscillators (e.g., 56 or 23-89)\n");
      printf("\nExamples:\n");
      printf("  ./build/Sp3ctra --help                           # Show this help message\n");
      printf("  ./build/Sp3ctra --display                        # Enable visual scanner display\n");
      printf("  ./build/Sp3ctra --list-audio-devices             # List available audio devices and exit\n");
      printf("  ./build/Sp3ctra --audio-device=3                 # Use specific audio device ID\n");
      printf("  ./build/Sp3ctra --dmx                            # Enable DMX with default port\n");
      printf("  ./build/Sp3ctra --dmx=/dev/ttyUSB0               # Enable DMX with custom port\n");
      printf("  ./build/Sp3ctra --dmx=/dev/ttyUSB0,v             # Enable DMX with verbose mode\n");
      printf("  ./build/Sp3ctra --test-tone                      # Enable test tone mode (440Hz)\n");
      printf("  ./build/Sp3ctra --debug-image=2000               # Enable raw scanner capture debug (2000 lines)\n");
      printf("  ./build/Sp3ctra --debug-additive-osc-image=24000 # Enable oscillator volume capture debug (24000 samples)\n");
      printf("  ./build/Sp3ctra --debug-additive-osc-image=24000,m # Enable oscillator debug with markers\n");
      printf("  ./build/Sp3ctra --debug-additive-osc=56          # Debug one additive oscillator (56)\n");
      printf("  ./build/Sp3ctra --debug-additive-osc=23-89       # Debug range of additive oscillators (23-89)\n");
      printf("  ./build/Sp3ctra --display --audio-device=1       # Run with visual display and specific audio device\n");
      return EXIT_SUCCESS;
    } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
      logger_init(LOG_LEVEL_DEBUG);
      log_info("MAIN", "Verbose logging enabled");
    // Option --cli removed as redundant (CLI mode is the default mode)
    } else if (strcmp(argv[i], "--sfml-window") == 0 || strcmp(argv[i], "--show-display") == 0 || strcmp(argv[i], "--display") == 0) {
      use_sfml_window = 1;
      printf("Visual scanner display enabled\n");
    } else if (strcmp(argv[i], "--dmx") == 0) {
      use_dmx = 1;
      verbose_dmx = 0;
      printf("DMX enabled with default port: %s\n", dmx_port);
    } else if (strncmp(argv[i], "--dmx=", 6) == 0) {
      const char *dmx_param = argv[i] + 6;
      
      // Check for verbose flag: --dmx=<PORT>,v
      char *comma_pos = strchr(dmx_param, ',');
      if (comma_pos) {
        // Extract port and check for verbose flag
        size_t port_len = comma_pos - dmx_param;
        if (port_len > 0 && port_len < 256) {
          static char custom_port[256];
          strncpy(custom_port, dmx_param, port_len);
          custom_port[port_len] = '\0';
          dmx_port = custom_port;
          
          // Check for verbose flag
          if (strcmp(comma_pos + 1, "v") == 0) {
            verbose_dmx = 1;
            printf("DMX enabled with port: %s (verbose mode)\n", dmx_port);
          } else {
            printf("❌ Error: Invalid DMX flag '%s' (use 'v' for verbose)\n", comma_pos + 1);
            return EXIT_FAILURE;
          }
        } else {
          printf("❌ Error: Invalid DMX port length\n");
          return EXIT_FAILURE;
        }
      } else {
        // Only port specified: --dmx=<PORT>
        dmx_port = dmx_param;
        verbose_dmx = 0;
        printf("DMX enabled with port: %s\n", dmx_port);
      }
      use_dmx = 1;
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
    } else if (strncmp(argv[i], "--debug-image", 13) == 0) {
      const char *param = argv[i] + 13;
      int capture_lines = 1000; // Default value
      
      if (*param == '=') {
        // Parse the number after the equals sign
        param++; // Skip the '='
        char *endptr;
        long lines = strtol(param, &endptr, 10);
        if (*endptr == '\0' && lines > 0 && lines <= 50000) {
          capture_lines = (int)lines;
        } else {
          printf("❌ Error: Invalid capture lines value '%s' (must be 1-50000)\n", param);
          return EXIT_FAILURE;
        }
      } else if (*param != '\0') {
        printf("❌ Error: Invalid --debug-image format '%s' (use --debug-image or --debug-image=LINES)\n", argv[i]);
        return EXIT_FAILURE;
      }
      
      printf("🔧 Image transformation debug enabled (%d lines)\n", capture_lines);
      // Configure raw scanner capture with runtime parameters
      image_debug_configure_raw_scanner(1, capture_lines);
    } else if (strncmp(argv[i], "--debug-additive-osc-image", 26) == 0) {
      const char *param = argv[i] + 26;
      int capture_samples = MAX_SAMPLING_FREQUENCY; // Default value (1 second at max freq)
      int enable_markers = 0; // Default: markers disabled
      
      if (*param == '=') {
        // Parse the number after the equals sign
        param++; // Skip the '='
        char *comma_pos = strchr(param, ',');
        
        if (comma_pos) {
          // Parse samples and markers: "2000,m"
          char samples_str[32];
          int samples_len = comma_pos - param;
          if (samples_len < (int)sizeof(samples_str)) {
            strncpy(samples_str, param, samples_len);
            samples_str[samples_len] = '\0';
            
            long samples = strtol(samples_str, NULL, 10);
            if (samples > 0 && samples <= 480000) {
              capture_samples = (int)samples;
            } else {
              printf("❌ Error: Invalid capture samples value '%s' (must be 1-480000)\n", samples_str);
              return EXIT_FAILURE;
            }
            
            // Check for markers flag
            const char *marker_param = comma_pos + 1;
            if (strcmp(marker_param, "m") == 0) {
              enable_markers = 1;
            } else {
              printf("❌ Error: Invalid marker flag '%s' (use 'm' to enable markers)\n", marker_param);
              return EXIT_FAILURE;
            }
          } else {
            printf("❌ Error: Samples value too long\n");
            return EXIT_FAILURE;
          }
        } else {
          // Only samples specified: "2000"
          char *endptr;
          long samples = strtol(param, &endptr, 10);
          if (*endptr == '\0' && samples > 0 && samples <= 480000) {
            capture_samples = (int)samples;
          } else {
            printf("❌ Error: Invalid capture samples value '%s' (must be 1-480000)\n", param);
            return EXIT_FAILURE;
          }
        }
      } else if (*param != '\0') {
        printf("❌ Error: Invalid --debug-additive-osc-image format '%s' (use --debug-additive-osc-image[=SAMPLES[,m]])\n", argv[i]);
        return EXIT_FAILURE;
      }
      
      printf("🔧 Additive oscillator debug enabled (%d samples%s)\n", 
             capture_samples, enable_markers ? ", markers enabled" : "");
      // Configure oscillator volume capture with runtime parameters
      image_debug_configure_oscillator_capture(1, capture_samples, enable_markers);
    } else if (strncmp(argv[i], "--debug-additive-osc=", 21) == 0) {
      printf("🔧 Debug oscillateur additif activé !\n");
      const char *osc_param = argv[i] + 21;
      
      // Parse "56" or "23-89" - simplified version without global config
      if (strchr(osc_param, '-')) {
        // Range format: "23-89"
        int start, end;
        if (sscanf(osc_param, "%d-%d", &start, &end) == 2) {
          printf("🔧 Debug oscillateur additif activé pour la plage %d-%d\n", start, end);
        } else {
          printf("❌ Erreur: format de plage invalide '%s' (utilisez N-M)\n", osc_param);
          return EXIT_FAILURE;
        }
      } else {
        // Single oscillator format: "56"
        int single_osc = atoi(osc_param);
        printf("🔧 Debug oscillateur additif activé pour l'oscillateur %d\n", single_osc);
      }
    } else {
      printf("Unknown option: %s\n", argv[i]);
      printf("Use --help for usage information\n");
      return EXIT_FAILURE;
    }
  }

  int dmxFd = -1;
  if (use_dmx) {
#ifdef USE_DMX
    // Convert verbose_dmx to silent_dmx for init_Dmx function (inverted logic)
    int silent_dmx = !verbose_dmx;
    dmxFd = init_Dmx(dmx_port, silent_dmx);
    if (dmxFd < 0) {
      if (verbose_dmx) {
        printf("Failed to initialize DMX. Continuing without DMX support.\n");
      }
      // If DMX initialization failed, disable DMX completely
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
  // This entire block only executes if SFML is enabled
  // Use runtime pixel count for window width
  int window_width = get_cis_pixels_nb();
  sfVideoMode mode = {window_width, WINDOWS_HEIGHT, 32};

  // Create SFML window only if the option is enabled
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

  // Process audio options BEFORE initializing the audio system
  if (list_audio_devices) {
    // Initialize audio temporarily just to list devices
    audio_Init();
    printAudioDevices();
    // If --list-audio-devices is specified, we clean up and exit,
    // regardless of other arguments.
    printf("Audio device listing complete. Exiting.\n");
    audio_Cleanup();
    midi_Cleanup();          // Ensure MIDI cleanup as well
    if (dmxCtx) {            // Check if dmxCtx was allocated
      if (dmxCtx->fd >= 0) { // Check if fd is valid before closing
        close(dmxCtx->fd);
      }
      pthread_mutex_destroy(&dmxCtx->mutex); // Clean up mutex and cond
      pthread_cond_destroy(&dmxCtx->cond);
      free(dmxCtx);
      dmxCtx = NULL; // Avoid double free or use after free
    }
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
      midi_set_note_on_callback(synth_polyphonic_note_on);
      midi_set_note_off_callback(synth_polyphonic_note_off);
      log_info("MIDI", "Note On/Off callbacks for synth_polyphonic registered via C API");
    } else {
      log_info("MIDI", "No controller found");
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
  log_info("CONFIG", "========== SYNTHESIS CONFIGURATION ==========");
  log_info("CONFIG", "ADDITIVE synthesis: %s",
         enable_additive_synth ? "ENABLED" : "DISABLED");
  log_info("CONFIG", "POLYPHONIC synthesis: %s",
         enable_polyphonic_synth ? "ENABLED" : "DISABLED");
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
  synth_polyphonicMode_init(); // Initialize the polyphonic synth mode
  synth_photowave_mode_init(); // Initialize the Photowave synth mode
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
  context.audioData = NULL;   // RtAudio now manages the audio buffer
  context.doubleBuffer = &db; // Legacy double buffer (for display)
  context.audioImageBuffers =
      &audioImageBuffers; // New dual buffer system for audio
  context.dmxCtx = dmxCtx;
  context.running = 1; // Termination flag for the context

  // Save context for signal handler
  global_context = &context;

  /* Initialize auto-volume controller (reads IMU X from UDP thread and
     adjusts master volume). Instance is stored in auto_volume.c as
     gAutoVolumeInstance. */
  log_info("INIT", "Initializing auto-volume controller...");
  log_info("INIT", "Auto-volume config: enabled=%d, threshold=%.3f, timeout=%ds, fade=%dms",
         g_sp3ctra_config.auto_volume_enabled, IMU_ACTIVE_THRESHOLD_X, g_sp3ctra_config.imu_inactivity_timeout_s, g_sp3ctra_config.auto_volume_fade_ms);
  log_info("INIT", "Volume levels: active=%.1f, inactive=%.3f",
         1.0f, g_sp3ctra_config.auto_volume_inactive_level);

  gAutoVolumeInstance = auto_volume_create(&context);
  if (!gAutoVolumeInstance) {
    log_error("INIT", "Failed to initialize auto-volume controller");
  } else {
    log_info("INIT", "Auto-volume controller initialized successfully");
  }

  /* Create textures and sprites for rendering in main thread */
  sfTexture *backgroundTexture = NULL;
  sfTexture *foregroundTexture = NULL;

#ifndef NO_SFML
  sfSprite *backgroundSprite = NULL;
  sfSprite *foregroundSprite = NULL;
  // This block only executes if SFML is enabled
  // Create textures only if SFML window is requested
  if (use_sfml_window) {
    int texture_width = get_cis_pixels_nb();
    backgroundTexture = sfTexture_create(texture_width, WINDOWS_HEIGHT);
    foregroundTexture = sfTexture_create(texture_width, WINDOWS_HEIGHT);
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
  param.sched_priority = 50; // More moderate priority for Jetson Nano
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
    log_info("THREAD", "Polyphonic synthesis thread started successfully");
    // Optionally set scheduling parameters for fftSynthThreadId as well if
    // needed
  } else {
    log_info("THREAD", "Polyphonic synthesis thread NOT created (disabled by configuration)");
  }

  // Create and start the Photowave synth thread
  pthread_t photowaveThreadId;
  if (pthread_create(&photowaveThreadId, NULL,
                     synth_photowave_thread_func,
                     (void *)&context) != 0) {
    perror("Error creating Photowave synth thread");
#ifndef NO_SFML
    if (window)
      sfRenderWindow_destroy(window);
#endif
    return EXIT_FAILURE;
  }
  log_info("THREAD", "Photowave synthesis thread started successfully");
  
  // Apply Photowave configuration from sp3ctra.ini
  synth_photowave_apply_config(&g_photowave_state);
  
  // Photowave is now polyphonic and controlled via MIDI notes
  // No need to set a default frequency - it will respond to MIDI Note On events
  log_info("PHOTOWAVE", "Polyphonic photowave synthesis initialized (8 voices)");
  
  // Log initial mix level for debugging
  extern float getSynthPhotowaveMixLevel(void);
  log_info("PHOTOWAVE_DEBUG", "Initial mix level at startup: %.2f (%.0f%%)", 
           getSynthPhotowaveMixLevel(), getSynthPhotowaveMixLevel() * 100.0f);

  /* Main loop (gestion des événements et rendu) */
  // sfEvent event; // Unused variable
#ifndef NO_SFML
  sfClock *clock = NULL;
  // Create clock only if SFML is used
  clock = sfClock_create();
#endif // NO_SFML

#ifdef PRINT_FPS
  unsigned int frameCount = 0;
#endif
  int running = 1;

  /* Boucle principale */
  log_info("SYNTH", "========================================================");
  log_info("SYNTH", "Application running");
  if (use_sfml_window) {
    log_info("SYNTH", "Visual scanner display enabled");
  } else {
    log_info("SYNTH", "No visual display (use --display to enable)");
  }
  log_info("SYNTH", "Press Ctrl+C to stop the application");
  log_info("SYNTH", "========================================================");

  /* Boucle principale */
  /* Allocate local buffers dynamically based on runtime pixel count */
  int nb_pixels = get_cis_pixels_nb();
  uint8_t *local_main_R = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  uint8_t *local_main_G = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  uint8_t *local_main_B = (uint8_t *)malloc(nb_pixels * sizeof(uint8_t));
  
  if (!local_main_R || !local_main_G || !local_main_B) {
    log_error("MAIN", "Failed to allocate local main loop buffers");
    if (local_main_R) free(local_main_R);
    if (local_main_G) free(local_main_G);
    if (local_main_B) free(local_main_B);
    context.running = 0;
    return EXIT_FAILURE;
  }
  // Note: sequencer_output_R/G/B buffers removed - sequencer output is now
  // directly written to g_displayable_synth_R/G/B by the UDP thread
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
    // If NO_SFML is defined, but use_sfml_window is true,
    // this indicates a configuration inconsistency.
    // On pourrait ajouter un avertissement ici, ou simplement ne rien faire.
    if (use_sfml_window && window) {
      // This block should not be reached if NO_SFML is defined and window
      // est NULL. Si window est non-NULL ici, c'est une erreur de logique dans
      // window creation.
    }
#endif // NO_SFML

    /* Vérifier si le double buffer contient de nouvelles données */
    pthread_mutex_lock(&db.mutex);
    if (db.dataReady) {
      // Copy live data from double buffer
      memcpy(local_main_R, db.processingBuffer_R, nb_pixels);
      memcpy(local_main_G, db.processingBuffer_G, nb_pixels);
      memcpy(local_main_B, db.processingBuffer_B, nb_pixels);
      db.dataReady = 0; // Mark as consumed by main loop
      process_this_frame_main_loop = 1;
    }
    pthread_mutex_unlock(&db.mutex);

    if (process_this_frame_main_loop) {
      /* NOTE: Image sequencer is already processed in UDP thread!
       * The UDP thread calls image_sequencer_process_frame() and updates
       * g_displayable_synth_R/G/B with the ADSR-modulated output.
       * We just use local_main_R/G/B (from db.processingBuffer) for DMX.
       */
      
      /* Rendu de la nouvelle ligne si SFML est activé */
      if (use_sfml_window && window) {
        // Lock mutex before accessing displayable synth buffers
        pthread_mutex_lock(&g_displayable_synth_mutex);
        printImageRGB(window, g_displayable_synth_R, g_displayable_synth_G,
                      g_displayable_synth_B, backgroundTexture,
                      foregroundTexture); // Now displays sequencer output with ADSR!
        pthread_mutex_unlock(&g_displayable_synth_mutex);
      }

      /* Calcul de la couleur moyenne et mise à jour du contexte DMX */
      // DMX uses copied data local_main_R,G,B (which is the data
      // live de db.processingBuffer)
      if (use_dmx && dmxCtx->spots && dmxCtx->num_spots > 0) {
        computeAverageColorPerZone(local_main_R, local_main_G, local_main_B,
                                   nb_pixels, dmxCtx->spots, dmxCtx->num_spots);

        pthread_mutex_lock(&dmxCtx->mutex);
        dmxCtx->colorUpdated = 1;
        pthread_cond_signal(&dmxCtx->cond);
        pthread_mutex_unlock(&dmxCtx->mutex);
      }

#ifdef PRINT_FPS
      frameCount++; // Count each processed frame
#endif
    }

#ifdef PRINT_FPS
    float elapsedTime = 0.0f;
#ifndef NO_SFML
    if (clock) { // Check if clock was initialized
      elapsedTime = sfClock_getElapsedTime(clock).microseconds / 1000000.0f;
      if (elapsedTime >= 1.0f) {
        float fps = frameCount / elapsedTime;
        (void)fps; // Mark fps as used to silence warning if printf is commented
        // printf("Processing rate: %.2f FPS\n", fps); // Removed or commented
        sfClock_restart(clock);
        frameCount = 0; // Reset frameCount here
      }
    }
#else
    // Alternative for timing if NO_SFML is defined and PRINT_FPS is
    // active (would require non-SFML clock implementation, like those
    // at start of file) For now, we do nothing for FPS if
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

  // Stop and join the Photowave synth thread
  synth_photowave_thread_stop();  // Signal thread to stop BEFORE joining
  pthread_join(photowaveThreadId, NULL);
  printf("Photowave synthesis thread terminated\n");

#ifdef USE_DMX
  if (use_dmx && dmxFd >= 0) {
    pthread_join(dmxThreadId, NULL);
  }
#endif

  // Nettoyage MIDI et audio
  // visual_freeze_cleanup(); // Removed: Old visual-only freeze
  displayable_synth_buffers_cleanup(); // Cleanup displayable synth buffers
  synth_data_freeze_cleanup();         // Cleanup synth data freeze resources
  synth_photowave_mode_cleanup();      // Cleanup Photowave synthesis resources
  
  // Cleanup image sequencer
  if (imageSequencer) {
    image_sequencer_destroy(imageSequencer);
    imageSequencer = NULL;
  }
  
  // Free local main loop buffers
  if (local_main_R) free(local_main_R);
  if (local_main_G) free(local_main_G);
  if (local_main_B) free(local_main_B);
  
  cleanupDoubleBuffer(&db);            // Cleanup DoubleBuffer resources
  audio_image_buffers_cleanup(
      &audioImageBuffers);     // Cleanup new audio image buffers
  udp_cleanup(context.socket); // Cleanup UDP socket to prevent port conflicts
  
  // Cleanup unified MIDI system before general MIDI cleanup
  midi_mapping_cleanup();
  midi_Cleanup();

  /* Destroy auto-volume controller (if created) before audio cleanup */
  if (gAutoVolumeInstance) {
    auto_volume_destroy(gAutoVolumeInstance);
    gAutoVolumeInstance = NULL;
  }

  audio_Cleanup(); // Nettoyage de RtAudio

  /* Nettoyage des ressources graphiques */
#ifndef NO_SFML
  // This block only executes if SFML is enabled
  // Clean up only if SFML window was used
  if (use_sfml_window &&
      window) { // window will be non-NULL only if use_sfml_window was true AND
                // creation succeeded
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
