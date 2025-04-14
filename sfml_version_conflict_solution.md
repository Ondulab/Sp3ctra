# Résolution du crash CISYNTH causé par des conflits de versions SFML

## Problème identifié

D'après le rapport de crash fourni, l'application CISYNTH plante avec une exception `EXC_BAD_ACCESS (SIGSEGV)` lors de la création de la fenêtre SFML. L'analyse du crash montre que le problème se produit dans le constructeur de `MainWindow` au moment de l'appel à `sfRenderWindow_create`.

Extrait du stacktrace du crash :

```
Thread 0 Crashed::  Dispatch queue: com.apple.main-thread
0   libsfml-window.2.6.dylib      	       0x10806b4e8 0x108064000 + 29928
1   libsfml-window.2.6.dylib      	       0x10806ba0c 0x108064000 + 31244
2   libsfml-window.2.6.dylib      	       0x10806fb18 sf::Window::create(sf::VideoMode, sf::String const&, unsigned int, sf::ContextSettings const&) + 420
3   libcsfml-graphics.2.6.dylib   	       0x107214ad8 sfRenderWindow_create + 224
4   CISYNTH                       	       0x10475a3f0 MainWindow::MainWindow(QWidget*) + 644
5   CISYNTH                       	       0x10475a0e0 main + 124
```

En examinant la liste des bibliothèques chargées dans le rapport de crash, on constate que l'application charge à la fois **SFML 2.6** et **SFML 3.0** :

```
libsfml-graphics.3.0.dylib
libsfml-window.3.0.dylib
libsfml-system.3.0.dylib
...
libsfml-graphics.2.6.dylib
libsfml-window.2.6.dylib
libsfml-system.2.6.dylib
```

Ce conflit de versions cause une incompatibilité qui mène au crash lorsque l'application essaie d'utiliser les fonctions SFML.

## Solution mise en place

### 1. Modification du fichier CISYNTH.pro

Le fichier `CISYNTH.pro` a été modifié pour spécifier explicitement les versions des bibliothèques SFML à utiliser. Les liens vers les bibliothèques SFML génériques ont été remplacés par des liens vers les versions spécifiques 2.6 :

```diff
# Dépendances via Homebrew
LIBS += -L/opt/homebrew/lib \
    -lfftw3 \
    -lcairo \
    -lsndfile \
-   -lsfml-graphics -lsfml-window -lsfml-system \
+   -lsfml-graphics-2.6 -lsfml-window-2.6 -lsfml-system-2.6 \
    -lcsfml-graphics -lcsfml-window -lcsfml-system
```

### 2. Script de reconstruction et nettoyage

Un script `rebuild_sfml.sh` a été créé pour nettoyer complètement l'environnement de build et reconstruire l'application avec les bonnes versions de SFML. Ce script :

1. Nettoie entièrement le répertoire de build
2. Reconfigure et recompile l'application avec qmake
3. Supprime explicitement les bibliothèques SFML 3.0 du bundle si elles existent
4. Vérifie les dépendances des bibliothèques SFML
5. Déploie correctement toutes les dépendances avec macdeployqt
6. Applique une signature ad-hoc pour éviter les problèmes de Gatekeeper
7. Crée un script de lancement avec débogage activé pour diagnostiquer d'éventuels problèmes persistants

## Comment utiliser la solution

1. Exécutez le script `rebuild_sfml.sh` pour reconstruire l'application :
   ```bash
   ./rebuild_sfml.sh
   ```

2. Si l'application se compile correctement, elle sera automatiquement lancée.

3. En cas de problème persistant, utilisez le script de lancement avec débogage pour obtenir plus d'informations :
   ```bash
   ./build_qmake/launch_cisynth_with_debug.sh
   ```

## Vérification de la résolution

Pour vérifier que le problème est résolu :

1. Exécutez l'application reconstruite
2. Vérifiez qu'aucun crash ne se produit lors du démarrage
3. Vérifiez que la fenêtre SFML s'affiche correctement
4. Vérifiez que les fonctionnalités audio et visuelles fonctionnent comme prévu

## Explications techniques supplémentaires

Le crash était causé par une situation où des symboles de deux versions différentes de SFML étaient chargés simultanément. Lorsque l'application appelait une fonction, elle pouvait utiliser une implémentation d'une version alors que les structures de données étaient instanciées pour une autre version, causant une corruption mémoire.

La solution mise en place garantit que seule la version 2.6 de SFML est chargée, ce qui assure la cohérence des structures de données et des implémentations de fonctions utilisées.
