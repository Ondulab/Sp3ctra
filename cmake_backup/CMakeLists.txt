cmake_minimum_required(VERSION 3.16)
project(CISYNTH VERSION 1.0 LANGUAGES C CXX)

# Configuration C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configuration Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Trouver Qt
find_package(Qt6 COMPONENTS Core Gui Widgets Network QUIET)
# Si Qt6 n'est pas disponible, essayer Qt5
if (NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Gui Widgets Network REQUIRED)
    message(STATUS "Using Qt5")
else()
    message(STATUS "Using Qt6")
endif()

# Trouver les autres dépendances
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW REQUIRED fftw3)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(SNDFILE REQUIRED sndfile)

# Trouver SFML (utiliser explicitement la version 2.6)
set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/sfml@2/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(SFML REQUIRED sfml-graphics sfml-window sfml-system)
message(STATUS "Found SFML 2.6 via pkg-config")

# Trouver CSFML
pkg_check_modules(CSFML QUIET csfml-graphics csfml-window csfml-system)
if (NOT CSFML_FOUND)
    # Si CSFML n'est pas trouvé, utiliser les chemins Homebrew par défaut
    set(CSFML_INCLUDE_DIRS "/opt/homebrew/Cellar/csfml/2.6.1_1/include")
    set(CSFML_LIBRARIES "-L/opt/homebrew/lib -lcsfml-graphics -lcsfml-window -lcsfml-system")
    message(STATUS "Using default Homebrew paths for CSFML")
else()
    message(STATUS "Found CSFML via pkg-config")
endif()

# Les fichiers sources et les répertoires existent déjà
# Aucun besoin de les copier ou de créer des répertoires

# Sources C (tous les fichiers .c sauf main.c)
file(GLOB C_SOURCES 
    "src/core/*.c"
)
list(FILTER C_SOURCES EXCLUDE REGEX ".*main\\.c$")

# Sources C++/Qt
set(CXX_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/AudioVisualizer.cpp
)

# Créer l'exécutable
add_executable(CISYNTH MACOSX_BUNDLE ${C_SOURCES} ${CXX_SOURCES})

# Inclure les en-têtes
target_include_directories(CISYNTH PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${FFTW_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS}
    ${SFML_INCLUDE_DIRS}
    ${CSFML_INCLUDE_DIRS}
)

# Ajouter le fichier doublebuffer.h aux sources
set_property(TARGET CISYNTH APPEND PROPERTY SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/doublebuffer.h
)

# Lier les bibliothèques
if (Qt6_FOUND)
    target_link_libraries(CISYNTH PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
        ${FFTW_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${SNDFILE_LIBRARIES}
        ${SFML_LIBRARIES}
        ${CSFML_LIBRARIES}
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework Cocoa"
    )
else()
    target_link_libraries(CISYNTH PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network
        ${FFTW_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${SNDFILE_LIBRARIES}
        ${SFML_LIBRARIES}
        ${CSFML_LIBRARIES}
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework Cocoa"
    )
endif()

# Configuration du bundle macOS
set_target_properties(CISYNTH PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.ondulab.cisynth
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
)

# Configuration de la signature de code pour macOS simplifiée
if(APPLE)
    # Utiliser une signature ad-hoc pour permettre le débogage
    set_target_properties(CISYNTH PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
        XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--deep --force"
    )
    
    # Ajouter le fichier d'autorisations (entitlements) si nécessaire
    set_target_properties(CISYNTH PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/CISYNTH.entitlements"
    )
endif()

# Installation
install(TARGETS CISYNTH
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Créer un script de post-traitement pour déployer et désactiver la validation des signatures
# Cette approche évite les problèmes avec install_name_tool
if(APPLE)
    if (Qt6_FOUND)
        set(MACDEPLOYQT_EXECUTABLE ${Qt6_DIR}/../../../bin/macdeployqt)
    else()
        set(MACDEPLOYQT_EXECUTABLE ${Qt5_DIR}/../../../bin/macdeployqt)
    endif()
    
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fix_app.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/fix_app.sh"
        @ONLY
    )
    
    # Création d'un fichier d'instructions à afficher après le build
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/instructions.txt"
"
===== INSTRUCTIONS IMPORTANTES =====
Pour finaliser l'installation et exécuter l'application:
1. Exécutez: ${CMAKE_CURRENT_BINARY_DIR}/fix_app.sh
2. Cette commande supprimera les attributs de quarantaine et préparera l'application
===== FIN DES INSTRUCTIONS =====
")

    add_custom_command(TARGET CISYNTH POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Deploying Qt dependencies with macdeployqt..."
        COMMAND ${MACDEPLOYQT_EXECUTABLE} $<TARGET_FILE_DIR:CISYNTH>/../.. -no-strip
        
        COMMAND ${CMAKE_COMMAND} -E echo "Creating fix_app.sh script for manual execution..."
        COMMAND chmod +x "${CMAKE_CURRENT_BINARY_DIR}/fix_app.sh"
        
        # Afficher les instructions depuis le fichier
        COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_BINARY_DIR}/instructions.txt"
    )
endif()
