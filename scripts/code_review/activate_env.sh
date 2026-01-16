#!/bin/bash
# Activate the code review environment
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source "$SCRIPT_DIR/venv/bin/activate"
echo "✓ Code review environment activated"
echo ""
echo "Available commands:"
echo "  python3 run_code_review.py        # Run all agents"
echo "  python3 agent_llm_semantic.py     # Run LLM agent only"
echo "  deactivate                         # Exit virtual environment"
echo ""
