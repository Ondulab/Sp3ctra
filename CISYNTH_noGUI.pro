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
    # Dépendances via Homebrew
    LIBS += -L/opt/homebrew/lib \
        -lfftw3 \
        -lsndfile \
        -L/opt/homebrew/Cellar/sfml@2/2.6.2_1/lib \
        -lsfml-graphics -lsfml-window -lsfml-system \
        -lcsfml-graphics -lcsfml-window -lcsfml-system \
        -lrtaudio \
        -lrtmidi
    
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
    # Définir que nous sommes sur Linux
    DEFINES += __LINUX__
    
    # Dépendances pour Linux
    LIBS += -lfftw3 -lsndfile
    
    # Tentative de configuration SFML et CSFML
    # On suppose que si on est sous Linux pour ce projet, on veut SFML/CSFML
    # car install_dependencies_raspberry.sh les installe.

    SFML_CONFIG_SUCCESS = false
    # Essayer de trouver sfml-graphics et csfml-graphics via pkg-config
    system("pkg-config --exists sfml-graphics && pkg-config --exists csfml-graphics") {
        message("SFML et CSFML trouvés via pkg-config.")
        # Utiliser pkg-config pour obtenir les flags de liaison et d'inclusion
        # Note: CSFML dépend de SFML, donc lier les deux est généralement nécessaire.
        LIBS += $(shell pkg-config --libs sfml-graphics sfml-window sfml-system csfml-graphics csfml-window csfml-system)
        # Les chemins d'inclusion sont souvent gérés par les paquets -dev, mais peuvent être ajoutés si nécessaire:
        # INCLUDEPATH += $(shell pkg-config --cflags sfml-graphics csfml-graphics)
        SFML_CONFIG_SUCCESS = true
    }

    if (!$$SFML_CONFIG_SUCCESS) {
        message("pkg-config n'a pas trouvé sfml-graphics et/ou csfml-graphics. Tentative de liaison manuelle.")
        message("Veuillez vérifier que libsfml-dev, libcsfml-dev et pkg-config sont correctement installés.")
        # Ajout manuel des bibliothèques. Cela suppose qu'elles sont dans les chemins standards.
        # Ordre modifié: CSFML d'abord, puis SFML car CSFML dépend de SFML.
        LIBS += -lcsfml-graphics -lcsfml-window -lcsfml-system
        LIBS += -lsfml-graphics -lsfml-window -lsfml-system
        # Si des erreurs persistent, vous pourriez avoir besoin d'ajouter explicitement des chemins de bibliothèques
        # avec -L/chemin/vers/libs et des chemins d'inclusion avec INCLUDEPATH += /chemin/vers/includes
        # Toutefois, une installation correcte via apt devrait rendre cela inutile.
    } else {
        message("SFML et CSFML configurés avec succès via pkg-config.")
    }

    # Si vous souhaitez avoir une option pour compiler SANS SFML/CSFML (par exemple, pour un mode purement CLI sans affichage),
    # vous devriez introduire une variable de configuration (ex: CONFIG += no_sfml_build)
    # et conditionner l'ensemble de ce bloc SFML/CSFML avec cette variable.
    # Pour l'instant, l'objectif est de résoudre le problème de liaison.
    # Si, après ces tentatives, la liaison échoue toujours, le problème est probablement plus profond :
    # - Bibliothèques non installées correctement (vérifiez avec `dpkg -L libsfml-dev libcsfml-dev`)
    # - Versions incompatibles
    # - Problème avec l'environnement de l'éditeur de liens (LD_LIBRARY_PATH, etc., bien que moins probable pour la compilation)
    # - pkg-config lui-même non installé (le script d'installation ne l'installe pas explicitement)
    #   Vous pouvez l'installer avec: sudo apt install pkg-config
    
    # Threads POSIX
    LIBS += -lpthread
    
    # Support audio et MIDI
    LIBS += -lasound -lrtaudio -lrtmidi
    
    # C++11 pour support RtAudio
    CONFIG += c++11
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
# QMAKE_CFLAGS_RELEASE -= -O2
# QMAKE_CXXFLAGS_RELEASE -= -O2

# Ajouter Ofast
# QMAKE_CFLAGS_RELEASE += -Ofast
# QMAKE_CXXFLAGS_RELEASE += -Ofast
