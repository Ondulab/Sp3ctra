#!/bin/bash

echo "🧪 Test du système de debug des oscillateurs"
echo "=============================================="

# Vérifier que l'exécutable existe
if [ ! -f "build/Sp3ctra" ]; then
    echo "❌ L'exécutable build/Sp3ctra n'existe pas. Compilez d'abord avec 'make'."
    exit 1
fi

echo "✅ Exécutable trouvé"

# Vérifier que le dossier debug_images existe
if [ ! -d "debug_images" ]; then
    echo "❌ Le dossier debug_images n'existe pas"
    exit 1
fi

echo "✅ Dossier debug_images trouvé"

# Compter les fichiers PNG existants avant le test
PNG_COUNT_BEFORE=$(find debug_images -name "*.png" | wc -l)
echo "📊 Fichiers PNG avant test: $PNG_COUNT_BEFORE"

echo ""
echo "🚀 Lancement de l'application pour 10 secondes..."
echo "   Cherchez les messages suivants dans la sortie:"
echo "   - '🔧 IMAGE_DEBUG: Initialized, output directory: ./debug_images/'"
echo "   - '🔧 OSCILLATOR_SCAN: Initialized buffer'"
echo "   - '🔧 OSCILLATOR_SCAN: Auto-saving after 48000 samples'"
echo ""

# Lancer l'application en arrière-plan et la tuer après 10 secondes
timeout 10s ./build/Sp3ctra 2>&1 | grep -E "(IMAGE_DEBUG|OSCILLATOR_SCAN|ERROR|🔧)" || echo "Aucun message de debug détecté"

echo ""
echo "⏹️  Application arrêtée"

# Compter les fichiers PNG après le test
PNG_COUNT_AFTER=$(find debug_images -name "*.png" | wc -l)
echo "📊 Fichiers PNG après test: $PNG_COUNT_AFTER"

if [ $PNG_COUNT_AFTER -gt $PNG_COUNT_BEFORE ]; then
    echo "✅ Nouveaux fichiers PNG créés! Le debug fonctionne."
    echo "📁 Fichiers récents dans debug_images:"
    ls -lt debug_images/*.png 2>/dev/null | head -5
else
    echo "⚠️  Aucun nouveau fichier PNG créé."
    echo "   Cela peut être normal si l'application n'a pas reçu de données d'image."
fi

echo ""
echo "🔍 Pour vérifier manuellement:"
echo "   1. Lancez: ./build/Sp3ctra"
echo "   2. Cherchez les messages commençant par '🔧 IMAGE_DEBUG:' et '🔧 OSCILLATOR_SCAN:'"
echo "   3. Vérifiez les fichiers PNG dans debug_images/"
echo "   4. Les fichiers oscillator_volumes.png contiennent les traces des oscillateurs"
