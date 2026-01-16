# Notes d'Architecture VST vs Standalone

## 1. RtAudio vs JUCE Audio

### ❌ Dans le VST : **PAS de RtAudio**
Le VST n'utilise **pas** RtAudio. C'est le DAW (Ableton, Logic, Reaper, etc.) qui gère l'audio.

**Pipeline Audio VST:**
```
DAW → juce::AudioProcessor::processBlock() → Votre code de synthèse → DAW → Carte son
```

- Le DAW appelle `processBlock()` périodiquement
- Vous remplissez le buffer fourni par le DAW
- Le DAW se charge de l'envoyer à la carte son

### ✅ Dans le Standalone : **RtAudio**
L'application standalone utilise RtAudio pour accéder directement à la carte son.

**Pipeline Audio Standalone:**
```
RtAudio → audio_rtaudio.cpp::rtCallback() → Votre code → RtAudio → Carte son
```

## 2. Différences Clés

| Aspect | VST (Plugin) | Standalone (Application) |
|--------|--------------|--------------------------|
| **Gestion Audio** | DAW (JUCE) | RtAudio |
| **Sample Rate** | Imposé par le DAW | Configurable (sp3ctra.ini) |
| **Buffer Size** | Imposé par le DAW | Configurable (sp3ctra.ini) |
| **MIDI** | Du DAW via MidiBuffer | Directement du hardware |
| **Latence** | Gérée par le DAW | Gérée par RtAudio |

## 3. Code Partagé

Le code de synthèse (vos algos) sera **identique** dans les deux cas:

```cpp
// Ce code fonctionne dans VST ET Standalone
float sample = std::sin(phase) * volume;
buffer[i] = sample;
```

## 4. Ce Qui Change

### Dans VST (`PluginProcessor.cpp`):
```cpp
void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    // Le DAW vous donne buffer et midi
    // Vous remplissez buffer avec vos samples
}
```

### Dans Standalone (`audio_rtaudio.cpp`):
```cpp
int rtCallback(void* outputBuffer, ...) {
    // RtAudio vous donne outputBuffer
    // Vous remplissez outputBuffer avec vos samples
}
```

## 5. Votre Code de Synthèse

Quand on intégrera vos synthés (LuxStral, LuxSynth, LuxWave), le code sera le même:

```cpp
// Même code dans VST et Standalone
synth_luxstral_process(buffer, numSamples);
synth_luxsynth_process(buffer, numSamples);
```

**La seule différence:** où ce code est appelé (processBlock vs rtCallback).

## 6. Avantages du VST

- ✅ Pas de gestion de l'audio (le DAW s'en charge)
- ✅ MIDI intégré au DAW (automation, enregistrement)
- ✅ Paramètres exposés au DAW
- ✅ Sauvegarde d'état dans les projets
- ✅ Plusieurs instances possibles

## Résumé

🎯 **VST = Pas RtAudio, c'est JUCE qui fait l'interface avec le DAW**

Le code de synthèse reste identique, seule la couche d'interface audio change.
