# Configuration Qt
QT += core gui widgets network

# Configuration du projet
CONFIG += c++17
CONFIG += sdk_no_version_check

# Nom et cible du projet
TARGET = CISYNTH

# Version du projet
VERSION = 1.0.0

# Icône de l'application pour macOS
# Note: L'icône est manquante, nous la désactivons pour le moment
# ICON = resources/CISYNTH.icns

# Informations sur le bundle macOS
QMAKE_TARGET_BUNDLE_PREFIX = com.ondulab
QMAKE_BUNDLE = cisynth

# Répertoires pour les fichiers générés
OBJECTS_DIR = build/obj
MOC_DIR = build/moc
RCC_DIR = build/rcc
UI_DIR = build/ui

# Fichiers sources C (tous les fichiers .c sauf main.c)
SOURCES += \
    src/core/audio.c \
    src/core/display.c \
    src/core/dmx.c \
    src/core/error.c \
    src/core/multithreading.c \
    src/core/shared.c \
    src/core/synth.c \
    src/core/udp.c \
    src/core/wave_generation.c

# Fichiers sources C++/Qt
SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/AudioVisualizer.cpp

# Fichiers d'en-tête
HEADERS += \
    src/core/audio.h \
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
    src/core/wave_generation.h \
    src/MainWindow.h \
    src/AudioVisualizer.h

# Chemins d'inclusion
INCLUDEPATH += src/core

# Configuration macOS spécifique
macx {
    # Dépendances via Homebrew
    LIBS += -L/opt/homebrew/lib \
        -lfftw3 \
        -lcairo \
        -lsndfile \
        -L/opt/homebrew/Cellar/sfml@2/2.6.2_1/lib \
        -lsfml-graphics -lsfml-window -lsfml-system \
        -lcsfml-graphics -lcsfml-window -lcsfml-system
    
    # Chemins d'inclusion pour Homebrew
    INCLUDEPATH += /opt/homebrew/include
    INCLUDEPATH += /opt/homebrew/Cellar/sfml@2/2.6.2_1/include
    
    # Frameworks macOS requis
    LIBS += -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework Cocoa
    
    # Chemins d'inclusion pour Homebrew
    INCLUDEPATH += /opt/homebrew/include
    
    # Fichier d'autorisations (entitlements) et Info.plist
    QMAKE_INFO_PLIST = resources/Info.plist
    
    # Configuration de signature simplifiée (signature ad-hoc correcte)
    QMAKE_MAC_IDENTITY = "-"        # Signature ad-hoc
    QMAKE_CODESIGN = "echo"         # Désactive la signature automatique
    
    # Désactiver la validation de la signature et les vérifications de sécurité
    QMAKE_LFLAGS += -Wl,-no_warn_duplicate_libraries -Wl,-headerpad_max_install_names
    
    # Variables supplémentaires pour le déploiement
    DESTDIR = $$OUT_PWD
    TARGET_APP = $${TARGET}.app
}

# Configuration de déploiement
# qmake gère automatiquement le déploiement de façon plus propre que CMake
