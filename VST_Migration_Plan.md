# Plan de Migration VST pour Sp3ctra

## 1. Choix Technologiques

Pour convertir Sp3ctra en VST tout en conservant la réception UDP et en facilitant le "vibe coding" (développement itératif rapide assisté par IA), voici la recommandation :

### Framework : **JUCE (C++)**
C'est le standard industriel pour le développement de plugins audio.
- **Pourquoi ?**
  - **Compatibilité :** Exporte en VST3, AU, AAX, et Standalone à partir d'un seul code.
  - **Audio :** Gestion robuste des callbacks audio et du MIDI.
  - **Réseau :** Classes `juce::DatagramSocket` ou intégration facile de sockets POSIX existants.
  - **Vibe Coding :** Très populaire, donc les LLM (comme moi) connaissent très bien l'API, ce qui accélère le développement.
  - **GUI :** Moteur graphique puissant pour visualiser vos buffers d'images/audio.

### Système de Build : **CMake**
- **Pourquoi ?**
  - Plus moderne et flexible que le "Projucer" de JUCE.
  - S'intègre parfaitement avec VS Code et les outils d'IA.
  - Permet de compiler facilement le code C existant comme une bibliothèque statique liée au plugin.

## 2. Architecture du VST

L'architecture actuelle (Application Standalone) doit être adaptée pour fonctionner dans un hôte (DAW comme Ableton, Logic, Reaper).

### A. Structure Globale
Le `main.c` disparaît. Le point d'entrée devient le `PluginProcessor` de JUCE.

```mermaid
graph TD
    DAW[DAW / Hôte VST] -->|Audio & MIDI| Processor[Sp3ctraAudioProcessor]
    Processor -->|Gère| Engine[Sp3ctraEngine (C++ Wrapper)]
    Engine -->|Encapsule| CoreC[Code C Existant (Synth, Buffers)]
    
    UDP[Thread UDP] -->|Reçoit Images| DoubleBuffer[DoubleBuffer]
    DoubleBuffer -->|Lit| CoreC
    
    Processor -->|Paramètres| Editor[PluginEditor (GUI)]
```

### B. Gestion du Flux UDP (Point Critique)
Vous avez mentionné que la réception UDP est critique.
- **Problème :** Dans un VST, on peut avoir plusieurs instances du plugin. Si toutes essaient d'écouter le port 55151, il y aura conflit.
- **Solution :** Votre code utilise déjà `SO_REUSEPORT`. Cela permet à plusieurs instances d'écouter le même port (sur macOS/Linux).
- **Implémentation :**
  - Le `PluginProcessor` lancera un `juce::Thread` (ou `std::thread`) au démarrage qui exécutera votre boucle `udpThread` existante.
  - Ce thread écrira dans le `DoubleBuffer` encapsulé dans l'instance du plugin.

### C. Encapsulation (Refactoring nécessaire)
Le code actuel utilise beaucoup de variables globales (`buffers_L`, `buffers_R`, `g_sp3ctra_config`). C'est problématique pour un VST car toutes les instances partageraient ces globales.

**Action : Créer une classe `Sp3ctraCore`**
Cette classe contiendra tout l'état qui était global :
- `DoubleBuffer`
- `AudioImageBuffers`
- `Context`
- Les états des synthés (LuxStral, LuxSynth, LuxWave)

Les fonctions C devront être modifiées pour prendre un pointeur vers ce contexte (ex: `synth_process(Sp3ctraContext* ctx, ...)` au lieu d'utiliser des globales).

### D. Pipeline Audio
Remplacement de `AudioSystem::rtCallback` (RtAudio) par `AudioProcessor::processBlock` (JUCE).

1. **processBlock (JUCE) :**
   - Reçoit le buffer audio du DAW.
   - Récupère les événements MIDI du DAW (plus besoin de `midi_Connect` direct, on utilise le MIDI du DAW).
   - Appelle votre fonction de synthèse existante `synth_AudioProcess` en lui passant les buffers de sortie du DAW.

### E. Paramètres (Automation)
Les variables contrôlées par MIDI (mix levels, reverb send) deviendront des `juce::AudioParameterFloat`.
- Cela permet l'automation dans le DAW.
- Cela permet de créer une interface graphique native VST.

## 3. Plan d'Action

1. **Préparation du Projet :**
   - Créer un projet JUCE avec CMake.
   - Configurer les sources C existantes pour être compilées dans ce projet.

2. **Refactoring "Global-to-Local" :**
   - Regrouper les globales critiques dans une structure/classe `Sp3ctraState`.
   - Adapter `udpThread` et `audioProcessingThread` pour utiliser cette structure.

3. **Intégration UDP :**
   - Migrer `udpThread` dans le `PluginProcessor`.
   - Vérifier la réception des paquets dans le contexte VST.

4. **Intégration Audio :**
   - Connecter `processBlock` aux algorithmes de synthèse.
   - Remplacer l'entrée MIDI directe par le buffer MIDI du VST.

5. **Interface (Optionnel pour débuter) :**
   - Afficher une visualisation simple pour confirmer la réception des images.
