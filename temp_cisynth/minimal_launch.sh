#!/bin/bash

# Variables d'environnement essentielles
export DYLD_LIBRARY_PATH="/Users/zhonx/Documents/Workspaces/Workspace_Xcode/CISYNTH/build/CISYNTH.app/Contents/Frameworks"
export DYLD_FORCE_FLAT_NAMESPACE=1
export DYLD_SHARED_REGION=avoid

# Lancement avec arguments modifiés pour désactiver les fonctionnalités problématiques
"/Users/zhonx/Documents/Workspaces/Workspace_Xcode/CISYNTH/temp_cisynth/CISYNTH_copy" --disable-gpu --disable-audio --disable-extensions

