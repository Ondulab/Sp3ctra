# Configuration du projet
CONFIG += c++17
CONFIG += sdk_no_version_check
CONFIG += console

# Option pour le mode CLI (sans interface graphique)
cli_mode {
    DEFINES += CLI_MODE
    message("Mode CLI activé - l'application fonctionnera sans interface graphique")

    macx {
        CONFIG -= app_bundle
        message("Génération de bundle macOS désactivée - un exécutable standard sera créé")
        # En mode CLI sur macOS, désactiver SFML par défaut.
        # L'utilisateur peut l'activer en passant CONFIG+=use_sfml à qmake.
        !use_sfml {
            DEFINES += NO_SFML
            message("Mode CLI sur macOS: SFML/CSFML désactivé (NO_SFML défini). Pour activer, passez CONFIG+=use_sfml à qmake.")
        } else {
            message("Mode CLI sur macOS: SFML/CSFML activé via CONFIG+=use_sfml.")
        }
    }

    # Pour Linux en mode CLI, SFML est toujours désactivé comme demandé par l'utilisateur pour RPi
    linux-g++ {
        # Cette condition est dans cli_mode -> linux-g++
        DEFINES += NO_SFML
        message("Mode CLI sur Linux: SFML/CSFML désactivé (NO_SFML défini).")
    }
}

# Nom et cible du projet
TARGET = CISYNTH_noGUI

# Version du projet
VERSION = 1.0.0

# Répertoires pour les fichiers générés
OBJECTS_DIR = build_nogui/obj
MOC_DIR = build_nogui/moc
RCC_DIR = build_nogui/rcc
UI_DIR = build_nogui/ui

# Définir qu'on utilise RtAudio
DEFINES += USE_RTAUDIO

# Fichiers sources C (tous les fichiers .c incluant main.c)
SOURCES += \
    src/core/display.c \
    src/core/dmx.c \
    src/core/error.c \
    src/core/kissfft/kiss_fft.c \
    src/core/kissfft/kiss_fftr.c \
    src/core/main.c \
    src/core/multithreading.c \
    src/core/shared.c \
    src/core/synth.c \
    src/core/synth_fft.c \
    src/core/udp.c \
    src/core/wave_generation.c \
    src/core/audio_rtaudio.cpp \
    src/core/midi_controller.cpp \
    src/core/ZitaRev1.cpp \
    src/core/reverb.cpp \
    src/core/pareq.cpp \
    src/core/three_band_eq.cpp

# Fichiers d'en-tête
HEADERS += \
    src/core/audio_rtaudio.h \
    src/core/audio_c_api.h \
    src/core/config.h \
    src/core/context.h \
    src/core/display.h \
    src/core/dmx.h \
    src/core/doublebuffer.h \
    src/core/error.h \
    src/core/kissfft/kiss_fft.h \
    src/core/kissfft/kiss_fftr.h \
    src/core/multithreading.h \
    src/core/midi_controller.h \
    src/core/shared.h \
    src/core/synth.h \
    src/core/synth_fft.h \
    src/core/udp.h \
    src/core/wave_generation.h \
    src/core/ZitaRev1.h \
    src/core/reverb.h \
    src/core/pareq.h \
    src/core/global.h \
    src/core/three_band_eq.h

# Chemins d'inclusion
INCLUDEPATH += src/core

# Configuration macOS spécifique
macx {
    # Si NO_SFML N'EST PAS défini, configurer SFML/CSFML
    !contains(DEFINES, NO_SFML) {
        message("macOS: Configuration de SFML/CSFML...")
        LIBS += -L/opt/homebrew/Cellar/sfml@2/2.6.2_1/lib \
                -lsfml-graphics -lsfml-window -lsfml-system \
                -lcsfml-graphics -lcsfml-window -lcsfml-system
        INCLUDEPATH += /opt/homebrew/Cellar/sfml@2/2.6.2_1/include
        # Assurer que les en-têtes CSFML sont trouvables.
        # /opt/homebrew/include est déjà ajouté plus bas, ce qui devrait suffire si CSFML y est.
        # Sinon, un chemin spécifique comme /opt/homebrew/opt/csfml/include pourrait être nécessaire.
    } else {
        message("macOS: SFML/CSFML n'est pas configuré (NO_SFML est défini).")
    }

    # Dépendances non-SFML et chemins d'inclusion de base pour Homebrew (toujours requis)
    LIBS += -L/opt/homebrew/lib -lfftw3 -lsndfile -lrtaudio -lrtmidi
    INCLUDEPATH += /opt/homebrew/include
    
    # Frameworks macOS requis (toujours requis)
    LIBS += -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework Cocoa
    
    # Configuration de signature simplifiée (signature ad-hoc correcte)
    QMAKE_MAC_IDENTITY = "-"        # Signature ad-hoc
    QMAKE_CODESIGN = "echo"         # Désactive la signature automatique
    
    # Désactiver la validation de la signature et les vérifications de sécurité
    QMAKE_LFLAGS += -Wl,-no_warn_duplicate_libraries -Wl,-headerpad_max_install_names
    
    # Variables supplémentaires pour le déploiement
    DESTDIR = $$OUT_PWD
    TARGET_APP = $${TARGET}
}

# Configuration pour Raspberry Pi / Linux ARM
linux-g++ {
    # Définir que nous sommes sur Linux
    DEFINES += __LINUX__
    
    # Détection de l'architecture pour optimisations spécifiques
    ARCH = $$system(uname -m)
    contains(ARCH, "aarch64") {
        message("Detected ARM64 architecture: $$ARCH")
        message("Applying Raspberry Pi 5 (ARM Cortex-A76) optimizations...")
        
        # Optimisations spécifiques ARM Cortex-A76 (Pi 5) - ARM64 uniquement
        QMAKE_CFLAGS += -march=armv8.2-a+fp16+rcpc+dotprod -mtune=cortex-a76
        QMAKE_CFLAGS += -ftree-vectorize -fvect-cost-model=cheap
        QMAKE_CFLAGS += -funroll-loops -fprefetch-loop-arrays
        QMAKE_CFLAGS += -fomit-frame-pointer -ffunction-sections -fdata-sections
        
        QMAKE_CXXFLAGS += -march=armv8.2-a+fp16+rcpc+dotprod -mtune=cortex-a76
        QMAKE_CXXFLAGS += -ftree-vectorize -fvect-cost-model=cheap
        QMAKE_CXXFLAGS += -funroll-loops -fprefetch-loop-arrays
        QMAKE_CXXFLAGS += -fomit-frame-pointer -ffunction-sections -fdata-sections
        
        # Optimisations mathématiques pour l'audio
        QMAKE_CFLAGS += -fno-signed-zeros -fno-trapping-math
        QMAKE_CFLAGS += -fassociative-math -ffinite-math-only
        QMAKE_CXXFLAGS += -fno-signed-zeros -fno-trapping-math  
        QMAKE_CXXFLAGS += -fassociative-math -ffinite-math-only
        
        # Optimisations de linking pour réduire la taille
        QMAKE_LFLAGS += -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed
        
        message("ARM64 Cortex-A76 optimizations applied for Pi 5")
    } else {
        contains(ARCH, "armv7l") {
            message("Detected ARM32 architecture: $$ARCH")
            message("Applying Raspberry Pi 4 (ARM Cortex-A72) optimizations...")
            
            # Optimisations spécifiques ARM Cortex-A72 (Pi 4)
            QMAKE_CFLAGS += -march=armv7-a+fp+simd -mtune=cortex-a72
            QMAKE_CFLAGS += -mfpu=neon-fp-armv8 -mfloat-abi=hard
            QMAKE_CXXFLAGS += -march=armv7-a+fp+simd -mtune=cortex-a72
            QMAKE_CXXFLAGS += -mfpu=neon-fp-armv8 -mfloat-abi=hard
            
            message("ARM32 Cortex-A72 optimizations applied for Pi 4")
        } else {
            message("Generic ARM architecture detected: $$ARCH")
            message("Applying generic ARM optimizations...")
            
            # Optimisations génériques ARM
            QMAKE_CFLAGS += -march=native -mtune=native
            QMAKE_CXXFLAGS += -march=native -mtune=native
        }
    }
    
    # Dépendances pour Linux (non-SFML)
    LIBS += -lfftw3 -lsndfile

    # Si NO_SFML N'EST PAS défini (c.a.d. mode non-CLI sur Linux, car cli_mode->linux-g++ définit NO_SFML)
    !contains(DEFINES, NO_SFML) {
        message("Linux (non-CLI): Tentative de configuration SFML/CSFML.")
        SFML_CONFIG_SUCCESS = false
        system("pkg-config --exists sfml-graphics") {
            message("sfml-graphics trouvé via pkg-config pour Linux (non-CLI).")
            LIBS += $(shell pkg-config --libs sfml-graphics sfml-window sfml-system)
            LIBS += -lcsfml-graphics -lcsfml-window -lcsfml-system # Lier CSFML manuellement
            SFML_CONFIG_SUCCESS = true
        }
        if (!$$SFML_CONFIG_SUCCESS) {
            message("pkg-config n'a pas trouvé sfml-graphics pour Linux (non-CLI). Tentative de liaison manuelle SFML/CSFML.")
            LIBS += -lcsfml-graphics -lcsfml-window -lcsfml-system
            LIBS += -lsfml-graphics -lsfml-window -lsfml-system
        } else {
            message("SFML et CSFML configurés pour Linux (non-CLI).")
        }
    } else {
        message("Linux: SFML/CSFML n'est pas configuré (NO_SFML est défini).")
    }
    
    # Threads POSIX (toujours requis pour Linux)
    LIBS += -lpthread
    
    # Support audio et MIDI
    LIBS += -lasound -lrtaudio -lrtmidi
    
    # C++17 pour support RtAudio moderne et optimisations
    CONFIG += c++17
}

# Configuration pour Windows (si besoin dans le futur)
win32 {
    # Support RtAudio et RtMidi pour Windows
    LIBS += -lrtaudio -lrtmidi -lole32 -lwinmm -ldsound
}

# Définir USE_DMX si nécessaire
# DEFINES += USE_DMX

# Définir PRINT_FPS pour afficher les informations de FPS
DEFINES += PRINT_FPS

# Supprimer -O2 du mode release
QMAKE_CFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE -= -O2

# Ajouter -O3 -ffast-math au lieu de -Ofast (déprécié)
QMAKE_CFLAGS_RELEASE += -O3 -ffast-math
QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math
