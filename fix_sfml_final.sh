#!/bin/bash

# Script pour résoudre définitivement le problème de conflit SFML dans CISYNTH
# Cette version utilise explicitement SFML 2.6 de la version keg-only

echo "=== RÉSOLUTION DÉFINITIVE DU PROBLÈME SFML DANS CISYNTH ==="
echo ""

# Déterminer les chemins
PROJECT_DIR="$(pwd)"
BUILD_DIR="${PROJECT_DIR}/build_qmake"
APP_PATH="${BUILD_DIR}/CISYNTH.app"
FRAMEWORKS_DIR="${APP_PATH}/Contents/Frameworks"

# Les bibliothèques SFML nécessaires
SFML2_PATH="/opt/homebrew/Cellar/sfml@2/2.6.2_1/lib"
CSFML_PATH="/opt/homebrew/lib"

# Étape 1: Nettoyage complet
echo "1. Nettoyage complet des répertoires de build..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Étape 2: Compilation avec qmake
echo "2. Compilation avec la configuration SFML 2.6..."
cd "${BUILD_DIR}"
qmake ../CISYNTH.pro
make

# Vérifier si la compilation a réussi
if [ ! -d "${APP_PATH}" ]; then
    echo "ERREUR: La compilation a échoué!"
    exit 1
fi

# Étape 3: Créer le répertoire Frameworks s'il n'existe pas
echo "3. Préparation du bundle..."
mkdir -p "${FRAMEWORKS_DIR}"

# Étape 4: Copier manuellement les bibliothèques SFML 2.6
echo "4. Copie des bibliothèques SFML 2.6 dans le bundle..."
cp "${SFML2_PATH}/libsfml-graphics.2.6.dylib" "${FRAMEWORKS_DIR}/"
cp "${SFML2_PATH}/libsfml-window.2.6.dylib" "${FRAMEWORKS_DIR}/"
cp "${SFML2_PATH}/libsfml-system.2.6.dylib" "${FRAMEWORKS_DIR}/"
cp "${CSFML_PATH}/libcsfml-graphics.2.6.dylib" "${FRAMEWORKS_DIR}/"
cp "${CSFML_PATH}/libcsfml-window.2.6.dylib" "${FRAMEWORKS_DIR}/"
cp "${CSFML_PATH}/libcsfml-system.2.6.dylib" "${FRAMEWORKS_DIR}/"

# Étape 5: Vérifier que les bibliothèques SFML 3.0 ne sont pas présentes
echo "5. Vérification de l'absence de bibliothèques SFML 3.0 conflictuelles..."
rm -f "${FRAMEWORKS_DIR}/libsfml-graphics.3.0.dylib" 2>/dev/null
rm -f "${FRAMEWORKS_DIR}/libsfml-window.3.0.dylib" 2>/dev/null
rm -f "${FRAMEWORKS_DIR}/libsfml-system.3.0.dylib" 2>/dev/null

# Étape 6: Correction des chemins de recherche de bibliothèques dans l'exécutable
echo "6. Correction des chemins de bibliothèques..."
install_name_tool -change "/opt/homebrew/opt/sfml@2/lib/libsfml-graphics.2.6.dylib" "@executable_path/../Frameworks/libsfml-graphics.2.6.dylib" "${APP_PATH}/Contents/MacOS/CISYNTH"
install_name_tool -change "/opt/homebrew/opt/sfml@2/lib/libsfml-window.2.6.dylib" "@executable_path/../Frameworks/libsfml-window.2.6.dylib" "${APP_PATH}/Contents/MacOS/CISYNTH"
install_name_tool -change "/opt/homebrew/opt/sfml@2/lib/libsfml-system.2.6.dylib" "@executable_path/../Frameworks/libsfml-system.2.6.dylib" "${APP_PATH}/Contents/MacOS/CISYNTH"
install_name_tool -change "/opt/homebrew/lib/libcsfml-graphics.2.6.dylib" "@executable_path/../Frameworks/libcsfml-graphics.2.6.dylib" "${APP_PATH}/Contents/MacOS/CISYNTH"
install_name_tool -change "/opt/homebrew/lib/libcsfml-window.2.6.dylib" "@executable_path/../Frameworks/libcsfml-window.2.6.dylib" "${APP_PATH}/Contents/MacOS/CISYNTH"
install_name_tool -change "/opt/homebrew/lib/libcsfml-system.2.6.dylib" "@executable_path/../Frameworks/libcsfml-system.2.6.dylib" "${APP_PATH}/Contents/MacOS/CISYNTH"

# Correction récursive pour les dépendances des bibliothèques SFML
echo "7. Correction des dépendances entre bibliothèques SFML..."
install_name_tool -change "/opt/homebrew/opt/sfml@2/lib/libsfml-window.2.6.dylib" "@executable_path/../Frameworks/libsfml-window.2.6.dylib" "${FRAMEWORKS_DIR}/libsfml-graphics.2.6.dylib"
install_name_tool -change "/opt/homebrew/opt/sfml@2/lib/libsfml-system.2.6.dylib" "@executable_path/../Frameworks/libsfml-system.2.6.dylib" "${FRAMEWORKS_DIR}/libsfml-graphics.2.6.dylib"
install_name_tool -change "/opt/homebrew/opt/sfml@2/lib/libsfml-system.2.6.dylib" "@executable_path/../Frameworks/libsfml-system.2.6.dylib" "${FRAMEWORKS_DIR}/libsfml-window.2.6.dylib"

# Étape 7: Déploiement des autres dépendances Qt
echo "8. Déploiement des autres dépendances avec macdeployqt..."
MACDEPLOYQT_PATH=$(which macdeployqt)
if [ -n "$MACDEPLOYQT_PATH" ]; then
    echo "Utilisation de macdeployqt: $MACDEPLOYQT_PATH"
    "$MACDEPLOYQT_PATH" "${APP_PATH}" -verbose=1
fi

# Étape 8: Suppression des attributs de quarantaine et signature
echo "9. Suppression des attributs de quarantaine et signature ad-hoc..."
xattr -cr "${APP_PATH}"
codesign --deep --force -s - "${APP_PATH}"

# Étape 9: Créer un script de lancement
LAUNCH_SCRIPT="${BUILD_DIR}/launch_cisynth.sh"

cat > "${LAUNCH_SCRIPT}" << EOL
#!/bin/bash

# Script de lancement pour CISYNTH
APP_PATH="${APP_PATH}"

echo "Lancement de CISYNTH..."
open "\${APP_PATH}"
EOL

chmod +x "${LAUNCH_SCRIPT}"

echo ""
echo "=== RÉSOLUTION TERMINÉE ==="
echo ""
echo "Pour lancer CISYNTH, exécutez:"
echo "${LAUNCH_SCRIPT}"
echo ""
echo "Vous pouvez également double-cliquer sur l'application directement:"
echo "${APP_PATH}"
echo ""

# Lancement automatique
echo "Tentative de lancement automatique..."
open "${APP_PATH}"
