#!/bin/bash

echo "🔄 Restauration de la configuration 48kHz"
echo "=========================================="

# Vérifier si la sauvegarde existe
if [ -f "src/core/config.h.original" ]; then
    mv src/core/config.h.original src/core/config.h
    echo "✅ Configuration 48kHz restaurée"
    
    echo "🔧 Recompilation avec config 48kHz..."
    make clean -C build_nogui
    make -C build_nogui
    
    if [ $? -eq 0 ]; then
        echo "✅ Compilation 48kHz réussie !"
        echo ""
        echo "Paramètres restaurés :"
        echo "- Fréquence : 48000 Hz"
        echo "- Buffer : 150 frames"
        echo "- Latence : 3.125ms"
    else
        echo "❌ Erreur de compilation 48kHz"
    fi
    
elif [ -f "src/core/config.h.bak" ]; then
    cp src/core/config.h.bac src/core/config.h
    echo "✅ Configuration restaurée depuis sauvegarde"
else
    echo "❌ Aucune sauvegarde trouvée"
    echo "Configuration actuelle conservée"
fi

# Nettoyer les fichiers temporaires
rm -f src/core/config.h.bak
rm -f src/core/config_temp.h

echo "🧹 Nettoyage terminé"
