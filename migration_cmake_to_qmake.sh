#!/bin/bash

# Script complet pour migrer CISYNTH de CMake vers qmake
# Ce script sauvegarde la configuration CMake actuelle et met en place qmake

echo "=== MIGRATION DE CISYNTH DE CMAKE VERS QMAKE ==="
echo ""

# Déterminer le répertoire du projet
PROJECT_DIR="$(pwd)"
BACKUP_DIR="${PROJECT_DIR}/cmake_backup"

# Étape 1: Sauvegarder les fichiers CMake s'ils n'ont pas encore été sauvegardés
if [ ! -d "${BACKUP_DIR}" ]; then
    echo "1. Sauvegarde des fichiers CMake..."
    mkdir -p "${BACKUP_DIR}/resources"
    
    # Déplacer les fichiers CMake vers le répertoire de sauvegarde
    if [ -f "CMakeLists.txt" ]; then
        mv "CMakeLists.txt" "${BACKUP_DIR}/"
    fi
    
    if [ -f "resources/fix_app.sh.in" ]; then
        mv "resources/fix_app.sh.in" "${BACKUP_DIR}/resources/"
    fi
    
    if [ -f "rebuild_cisynth.sh" ]; then
        mv "rebuild_cisynth.sh" "${BACKUP_DIR}/"
    fi
    
    if [ -f "debug_cisynth.sh" ]; then
        mv "debug_cisynth.sh" "${BACKUP_DIR}/"
    fi
    
    if [ -f "emergency_run.sh" ]; then
        mv "emergency_run.sh" "${BACKUP_DIR}/"
    fi
    
    # Supprimer le répertoire build s'il existe
    if [ -d "build" ]; then
        rm -rf "build"
    fi
    
    echo "   Fichiers CMake sauvegardés dans ${BACKUP_DIR}"
else
    echo "1. Les fichiers CMake ont déjà été sauvegardés dans ${BACKUP_DIR}"
fi

# Étape 2: Vérifier que les fichiers qmake sont en place
echo "2. Vérification des fichiers qmake..."

if [ ! -f "CISYNTH.pro" ]; then
    echo "ERREUR: Le fichier CISYNTH.pro n'existe pas."
    echo "Veuillez d'abord créer ce fichier en suivant les instructions du fichier cisynth_qmake_solution.md"
    exit 1
fi

if [ ! -f "build_with_qmake.sh" ]; then
    echo "ERREUR: Le script build_with_qmake.sh n'existe pas."
    echo "Veuillez d'abord créer ce script en suivant les instructions du fichier cisynth_qmake_solution.md"
    exit 1
fi

if [ ! -x "build_with_qmake.sh" ]; then
    chmod +x "build_with_qmake.sh"
    echo "   Permissions d'exécution ajoutées à build_with_qmake.sh"
fi

# Étape 3: Compiler et déployer avec qmake
echo "3. Lancement de la compilation avec qmake..."
./build_with_qmake.sh

# Étape 4: Vérification des résultats
echo ""
echo "=== MIGRATION TERMINÉE ==="
echo ""
echo "Si la compilation a réussi, vous pouvez maintenant utiliser CISYNTH avec qmake."
echo "En cas d'échec, vous pouvez restaurer la configuration CMake avec la commande:"
echo ""
echo "mv ${BACKUP_DIR}/* ."
echo ""
echo "Consultez le fichier cisynth_qmake_solution.md pour plus d'informations sur les avantages"
echo "de l'approche qmake par rapport à CMake pour CISYNTH."
