#!/bin/bash

# Script de compilation et déploiement de CISYNTH avec qmake
# Ce script remplace le flux de travail CMake par qmake pour éviter les problèmes de signature

echo "=== COMPILATION ET DÉPLOIEMENT DE CISYNTH AVEC QMAKE ==="
echo ""

# Déterminer le répertoire du projet
PROJECT_DIR="$(pwd)"
BUILD_DIR="${PROJECT_DIR}/build_qmake"
APP_PATH="${BUILD_DIR}/CISYNTH.app"

# Étape 1: Nettoyage
echo "1. Nettoyage du répertoire de build..."
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

# Étape 3: Déploiement avec macdeployqt (inclus avec Qt)
echo "4. Déploiement des dépendances Qt..."
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
    echo "ERREUR: Impossible de trouver macdeployqt. Vérifiez votre installation Qt."
    echo "Installation manuelle des frameworks requise."
else
    echo "Utilisation de macdeployqt: $MACDEPLOYQT_PATH"
    "$MACDEPLOYQT_PATH" "${APP_PATH}" -verbose=2 -no-strip
fi

echo "5. Suppression des attributs de quarantaine et re-signature ad-hoc..."
xattr -cr "${APP_PATH}"

# Étape supplémentaire pour s'assurer que l'application est correctement signée ad-hoc
echo "6. Application d'une signature ad-hoc profonde..."
codesign --deep --force -s - "${APP_PATH}"

# Étape 4: Création d'un script de lancement simple
# Ce script est beaucoup plus simple que celui utilisé avec CMake car qmake gère mieux les dépendances
LAUNCH_SCRIPT="${BUILD_DIR}/lancer_cisynth.sh"

cat > "${LAUNCH_SCRIPT}" << EOL
#!/bin/bash

# Script de lancement pour CISYNTH (version qmake)
# Cette version nécessite beaucoup moins de contournements que la version CMake

APP_PATH="${APP_PATH}"

# Lancement direct de l'application
echo "Lancement de CISYNTH..."
open "\${APP_PATH}"
EOL

chmod +x "${LAUNCH_SCRIPT}"

echo ""
echo "=== INSTALLATION TERMINÉE ==="
echo ""
echo "Pour lancer CISYNTH, exécutez:"
echo "${LAUNCH_SCRIPT}"
echo ""
echo "Pour installer CISYNTH sur un autre Mac:"
echo "1. Copiez simplement le dossier ${APP_PATH}"
echo "2. Double-cliquez sur l'application ou utilisez 'open' pour la lancer"
echo ""
echo "Avantages de cette approche qmake:"
echo "- Signature automatique et cohérente"
echo "- Pas besoin de scripts complexes de contournement"
echo "- Compatible avec Gatekeeper sans modification extensive"
echo "- Structure de bundle standard"
echo ""

# Tentative d'exécution automatique
echo "Tentative d'exécution automatique..."
open "${APP_PATH}"
