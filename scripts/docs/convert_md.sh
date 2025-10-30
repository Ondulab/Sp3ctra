#!/bin/bash
# Script de conversion Markdown → HTML
# Usage: ./scripts/docs/convert_md.sh <fichier.md> [options]
#
# Exemples:
#   ./scripts/docs/convert_md.sh docs/IMAGE_SEQUENCER_SPECIFICATION.md
#   ./scripts/docs/convert_md.sh docs/ --recursive
#   ./scripts/docs/convert_md.sh docs/MIDI_SYSTEM_SPECIFICATION.md --template gdocs

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory (absolute path)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VENV_DIR="$PROJECT_ROOT/venv"
PYTHON_SCRIPT="$SCRIPT_DIR/md_to_html.py"
REQUIREMENTS="$PROJECT_ROOT/requirements.txt"

# Function to print colored messages
print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check if Python 3 is installed
if ! command_exists python3; then
    print_error "Python 3 n'est pas installé. Veuillez l'installer d'abord."
    exit 1
fi

# Check if input file/directory is provided
if [ $# -eq 0 ]; then
    print_error "Usage: $0 <fichier.md ou dossier> [options]"
    echo ""
    echo "Exemples:"
    echo "  $0 docs/IMAGE_SEQUENCER_SPECIFICATION.md"
    echo "  $0 docs/ --recursive"
    echo "  $0 docs/MIDI_SYSTEM_SPECIFICATION.md --template gdocs"
    echo ""
    echo "Options:"
    echo "  --template <standard|gdocs|both>  Type de template (défaut: both)"
    echo "  --recursive                       Traiter les sous-dossiers"
    echo "  --exclude <fichier1> <fichier2>   Exclure des fichiers"
    exit 1
fi

# Check if virtual environment exists
if [ ! -d "$VENV_DIR" ]; then
    print_info "Création de l'environnement virtuel Python..."
    python3 -m venv "$VENV_DIR"
    print_success "Environnement virtuel créé"
fi

# Activate virtual environment
print_info "Activation de l'environnement virtuel..."
source "$VENV_DIR/bin/activate"

# Check if mistune is installed
if ! python -c "import mistune" 2>/dev/null; then
    print_warning "Installation de la dépendance mistune..."
    pip install -q "mistune>=3.0.0"
    print_success "Dépendance installée"
fi

# Run the Python script with all arguments
print_info "Conversion en cours..."
echo ""

cd "$PROJECT_ROOT"
python "$PYTHON_SCRIPT" "$@"

EXIT_CODE=$?

echo ""

if [ $EXIT_CODE -eq 0 ]; then
    print_success "Conversion terminée avec succès !"
else
    print_error "La conversion a échoué (code: $EXIT_CODE)"
    exit $EXIT_CODE
fi
