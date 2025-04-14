#!/bin/bash

# Script de reconstruction de CISYNTH avec focus sur la résolution du conflit SFML
# Ce script nettoie complètement l'environnement et reconstruit l'application

echo "=== RECONSTRUCTION CISYNTH AVEC CORRECTION SFML ==="
echo ""

# Déterminer le répertoire du projet
PROJECT_DIR="$(pwd)"
BUILD_DIR="${PROJECT_DIR}/build_qmake"
APP_PATH="${BUILD_DIR}/CISYNTH.app"
FRAMEWORKS_DIR="${APP_PATH}/Contents/Frameworks"

# Étape 1: Nettoyage complet
echo "1. Nettoyage complet des répertoires de build..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Étape 2: Configuration et compilation avec qmake
echo "2. Configuration avec qmake..."
cd "${BUILD_DIR}"
qmake ../CISYNTH.pro

echo "3. Compilation du projet..."
make

# Vérifier si la compilation a réussi
if [ ! -d "${APP_PATH}" ]; then
    echo "ERREUR: La compilation a échoué. L'application n'a pas été créée."
    exit 1
fi

# Étape 3: Nettoyage spécifique des frameworks SFML conflictuels
echo "4. Nettoyage des bibliothèques SFML conflictuelles..."
echo "   Suppression des bibliothèques SFML 3.0 si elles existent..."
rm -f "${FRAMEWORKS_DIR}/libsfml-graphics.3.0.dylib" 2>/dev/null
rm -f "${FRAMEWORKS_DIR}/libsfml-window.3.0.dylib" 2>/dev/null
rm -f "${FRAMEWORKS_DIR}/libsfml-system.3.0.dylib" 2>/dev/null

# Étape 4: Vérification des liens des bibliothèques SFML
echo "5. Vérification des dépendances des bibliothèques SFML..."
otool -L "${FRAMEWORKS_DIR}/libsfml-graphics-2.6.dylib" | grep -i sfml
otool -L "${FRAMEWORKS_DIR}/libsfml-window-2.6.dylib" | grep -i sfml
otool -L "${FRAMEWORKS_DIR}/libsfml-system-2.6.dylib" | grep -i sfml

# Étape 5: Déploiement
echo "6. Déploiement des dépendances..."
# Utiliser le chemin complet vers macdeployqt et forcer le mode verbose
MACDEPLOYQT_PATH=$(which macdeployqt)
if [ -z "$MACDEPLOYQT_PATH" ]; then
    # Essayer de trouver macdeployqt dans les chemins typiques de Qt
    MACDEPLOYQT_PATH="/opt/homebrew/Cellar/qt@5/5.15.16_1/bin/macdeployqt"
    if [ ! -f "$MACDEPLOYQT_PATH" ]; then
        # Chercher dans tous les sous-répertoires de Qt
        MACDEPLOYQT_PATH=$(find /opt/homebrew/Cellar -name "macdeployqt" -type f | head -n 1)
    fi
fi

if [ -z "$MACDEPLOYQT_PATH" ]; then
    echo "AVERTISSEMENT: Impossible de trouver macdeployqt. Déploiement manuel requis."
else
    echo "Utilisation de macdeployqt: $MACDEPLOYQT_PATH"
    "$MACDEPLOYQT_PATH" "${APP_PATH}" -verbose=2 -no-strip
fi

# Étape 6: Nettoyage des attributs de quarantaine et signature ad-hoc
echo "7. Suppression des attributs de quarantaine et signature ad-hoc..."
xattr -cr "${APP_PATH}"
codesign --deep --force -s - "${APP_PATH}"

# Étape 7: Vérification finale des bibliothèques
echo "8. Vérification finale des bibliothèques SFML dans le bundle..."
find "${FRAMEWORKS_DIR}" -name "libsfml*" | sort

# Création d'un script de lancement de débogage
LAUNCH_SCRIPT="${BUILD_DIR}/launch_cisynth_with_debug.sh"

cat > "${LAUNCH_SCRIPT}" << EOL
#!/bin/bash

# Script de lancement pour CISYNTH avec des variables d'environnement de débogage
# Version optimisée pour résoudre les conflits SFML

APP_PATH="${APP_PATH}"
FRAMEWORKS_PATH="${APP_PATH}/Contents/Frameworks"

# Configuration spécifique pour les bibliothèques SFML
export DYLD_FRAMEWORK_PATH="${FRAMEWORKS_PATH}:${DYLD_FRAMEWORK_PATH}"
export DYLD_LIBRARY_PATH="${FRAMEWORKS_PATH}:${DYLD_LIBRARY_PATH}"
export DYLD_PRINT_LIBRARIES=1

# Débogage
echo "Lancement de CISYNTH (avec informations de débogage)..."
echo "APP_PATH: ${APP_PATH}"
echo "FRAMEWORKS_PATH: ${FRAMEWORKS_PATH}"

# Lancement direct pour voir les erreurs dans le terminal
"${APP_PATH}/Contents/MacOS/CISYNTH"
EOL

chmod +x "${LAUNCH_SCRIPT}"

echo ""
echo "=== RECONSTRUCTION TERMINÉE ==="
echo ""
echo "Pour exécuter CISYNTH avec débogage, utilisez:"
echo "${LAUNCH_SCRIPT}"
echo ""
echo "Si le problème persiste, vérifiez les messages d'erreur dans le terminal."
