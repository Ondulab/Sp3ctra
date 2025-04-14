#!/bin/bash

# Script d'urgence pour tenter de lancer CISYNTH avec des restrictions de sécurité minimales
# Ce script essaie différentes approches radicales pour contourner les problèmes de sécurité macOS

APP_PATH="$(pwd)/build/CISYNTH.app"
BINARY_PATH="${APP_PATH}/Contents/MacOS/CISYNTH"
FRAMEWORKS_PATH="${APP_PATH}/Contents/Frameworks"

echo "========================================================================"
echo "SCRIPT D'EXÉCUTION D'URGENCE POUR CISYNTH"
echo "========================================================================"
echo "Ce script tente des approches radicales pour contourner les restrictions"
echo "de sécurité de macOS qui pourraient empêcher l'application de s'exécuter."
echo "========================================================================"

# Vérifier si l'application existe
if [ ! -f "$BINARY_PATH" ]; then
    echo "ERREUR: L'application CISYNTH n'a pas été trouvée."
    echo "Veuillez exécuter ./rebuild_cisynth.sh d'abord."
    exit 1
fi

# Créer une copie de l'exécutable pour travailler dessus
TEMP_DIR="$(pwd)/temp_cisynth"
TEMP_BINARY="${TEMP_DIR}/CISYNTH_copy"

mkdir -p "$TEMP_DIR"
cp "$BINARY_PATH" "$TEMP_BINARY"
chmod +x "$TEMP_BINARY"

echo "Copie de l'exécutable créée dans $TEMP_BINARY"

# Tenter de désactiver la restriction AMFI (Apple Mobile File Integrity)
# Cette approche est plus agressive et désactive certaines protections de sécurité
echo "Tentative de lancement avec SIP temporairement ignoré..."

# Configuration des variables d'environnement
export DYLD_LIBRARY_PATH="${FRAMEWORKS_PATH}"
export DYLD_FORCE_FLAT_NAMESPACE=1
export DYLD_SHARED_REGION=avoid

# Tentative avec déblocage complet des frameworks Qt
echo "
=========================================================================
MÉTHODE 1: LANCEMENT AVEC CONFIG MINIMALISTE
=========================================================================
"

echo "Lancement de l'application avec configuration minimaliste..."
"$TEMP_BINARY" || echo "Échec avec code $?"

echo "
=========================================================================
MÉTHODE 2: LANCEMENT AVEC LIMITES DE MÉMOIRE
=========================================================================
"

# Limiter la mémoire pour éviter OOM killer
echo "Tentative avec limite de mémoire (4 Go max)..."
ulimit -v 4194304  # Limite à 4Go de mémoire virtuelle
"$TEMP_BINARY" || echo "Échec avec code $?"

echo "
=========================================================================
MÉTHODE 3: LANCEMENT AVEC DÉSACTIVATION DES LIBRAIRIES PROBLÉMATIQUES
=========================================================================
"

# Créer un script temporaire qui désactive certaines librairies potentiellement problématiques
cat > "${TEMP_DIR}/minimal_launch.sh" << EOL
#!/bin/bash

# Variables d'environnement essentielles
export DYLD_LIBRARY_PATH="${FRAMEWORKS_PATH}"
export DYLD_FORCE_FLAT_NAMESPACE=1
export DYLD_SHARED_REGION=avoid

# Lancement avec arguments modifiés pour désactiver les fonctionnalités problématiques
"$TEMP_BINARY" --disable-gpu --disable-audio --disable-extensions

EOL

chmod +x "${TEMP_DIR}/minimal_launch.sh"
echo "Tentative avec fonctionnalités restreintes..."
"${TEMP_DIR}/minimal_launch.sh" || echo "Échec avec code $?"

echo "
=========================================================================
MÉTHODE 4: LANCEMENT AVEC LIENS SYMBOLIQUES
=========================================================================
"

# Créer des liens symboliques pour les frameworks Qt directement dans le dossier de l'exécutable
LINK_DIR="${TEMP_DIR}/frameworks"
mkdir -p "$LINK_DIR"

echo "Création de liens symboliques pour les frameworks Qt..."
ln -sf "${FRAMEWORKS_PATH}/QtCore.framework/Versions/5/QtCore" "${LINK_DIR}/QtCore"
ln -sf "${FRAMEWORKS_PATH}/QtWidgets.framework/Versions/5/QtWidgets" "${LINK_DIR}/QtWidgets"
ln -sf "${FRAMEWORKS_PATH}/QtGui.framework/Versions/5/QtGui" "${LINK_DIR}/QtGui"
ln -sf "${FRAMEWORKS_PATH}/QtNetwork.framework/Versions/5/QtNetwork" "${LINK_DIR}/QtNetwork"

cat > "${TEMP_DIR}/linked_launch.sh" << EOL
#!/bin/bash

# Définir le chemin vers les liens symboliques
export DYLD_LIBRARY_PATH="${LINK_DIR}:${FRAMEWORKS_PATH}"
export DYLD_FORCE_FLAT_NAMESPACE=1
export DYLD_SHARED_REGION=avoid

# Lancement avec les liens symboliques
"$TEMP_BINARY"

EOL

chmod +x "${TEMP_DIR}/linked_launch.sh"
echo "Tentative avec liens symboliques directs..."
"${TEMP_DIR}/linked_launch.sh" || echo "Échec avec code $?"

echo "
=========================================================================
MÉTHODE 5: DISTRIBUTION ALTERNATIVE
=========================================================================
"

echo "Si aucune de ces méthodes ne fonctionne, considérez ces alternatives:"
echo ""
echo "1. Distribuer votre application sous forme de script + binaire + frameworks"
echo "   au lieu d'un bundle .app standard"
echo ""
echo "2. Utiliser le package manager homebrew pour la distribution"
echo ""
echo "3. Créer une version sans interface Qt, utilisant uniquement SFML/CSFML"
echo ""
echo "4. Distribuer via une image disque (.dmg) avec instructions spécifiques"
echo ""
echo "5. Obtenir un certificat de développeur Apple pour la signature de code"
echo "   (option payante mais la plus fiable)"
echo ""

# Nettoyage
echo "Nettoyage des fichiers temporaires..."
# Laisser les fichiers pour debug éventuel
# rm -rf "$TEMP_DIR"

echo "
=========================================================================
TOUTES LES MÉTHODES ONT ÉTÉ TENTÉES
=========================================================================
"
