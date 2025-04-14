#!/bin/bash

# Définir le chemin vers les liens symboliques
export DYLD_LIBRARY_PATH="/Users/zhonx/Documents/Workspaces/Workspace_Xcode/CISYNTH/temp_cisynth/frameworks:/Users/zhonx/Documents/Workspaces/Workspace_Xcode/CISYNTH/build/CISYNTH.app/Contents/Frameworks"
export DYLD_FORCE_FLAT_NAMESPACE=1
export DYLD_SHARED_REGION=avoid

# Lancement avec les liens symboliques
"/Users/zhonx/Documents/Workspaces/Workspace_Xcode/CISYNTH/temp_cisynth/CISYNTH_copy"

