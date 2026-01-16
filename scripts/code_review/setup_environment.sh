#!/bin/bash
# Setup script for Code Review Agents
# Installs all optional dependencies in a Python virtual environment

set -e  # Exit on error

echo "🔧 Setting up Code Review Agents Environment"
echo "=============================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"
VENV_DIR="$SCRIPT_DIR/venv"

echo -e "${BLUE}📁 Project root: $PROJECT_ROOT${NC}"
echo -e "${BLUE}📁 Virtual env: $VENV_DIR${NC}"
echo ""

# Step 1: Check Python
echo -e "${BLUE}[1/5]${NC} Checking Python installation..."
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}❌ Python 3 not found!${NC}"
    echo "Install with: brew install python3"
    exit 1
fi
PYTHON_VERSION=$(python3 --version)
echo -e "${GREEN}✓${NC} $PYTHON_VERSION found"
echo ""

# Step 2: Create virtual environment
echo -e "${BLUE}[2/5]${NC} Creating Python virtual environment..."
if [ -d "$VENV_DIR" ]; then
    echo -e "${YELLOW}⚠️  Virtual environment already exists${NC}"
    read -p "Delete and recreate? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf "$VENV_DIR"
        python3 -m venv "$VENV_DIR"
        echo -e "${GREEN}✓${NC} Virtual environment recreated"
    else
        echo -e "${GREEN}✓${NC} Using existing virtual environment"
    fi
else
    python3 -m venv "$VENV_DIR"
    echo -e "${GREEN}✓${NC} Virtual environment created at: $VENV_DIR"
fi
echo ""

# Step 3: Activate and upgrade pip
echo -e "${BLUE}[3/5]${NC} Activating virtual environment..."
source "$VENV_DIR/bin/activate"
echo -e "${GREEN}✓${NC} Virtual environment activated"

echo "Upgrading pip..."
pip install --upgrade pip > /dev/null 2>&1
echo -e "${GREEN}✓${NC} pip upgraded"
echo ""

# Step 4: Install Python packages
echo -e "${BLUE}[4/5]${NC} Installing Python packages..."
echo "  • Installing lizard (code metrics)..."
pip install lizard > /dev/null 2>&1 && echo -e "${GREEN}    ✓${NC} lizard installed" || echo -e "${YELLOW}    ⚠${NC} lizard installation failed"

echo "  • Installing anthropic (Claude LLM)..."
pip install anthropic > /dev/null 2>&1 && echo -e "${GREEN}    ✓${NC} anthropic installed" || echo -e "${YELLOW}    ⚠${NC} anthropic installation failed"
echo ""

# Step 5: Check optional system tools
echo -e "${BLUE}[5/5]${NC} Checking optional system tools..."

if command -v clang-tidy &> /dev/null; then
    echo -e "${GREEN}✓${NC} clang-tidy found ($(clang-tidy --version | head -n1))"
else
    echo -e "${YELLOW}⚠${NC} clang-tidy not found (optional)"
    echo "  Install with: brew install llvm"
    echo "  Then add to PATH: export PATH=\"/opt/homebrew/opt/llvm/bin:\$PATH\""
fi

if command -v cppcheck &> /dev/null; then
    echo -e "${GREEN}✓${NC} cppcheck found ($(cppcheck --version))"
else
    echo -e "${YELLOW}⚠${NC} cppcheck not found (optional)"
    echo "  Install with: brew install cppcheck"
fi
echo ""

# Step 6: Create activation script
echo "Creating activation script..."
cat > "$SCRIPT_DIR/activate_env.sh" << 'EOF'
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
EOF
chmod +x "$SCRIPT_DIR/activate_env.sh"
echo -e "${GREEN}✓${NC} Activation script created: activate_env.sh"
echo ""

# Summary
echo "=============================================="
echo -e "${GREEN}✅ Setup complete!${NC}"
echo "=============================================="
echo ""
echo "📋 What was installed:"
echo "  • Python virtual environment: $VENV_DIR"
echo "  • lizard (code complexity metrics)"
echo "  • anthropic (Claude AI SDK)"
echo ""
echo "🚀 To use the agents:"
echo ""
echo "  1. Activate the environment:"
echo -e "     ${BLUE}source scripts/code_review/activate_env.sh${NC}"
echo ""
echo "  2. Run the code review:"
echo -e "     ${BLUE}python3 scripts/code_review/run_code_review.py${NC}"
echo ""
echo "  3. When done:"
echo -e "     ${BLUE}deactivate${NC}"
echo ""
echo "💡 For LLM analysis (Agent 7), also configure:"
echo -e "   ${BLUE}export ANTHROPIC_API_KEY='sk-ant-api03-your-key'${NC}"
echo "   (Get your key at: https://console.anthropic.com/)"
echo ""
echo "📖 Documentation:"
echo "   • README.md        - Full documentation"
echo "   • SETUP_LLM.md     - LLM setup guide"
echo ""
