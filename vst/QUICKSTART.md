# Guide de Test Minimal VST - Sp3ctra

Ce guide vous permet de compiler et tester un VST minimal de Sp3ctra en quelques étapes.

## Étape 1 : Résoudre les Dépendances (Stubs Temporaires)

Le code C actuel utilise des variables globales qui empêchent la compilation. Créons des stubs temporaires.

### Créer `vst/source/global_stubs.c`

```c
// Stubs temporaires pour permettre la compilation du VST
// Ces variables seront remplacées par une architecture instanciée

#include "../../src/core/context.h"
#include "../../src/config/config_loader.h"

// Configuration globale (temporaire)
sp3ctra_config_t g_sp3ctra_config = {
    .sampling_frequency = 48000,
    .audio_buffer_size = 512,
    .log_level = 2, // INFO
    .udp_address = "239.100.100.100",
    .udp_port = 55151,
    .multicast_interface = ""
};

// Stub pour la taille des pixels CIS
int get_cis_pixels_nb(void) {
    return 2048; // Valeur par défaut
}

// Stub pour logger (version simplifiée)
void logger_init(int level) { (void)level; }
void log_info(const char* module, const char* format, ...) { (void)module; (void)format; }
void log_warning(const char* module, const char* format, ...) { (void)module; (void)format; }
void log_error(const char* module, const char* format, ...) { (void)module; (void)format; }
void log_debug(const char* module, const char* format, ...) { (void)module; (void)format; }
```

### Ajouter à `vst/CMakeLists.txt`

```cmake
# Après target_sources(Sp3ctraVST PRIVATE ${CORE_SOURCES})
target_sources(Sp3ctraVST PRIVATE
    source/global_stubs.c
)
```

## Étape 2 : Test Audio Minimal (Sans Code C)

Modifions `PluginProcessor.cpp` pour générer un signal de test avant d'intégrer le code existant.

### `vst/source/PluginProcessor.cpp` (processBlock)

```cpp
void Sp3ctraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, 
                                          juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    // Clear all channels
    buffer.clear();
    
    // Generate 440Hz test tone at 10% volume
    static float phase = 0.0f;
    const float frequency = 440.0f;
    const float sampleRate = (float)getSampleRate();
    const float phaseIncrement = 2.0f * juce::MathConstants<float>::pi * frequency / sampleRate;
    const float volume = 0.1f;
    
    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            channelData[sample] = std::sin(phase) * volume;
            phase += phaseIncrement;
            if (phase >= 2.0f * juce::MathConstants<float>::pi)
                phase -= 2.0f * juce::MathConstants<float>::pi;
        }
    }
    
    juce::ignoreUnused(midiMessages);
}
```

## Étape 3 : Compilation

```bash
cd vst
mkdir -p build
cd build

# Configuration CMake
cmake ..

# Compilation
cmake --build . --config Release

# Les plugins seront dans :
# - macOS: build/Sp3ctraVST_artefacts/Release/VST3/Sp3ctraVST.vst3
# - macOS: build/Sp3ctraVST_artefacts/Release/AU/Sp3ctraVST.component
```

### Résolution de Problèmes Courants

**Erreur : "JUCE not found"**
- CMake télécharge JUCE automatiquement, vérifiez votre connexion internet

**Erreur : "C compiler not found"**
```bash
xcode-select --install
```

**Erreur : "Multiple definitions of..."**
- Vérifiez que `main.c` est bien exclu du build (voir CMakeLists.txt)

## Étape 4 : Test dans un DAW

### macOS - Logic Pro / GarageBand
```bash
# Copier le plugin AU
sudo cp -r build/Sp3ctraVST_artefacts/Release/AU/Sp3ctraVST.component \
   /Library/Audio/Plug-Ins/Components/

# Relancer Logic Pro et chercher "Sp3ctra" dans les instruments
```

### macOS - Reaper / Ableton
```bash
# Copier le plugin VST3
cp -r build/Sp3ctraVST_artefacts/Release/VST3/Sp3ctraVST.vst3 \
   ~/Library/Audio/Plug-Ins/VST3/

# Rescanner les plugins dans le DAW
```

### Test de Base
1. Créer une nouvelle piste instrument
2. Charger "Sp3ctraVST"
3. Vous devriez entendre un La 440Hz à 10% de volume
4. L'interface devrait afficher "Sp3ctra VST - Spectral Audio Synthesis"

## Étape 5 : Intégration Progressive du Code C

Une fois le VST minimal fonctionnel, intégrez progressivement :

### 5.1 Ajouter les Buffers Audio
```cpp
// Dans PluginProcessor.h
private:
    AudioImageBuffers audioImageBuffers;
    DoubleBuffer doubleBuffer;
```

### 5.2 Ajouter le Thread UDP
```cpp
// Dans PluginProcessor.h
private:
    std::unique_ptr<juce::Thread> udpThread;
    int udpSocket = -1;
```

### 5.3 Intégrer les Synthés
```cpp
// Dans processBlock()
// Appeler synth_AudioProcess() avec les buffers JUCE
```

## Étape 6 : Debugging

### Logs dans le VST
```cpp
// Dans PluginProcessor.cpp
DBG("Sp3ctra: Sample rate = " + juce::String(sampleRate));
```

Les logs apparaissent dans la console du DAW (visible via Console.app sur macOS).

### Version Standalone pour Debug
Le build CMake crée aussi une version Standalone :
```bash
./build/Sp3ctraVST_artefacts/Release/Standalone/Sp3ctraVST.app/Contents/MacOS/Sp3ctraVST
```

Cette version est plus facile à débugger (peut être lancée directement dans Xcode/GDB).

## Prochaines Étapes

Après validation du VST minimal :
1. ✅ Créer une classe `Sp3ctraCore` pour encapsuler les globales
2. ✅ Migrer le thread UDP dans le PluginProcessor
3. ✅ Connecter les synthés au `processBlock()`
4. ✅ Ajouter les paramètres VST (mix levels, reverb, etc.)
5. ✅ Créer une interface graphique (visualisation des buffers)

## Ressources

- Documentation JUCE : https://docs.juce.com/master/
- Tutoriels VST JUCE : https://github.com/TheAudioProgrammer
- Forum JUCE : https://forum.juce.com/
