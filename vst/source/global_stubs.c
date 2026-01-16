// Stubs temporaires pour permettre la compilation du VST minimal
// Ces variables seront remplacées par une architecture instanciée dans la version finale

#include "../../src/core/context.h"
#include "../../src/config/config_loader.h"

// Configuration globale (temporaire pour test)
sp3ctra_config_t g_sp3ctra_config = {
    .sampling_frequency = 48000,
    .audio_buffer_size = 512,
    .log_level = 2, // LOG_LEVEL_INFO
    .udp_address = "239.100.100.100",
    .udp_port = 55151,
    .multicast_interface = ""
};

// Stub pour la taille des pixels CIS
int get_cis_pixels_nb(void) {
    return 2048; // Valeur par défaut du capteur
}

// Stubs pour le système de logging (version simplifiée sans printf pour éviter les dépendances)
void logger_init(int level) { 
    (void)level; 
}

void log_info(const char* module, const char* format, ...) { 
    (void)module; 
    (void)format; 
}

void log_warning(const char* module, const char* format, ...) { 
    (void)module; 
    (void)format; 
}

void log_error(const char* module, const char* format, ...) { 
    (void)module; 
    (void)format; 
}

void log_debug(const char* module, const char* format, ...) { 
    (void)module; 
    (void)format; 
}

// Stub pour le chargement de configuration
int load_luxstral_config(const char* filename) {
    (void)filename;
    return 0; // Succès
}
