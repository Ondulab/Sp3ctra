# Configuration du projet
CONFIG += c++17
CONFIG += sdk_no_version_check
CONFIG += console

# Option pour le mode CLI (sans interface graphique)
cli_mode {
    DEFINES += CLI_MODE
    message("Mode CLI activé - l'application fonctionnera sans interface graphique")
    
    # Désactiver la génération de bundle sur macOS en mode CLI
    macx {
        CONFIG -= app_bundle
        message("Génération de bundle macOS désactivée - un exécutable standard sera créé")
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
    src/core/main.c \
    src/core/multithreading.c \
    src/core/shared.c \
    src/core/synth.c \
    src/core/udp.c \
    src/core/wave_generation.c \
    src/core/audio_rtaudio.cpp

# Fichiers d'en-tête
HEADERS += \
    src/core/audio_rtaudio.h \
    src/core/config.h \
    src/core/context.h \
    src/core/display.h \
    src/core/dmx.h \
    src/core/doublebuffer.h \
    src/core/error.h \
    src/core/multithreading.h \
    src/core/shared.h \
    src/core/synth.h \
    src/core/udp.h \
    src/core/wave_generation.h

# Chemins d'inclusion
INCLUDEPATH += src/core

# Configuration macOS spécifique
macx {
    # Dépendances via Homebrew
    LIBS += -L/opt/homebrew/lib \
        -lfftw3 \
        -lsndfile \
        -L/opt/homebrew/Cellar/sfml@2/2.6.2_1/lib \
        -lsfml-graphics -lsfml-window -lsfml-system \
        -lcsfml-graphics -lcsfml-window -lcsfml-system \
        -lrtaudio
    
    # Chemins d'inclusion pour Homebrew
    INCLUDEPATH += /opt/homebrew/include
    INCLUDEPATH += /opt/homebrew/Cellar/sfml@2/2.6.2_1/include
    
    # Frameworks macOS requis
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

# Configuration pour Jetson Nano (Linux ARM)
linux-g++ {
    # Dépendances pour Linux
    LIBS += -lfftw3 -lsndfile -lsfml-graphics -lsfml-window -lsfml-system -lcsfml-graphics -lcsfml-window -lcsfml-system
    
    # Threads POSIX
    LIBS += -lpthread
    
    # Support audio
    LIBS += -lasound -lrtaudio
    
    # C++11 pour support RtAudio
    CONFIG += c++11
}

# Configuration pour Windows (si besoin dans le futur)
win32 {
    # Support RtAudio Windows
    LIBS += -lrtaudio -lole32 -lwinmm -ldsound
}

# Définir USE_DMX si nécessaire
# DEFINES += USE_DMX

# Définir PRINT_FPS pour afficher les informations de FPS
DEFINES += PRINT_FPS
