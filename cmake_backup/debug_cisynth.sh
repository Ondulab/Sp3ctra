#!/bin/bash

# Script de débogage pour identifier pourquoi CISYNTH se plante
# Ce script capture les logs système et tente plusieurs approches de lancement

APP_PATH="$(pwd)/build/CISYNTH.app"
BINARY_PATH="${APP_PATH}/Contents/MacOS/CISYNTH"
FRAMEWORKS_PATH="${APP_PATH}/Contents/Frameworks"
LOG_FILE="$(pwd)/cisynth_debug.log"

# Fonction pour nettoyer les logs précédents
clean_logs() {
    echo "==================== NOUVELLE SESSION DE DÉBOGAGE ====================" > "$LOG_FILE"
    echo "Date: $(date)" >> "$LOG_FILE"
    echo "Système: $(sw_vers)" >> "$LOG_FILE"
    echo "=============================================================" >> "$LOG_FILE"
}

# Fonction pour capturer les logs système après un crash
capture_logs() {
    echo "Capture des logs système récents..." | tee -a "$LOG_FILE"
    echo "=============================================================" >> "$LOG_FILE"
    log show --predicate 'processImagePath contains "CISYNTH"' --last 5m >> "$LOG_FILE" 2>&1
    echo "=============================================================" >> "$LOG_FILE"
}

# Vérifier si l'exécutable existe
check_app() {
    if [ ! -f "$BINARY_PATH" ]; then
        echo "ERREUR: L'exécutable CISYNTH n'existe pas à: $BINARY_PATH"
        echo "Veuillez exécuter ./rebuild_cisynth.sh d'abord."
        exit 1
    fi
    
    echo "Application trouvée à: $BINARY_PATH"
    
    # Vérifier les frameworks
    echo "Vérification des frameworks nécessaires:"
    QTCORE="${FRAMEWORKS_PATH}/QtCore.framework/Versions/5/QtCore"
    QTWIDGETS="${FRAMEWORKS_PATH}/QtWidgets.framework/Versions/5/QtWidgets"
    
    if [ -f "$QTCORE" ]; then
        echo " - QtCore: OK"
    else
        echo " - QtCore: MANQUANT!"
    fi
    
    if [ -f "$QTWIDGETS" ]; then
        echo " - QtWidgets: OK"
    else
        echo " - QtWidgets: MANQUANT!"
    fi
}

# Tentative 1: Exécution avec moins de variables d'environnement
try_minimal_env() {
    echo "Tentative 1: Exécution avec configuration minimale..." | tee -a "$LOG_FILE"
    echo "=============================================================" >> "$LOG_FILE"
    
    export DYLD_LIBRARY_PATH="${FRAMEWORKS_PATH}:$DYLD_LIBRARY_PATH"
    export DYLD_FORCE_FLAT_NAMESPACE=1
    
    echo "Lancement avec DYLD_LIBRARY_PATH et DYLD_FORCE_FLAT_NAMESPACE uniquement..."
    "$BINARY_PATH" 2>> "$LOG_FILE" || echo "Échec de l'exécution (code $?)" | tee -a "$LOG_FILE"
    
    echo "=============================================================" >> "$LOG_FILE"
    capture_logs
}

# Tentative 2: Exécution sans modifications de mémoire
try_no_memory_vars() {
    echo "Tentative 2: Exécution sans variables de mémoire..." | tee -a "$LOG_FILE"
    echo "=============================================================" >> "$LOG_FILE"
    
    # Suppression des variables qui pourraient affecter la gestion mémoire
    unset DYLD_INSERT_LIBRARIES
    
    export DYLD_LIBRARY_PATH="${FRAMEWORKS_PATH}:$DYLD_LIBRARY_PATH"
    export DYLD_FORCE_FLAT_NAMESPACE=1
    export DYLD_PRINT_LIBRARIES=1
    
    echo "Lancement avec impression des bibliothèques chargées..."
    "$BINARY_PATH" 2>> "$LOG_FILE" || echo "Échec de l'exécution (code $?)" | tee -a "$LOG_FILE"
    
    echo "=============================================================" >> "$LOG_FILE"
    capture_logs
}

# Tentative 3: Exécution avec débogage complet
try_debug_mode() {
    echo "Tentative 3: Exécution avec débogage complet..." | tee -a "$LOG_FILE"
    echo "=============================================================" >> "$LOG_FILE"
    
    export DYLD_LIBRARY_PATH="${FRAMEWORKS_PATH}:$DYLD_LIBRARY_PATH"
    export DYLD_FORCE_FLAT_NAMESPACE=1
    export DYLD_PRINT_LIBRARIES=1
    export DYLD_PRINT_STATISTICS=1
    export DYLD_PRINT_INITIALIZERS=1
    export DYLD_PRINT_APIS=1
    export DYLD_PRINT_BINDINGS=1
    
    echo "Lancement avec toutes les options de débogage DYLD..."
    "$BINARY_PATH" 2>> "$LOG_FILE" || echo "Échec de l'exécution (code $?)" | tee -a "$LOG_FILE"
    
    echo "=============================================================" >> "$LOG_FILE"
    capture_logs
}

# Tentative 4: MallocStackLogging
try_malloc_logging() {
    echo "Tentative 4: Exécution avec MallocStackLogging..." | tee -a "$LOG_FILE"
    echo "=============================================================" >> "$LOG_FILE"
    
    export DYLD_LIBRARY_PATH="${FRAMEWORKS_PATH}:$DYLD_LIBRARY_PATH"
    export DYLD_FORCE_FLAT_NAMESPACE=1
    export MallocStackLogging=1
    
    echo "Lancement avec MallocStackLogging..."
    "$BINARY_PATH" 2>> "$LOG_FILE" || echo "Échec de l'exécution (code $?)" | tee -a "$LOG_FILE"
    
    echo "=============================================================" >> "$LOG_FILE"
    capture_logs
}

# Programme principal
main() {
    clean_logs
    check_app
    
    echo "Démarrage de la session de débogage de CISYNTH..."
    echo "Les résultats seront enregistrés dans $LOG_FILE"
    
    try_minimal_env
    try_no_memory_vars
    try_debug_mode
    try_malloc_logging
    
    echo ""
    echo "Toutes les tentatives ont été effectuées."
    echo "Veuillez consulter le fichier de log pour plus de détails: $LOG_FILE"
    echo ""
    echo "Conseil: Pour examiner les crashs système, ouvrez l'application Console sur macOS"
    echo "et recherchez 'CISYNTH' dans la barre de recherche."
}

# Exécution
main
