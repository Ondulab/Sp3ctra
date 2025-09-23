#!/bin/bash
# Build a DEBUG version of Sp3ctra on macOS (LLDB-friendly)

set -euo pipefail
set -x

# Detect Homebrew prefix (defaults to /opt/homebrew on Apple Silicon)
HOMEBREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"

# Common include dir (avoid duplications)
INCLUDE_DIR="${HOMEBREW_PREFIX}/include"

# Optional: if you use a keg-only sfml@2 formula with separate include dir, uncomment:
# SFML2_INCLUDE_DIR="${HOMEBREW_PREFIX}/opt/sfml@2/include"

make clean

# Append (+=) to avoid overwriting flags set by your Makefile
make \
  BASE_CFLAGS+=" -g -O0 -Wall -Wextra -fPIC -DUSE_RTAUDIO -DPRINT_FPS -Wno-deprecated-declarations" \
  BASE_CXXFLAGS+=" -std=c++17 -g -O0 -Wall -Wextra -fPIC -DUSE_RTAUDIO -DPRINT_FPS -Wno-unused-but-set-variable -Wno-deprecated-declarations" \
  CFLAGS+=" -g -O0 -Wall -Wextra -fPIC -DUSE_RTAUDIO -DPRINT_FPS -Wno-deprecated-declarations -I${INCLUDE_DIR}" \
  CXXFLAGS+=" -std=c++17 -g -O0 -Wall -Wextra -fPIC -DUSE_RTAUDIO -DPRINT_FPS -Wno-deprecated-declarations -I${INCLUDE_DIR}"
  # If needed, also append: -I${SFML2_INCLUDE_DIR}

echo "Compilation de débogage terminée avec succès!"
echo "L'exécutable se trouve dans build/Sp3ctra"
echo "Vous pouvez maintenant le lancer avec le débogueur de VS Code."