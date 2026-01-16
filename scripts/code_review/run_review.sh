#!/bin/bash
# Wrapper script to run code review with the correct Python environment

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
VENV_PYTHON="$SCRIPT_DIR/venv/bin/python3"

# Check if venv exists
if [ ! -f "$VENV_PYTHON" ]; then
    echo "❌ Virtual environment not found!"
    echo "Run: bash scripts/code_review/setup_environment.sh"
    exit 1
fi

echo "🔍 Running Code Review with virtual environment Python..."
echo ""

# Run with venv Python
cd "$SCRIPT_DIR" && "$VENV_PYTHON" run_code_review.py "$@"
