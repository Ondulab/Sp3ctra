#!/bin/bash

# Script complet pour nettoyer, reconstruire et préparer CISYNTH
# Ce script contourne les problèmes de signature de code sur macOS

echo "=== NETTOYAGE ET RECONSTRUCTION DE CISYNTH ==="
echo ""

# Déterminer le répertoire du projet
PROJECT_DIR="$(pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
APP_PATH="${BUILD_DIR}/CISYNTH.app"
BINARY_PATH="${APP_PATH}/Contents/MacOS/CISYNTH"
FRAMEWORKS_PATH="${APP_PATH}/Contents/Frameworks"

# Étape 1: Nettoyage complet
echo "1. Nettoyage du répertoire build..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Étape 2: Configuration et compilation
echo "2. Configuration avec CMake..."
cd "${BUILD_DIR}"
cmake ..

echo "3. Compilation du projet..."
cmake --build .

# Vérifier si la compilation a réussi
if [ ! -f "${BINARY_PATH}" ]; then
    echo "ERREUR: La compilation a échoué. L'exécutable n'a pas été créé."
    exit 1
fi

# Étape 3: Suppression des attributs de quarantaine et des signatures
echo "4. Suppression des attributs de quarantaine et des signatures..."
xattr -cr "${APP_PATH}"
codesign --remove-signature "${APP_PATH}" 2>/dev/null || true

# Trouver tous les frameworks et bibliothèques
echo "5. Traitement des frameworks et bibliothèques..."
find "${FRAMEWORKS_PATH}" -type f \( -name "*.framework*" -o -name "*.dylib" \) | while read file; do
    echo "   - Suppression de la signature de: $(basename "$file")"
    xattr -cr "$file" 2>/dev/null || true
    codesign --remove-signature "$file" 2>/dev/null || true
done

# Créer le wrapper script avec toutes les variables d'environnement nécessaires
WRAPPER_SCRIPT="${BUILD_DIR}/run_cisynth.sh"
echo "6. Création du script de lancement..."

cat > "${WRAPPER_SCRIPT}" << EOL
#!/bin/bash

# Script de lancement pour CISYNTH avec contournements des restrictions de sécurité
APP="${APP_PATH}"
FRAMEWORKS="${FRAMEWORKS_PATH}"

# Configuration de l'environnement pour contourner les restrictions
export DYLD_LIBRARY_PATH="\${FRAMEWORKS}:\$DYLD_LIBRARY_PATH"
export DYLD_FORCE_FLAT_NAMESPACE=1
export DYLD_SHARED_REGION=avoid
# Décommenter la ligne suivante pour le débogage
# export DYLD_PRINT_LIBRARIES=1

echo "Lancement de CISYNTH avec variables d'environnement spéciales..."
"\${APP}/Contents/MacOS/CISYNTH" "\$@"
EOL

chmod +x "${WRAPPER_SCRIPT}"

# Créer un script pour déployer en dehors du répertoire de développement
DEPLOY_SCRIPT="${BUILD_DIR}/deploy_cisynth.sh"
echo "7. Création du script de déploiement..."

cat > "${DEPLOY_SCRIPT}" << EOL
#!/bin/bash

# Script pour déployer CISYNTH sur un autre Mac
# Ce script doit être exécuté avec l'application CISYNTH.app dans le même dossier

# Obtenir le chemin d'exécution
SCRIPT_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
APP_PATH="\${SCRIPT_DIR}/CISYNTH.app"

if [ ! -d "\${APP_PATH}" ]; then
    echo "ERREUR: CISYNTH.app non trouvé dans le même dossier que ce script."
    exit 1
fi

# Supprimer les attributs de quarantaine
xattr -cr "\${APP_PATH}"

# Créer le script de lancement
LAUNCH_SCRIPT="\${SCRIPT_DIR}/lancer_cisynth.sh"
cat > "\${LAUNCH_SCRIPT}" << EOF
#!/bin/bash
export DYLD_LIBRARY_PATH="\${APP_PATH}/Contents/Frameworks:\$DYLD_LIBRARY_PATH"
export DYLD_FORCE_FLAT_NAMESPACE=1
export DYLD_SHARED_REGION=avoid
"\${APP_PATH}/Contents/MacOS/CISYNTH"
EOF

chmod +x "\${LAUNCH_SCRIPT}"

echo "CISYNTH a été préparé pour l'exécution."
echo "Pour lancer l'application, utilisez: \${LAUNCH_SCRIPT}"
EOL

chmod +x "${DEPLOY_SCRIPT}"

echo ""
echo "=== INSTALLATION TERMINÉE ==="
echo ""
echo "Pour lancer CISYNTH, exécutez:"
echo "${WRAPPER_SCRIPT}"
echo ""
echo "Pour déployer CISYNTH sur un autre Mac, utilisez:"
echo "${DEPLOY_SCRIPT}"
echo ""

# Tentative d'exécution automatique
echo "Tentative d'exécution automatique..."
"${WRAPPER_SCRIPT}"
