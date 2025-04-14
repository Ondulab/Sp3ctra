#!/bin/bash

# Script pour corriger le crash CISYNTH en supprimant les bibliothèques SFML 3.0 conflictuelles
# Ce script est plus simple et se concentre uniquement sur la résolution du conflit SFML

echo "=== CORRECTION DU CRASH CISYNTH (CONFLIT SFML) ==="
echo ""

# Déterminer le répertoire du projet
PROJECT_DIR="$(pwd)"
BUILD_DIR="${PROJECT_DIR}/build_qmake"
APP_PATH="${BUILD_DIR}/CISYNTH.app"
FRAMEWORKS_DIR="${APP_PATH}/Contents/Frameworks"

# Compilation avec qmake si nécessaire
if [ ! -d "${APP_PATH}" ]; then
    echo "1. L'application n'existe pas, compilation avec qmake..."
    cd "${BUILD_DIR}" || mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"
    qmake ../CISYNTH.pro
    make
fi

if [ ! -d "${APP_PATH}" ]; then
    echo "ERREUR: La compilation a échoué. Utilisez d'abord ./build_with_qmake.sh pour compiler."
    exit 1
fi

# Vérifier les bibliothèques SFML présentes
echo "2. Vérification des bibliothèques SFML dans le bundle..."
mkdir -p "${FRAMEWORKS_DIR}"
find "${FRAMEWORKS_DIR}" -name "libsfml*.dylib" | sort

# Supprimer les bibliothèques SFML 3.0 si elles existent
echo "3. Suppression des bibliothèques SFML 3.0 conflictuelles..."
rm -f "${FRAMEWORKS_DIR}/libsfml-graphics.3.0.dylib" 2>/dev/null
rm -f "${FRAMEWORKS_DIR}/libsfml-window.3.0.dylib" 2>/dev/null
rm -f "${FRAMEWORKS_DIR}/libsfml-system.3.0.dylib" 2>/dev/null

# Lancer macdeployqt pour s'assurer que toutes les dépendances sont incluses
echo "4. Exécution de macdeployqt pour inclure les bonnes versions de SFML..."
MACDEPLOYQT_PATH=$(which macdeployqt)
if [ -z "$MACDEPLOYQT_PATH" ]; then
    MACDEPLOYQT_PATH=$(find /opt/homebrew/Cellar -name "macdeployqt" -type f | head -n 1)
fi

if [ -n "$MACDEPLOYQT_PATH" ]; then
    echo "Utilisation de macdeployqt: $MACDEPLOYQT_PATH"
    "$MACDEPLOYQT_PATH" "${APP_PATH}" -verbose=1
else
    echo "AVERTISSEMENT: macdeployqt non trouvé, copie manuelle des bibliothèques SFML..."
    # Copie manuelle des bibliothèques SFML 2.6
    cp -f /opt/homebrew/lib/libsfml-graphics.2.6*.dylib "${FRAMEWORKS_DIR}/" 2>/dev/null
    cp -f /opt/homebrew/lib/libsfml-window.2.6*.dylib "${FRAMEWORKS_DIR}/" 2>/dev/null
    cp -f /opt/homebrew/lib/libsfml-system.2.6*.dylib "${FRAMEWORKS_DIR}/" 2>/dev/null
    cp -f /opt/homebrew/lib/libcsfml-*.dylib "${FRAMEWORKS_DIR}/" 2>/dev/null
fi

# Vérifier à nouveau les bibliothèques SFML présentes
echo "5. Vérification des bibliothèques SFML après correction..."
find "${FRAMEWORKS_DIR}" -name "libsfml*.dylib" | sort

# Suppression des attributs de quarantaine et signature
echo "6. Suppression des attributs de quarantaine et signature ad-hoc..."
xattr -cr "${APP_PATH}"
codesign --deep --force -s - "${APP_PATH}"

# Création d'un script de lancement direct pour le débogage
LAUNCH_SCRIPT="${BUILD_DIR}/launch_cisynth_debug.sh"

cat > "${LAUNCH_SCRIPT}" << EOL
#!/bin/bash

# Script de lancement direct pour CISYNTH (version débogage)
# Ce script lance directement l'exécutable pour voir les erreurs dans le terminal

APP_PATH="${APP_PATH}"
EXEC_PATH="${APP_PATH}/Contents/MacOS/CISYNTH"

# Configuration des chemins pour les bibliothèques
export DYLD_LIBRARY_PATH="${APP_PATH}/Contents/Frameworks:${DYLD_LIBRARY_PATH}"
export DYLD_FRAMEWORK_PATH="${APP_PATH}/Contents/Frameworks:${DYLD_FRAMEWORK_PATH}"

# Activation du débogage des bibliothèques
export DYLD_PRINT_LIBRARIES=1

echo "Lancement de CISYNTH en mode débogage..."
"\${EXEC_PATH}"
EOL

chmod +x "${LAUNCH_SCRIPT}"

# Création d'un script de lancement normal
NORMAL_LAUNCH_SCRIPT="${BUILD_DIR}/launch_cisynth.sh"

cat > "${NORMAL_LAUNCH_SCRIPT}" << EOL
#!/bin/bash

# Script de lancement normal pour CISYNTH
APP_PATH="${APP_PATH}"

echo "Lancement de CISYNTH..."
open "\${APP_PATH}"
EOL

chmod +x "${NORMAL_LAUNCH_SCRIPT}"

echo ""
echo "=== CORRECTION TERMINÉE ==="
echo ""
echo "Pour lancer CISYNTH normalement:"
echo "${NORMAL_LAUNCH_SCRIPT}"
echo ""
echo "Pour lancer CISYNTH en mode débogage (voir les erreurs):"
echo "${LAUNCH_SCRIPT}"
echo ""

# Lancement automatique
echo "Tentative de lancement automatique..."
"${NORMAL_LAUNCH_SCRIPT}"
