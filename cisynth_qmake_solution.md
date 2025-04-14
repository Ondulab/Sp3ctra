# Solution pour CISYNTH avec qmake

Ce document explique comment convertir CISYNTH de CMake à qmake pour résoudre les problèmes de signature sur macOS.

## Pourquoi qmake résout les problèmes de signature

| Aspect | CMake (Problématique) | qmake (Solution) |
|--------|----------------------|------------------|
| **Gestion de bundle** | Gestion manuelle complexe | Automatique et standard pour macOS |
| **Signature de code** | Tentative de modification forcée des signatures | Signature minimale et ad-hoc automatique |
| **Déploiement** | Utilisation de scripts complexes (`fix_app.sh`) | Intégré via `macdeployqt` |
| **Variables d'environnement** | Nombreuses variables DYLD requises | Pratiquement aucune variable nécessaire |
| **Compatibilité Gatekeeper** | Problèmes fréquents | Meilleure compatibilité par défaut |

## Fichiers de la solution

1. **CISYNTH.pro** - Remplace CMakeLists.txt et configure le projet pour qmake
2. **build_with_qmake.sh** - Script pour compiler et déployer avec qmake

## Comment fonctionne cette solution

### Structure de bundle correcte

qmake crée automatiquement une structure de bundle macOS standard avec :
- Hiérarchie correcte des frameworks
- Chemins d'installation relatifs (@rpath/@executable_path)
- Info.plist correctement configuré

### Gestion des dépendances

Avec qmake :
- Les frameworks Qt sont déployés avec leurs signatures originales intactes
- Les bibliothèques tierces sont liées de manière appropriée
- Utilisez macdeployqt pour copier et configurer toutes les dépendances

### Avantages principaux de qmake pour CISYNTH

1. **Simplicité** : Configuration beaucoup plus simple que CMake
2. **Compatibilité native** : Meilleure intégration avec Qt et macOS
3. **Moins de contournements** : Pas besoin de variables d'environnement DYLD_*
4. **Distribution facilitée** : L'application peut être distribuée par simple copie

## Comparaison des scripts de lancement

### Ancien script (CMake)

```bash
#!/bin/bash
export DYLD_LIBRARY_PATH="$APP_PATH/Contents/Frameworks:$DYLD_LIBRARY_PATH"
export DYLD_FORCE_FLAT_NAMESPACE=1
export DYLD_SHARED_REGION=avoid
export DYLD_PRINT_LIBRARIES=1
export DYLD_PRINT_LIBRARIES_POST_LAUNCH=1
export DYLD_PRINT_RPATHS=1
# ... et d'autres variables
"$APP_PATH/Contents/MacOS/CISYNTH"
```

### Nouveau script (qmake)

```bash
#!/bin/bash
open "$APP_PATH"
```

## Comment migrer

1. Remplacez votre système de build CMake par qmake en utilisant les fichiers fournis
2. Exécutez `chmod +x build_with_qmake.sh && ./build_with_qmake.sh`
3. Testez l'application et vérifiez qu'elle fonctionne sans problèmes de signature

## Pourquoi cette solution fonctionne

qmake, étant l'outil natif de Qt, comprend mieux comment configurer les applications Qt pour macOS. Il gère automatiquement de nombreux aspects que CMake nécessite de configurer manuellement :

1. Il établit correctement les relations entre les frameworks
2. Il respecte les attentes de sécurité de macOS concernant les signatures
3. Il crée une structure de bundle standard que macOS sait interpréter

Ceci explique pourquoi SpectroGen fonctionne sans problème, car il utilise qmake plutôt que CMake.
