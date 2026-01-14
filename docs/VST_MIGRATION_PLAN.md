# Plan de migration Sp3ctra vers VST Plugin

**Branche**: `feature/vst-plugin`  
**Date**: 15/01/2026  
**Objectif**: Créer un plugin VST3/AU permettant d'injecter les données spectrales brutes du capteur CIS directement dans une DAW

---

## 🎯 Vision et contraintes

### Ce que nous voulons
- **Flux spectral brut** : 3456 valeurs × 1kHz via UDP/Ethernet
- **Injection directe** : Pas de transformation, réduction ou analyse
- **Latence faible** : < 15ms end-to-end pour usage instrumental
- **Indépendance** : Ne pas dépendre de MIDI/OSC/automation de la DAW
- **Préservation** : Garder le moteur spectral existant intact

### Ce que nous ne voulons pas
- ❌ Transformer en contrôles MIDI CC
- ❌ Extraire des features/descripteurs
- ❌ Décimation destructive
- ❌ Mapping via automation DAW
- ❌ Réécrire la synthèse

---

## 📊 Architecture technique

### Flux de données
```
┌──────────────────┐
│ Sp3ctra Firmware │ Capteur CIS (3456 pixels)
│ Ethernet TX      │ @ 1kHz → 3.456 MB/s
└────────┬─────────┘
         │ UDP packets
         ↓
┌─────────────────────────────────────────────────────────────┐
│                    DAW (Ableton, Logic, Reaper...)          │
│  ┌───────────────────────────────────────────────────────┐  │
│  │             Sp3ctra VST3/AU Plugin                    │  │
│  │                                                       │  │
│  │  ┌──────────────────┐       ┌────────────────────┐   │  │
│  │  │ UDP Receiver     │       │ Audio Callback     │   │  │
│  │  │ (Thread 1)       │       │ (Thread 2 - RT)    │   │  │
│  │  │                  │       │                    │   │  │
│  │  │ • Socket bind    │       │ • Read buffer      │   │  │
│  │  │ • Recv 3456 B    │       │ • Synth spectral   │   │  │
│  │  │ • Parse/validate │──────→│ • Output audio     │   │  │
│  │  │ • Write lock-free│       │                    │   │  │
│  │  └──────────────────┘       └────────────────────┘   │  │
│  │           │                                           │  │
│  │           └─→ Lock-free ring buffer (SPSC)           │  │
│  │               Capacity: 3456 × 16 frames              │  │
│  │                                                       │  │
│  └───────────────────────────────────────────────────────┘  │
│                             ↓                               │
│                      Audio Output (stereo)                  │
└─────────────────────────────────────────────────────────────┘
```

### Estimations de latence
```
Total latency budget: ~5-15ms
├── Ethernet (local): ~100-500 µs
├── Socket buffer: ~1 ms
├── Lock-free transfer: ~10 µs
├── DAW audio buffer: 2.6-10.6 ms (128-512 samples @ 48kHz)
└── Synthesis processing: ~1-2 ms
```

---

## 🏗️ Structure du projet

```
sp3ctra-vst/
├── README.md                      # Documentation plugin
├── CMakeLists.txt                 # Build iPlug2
├── config.h                       # Configuration VST
│
├── src/
│   ├── Sp3ctraPlugin.h           # Entry point VST
│   ├── Sp3ctraPlugin.cpp         # processBlock(), initialize()
│   │
│   ├── UdpReceiver.h             # UDP receiver thread
│   ├── UdpReceiver.cpp           # Adapté de src/communication/network/udp.c
│   │
│   ├── SpectralBuffer.h          # Lock-free SPSC ring buffer
│   ├── SpectralBuffer.cpp        # Adapté de src/audio/buffers/doublebuffer.h
│   │
│   └── engine/                   # Code synthèse RÉUTILISÉ depuis Sp3ctra
│       ├── synth_luxstral/       # Synthèse additive
│       │   ├── synth_luxstral.h
│       │   ├── synth_luxstral.c
│       │   ├── wave_generation.c
│       │   └── ...
│       │
│       ├── synth_luxsynth/       # Synthèse polyphonique FFT
│       │   ├── synth_luxsynth.h
│       │   ├── synth_luxsynth.c
│       │   └── kissfft/
│       │
│       ├── synth_luxwave/        # Synthèse photowave
│       │   ├── synth_luxwave.h
│       │   └── synth_luxwave.c
│       │
│       └── common/
│           ├── voice_manager.h
│           └── voice_manager.c
│
├── web-ui/                       # Interface graphique (React + WebView)
│   ├── package.json
│   ├── src/
│   │   ├── App.jsx               # Application principale
│   │   ├── components/
│   │   │   ├── SpectralView.jsx  # Visualisation spectre temps réel
│   │   │   ├── ConnectionStatus.jsx  # État UDP
│   │   │   ├── SynthSelector.jsx     # Choix moteur synthèse
│   │   │   └── Parameters.jsx        # Contrôles synthèse
│   │   └── styles/
│   │       └── main.css
│   └── public/
│       └── index.html
│
└── docs/
    ├── VST_MIGRATION_PLAN.md     # Ce document
    ├── API.md                    # Documentation API plugin
    └── PROTOCOL.md               # Format protocole UDP
```

---

## 📋 Plan d'implémentation par phases

### Phase 1: Setup initial (1-2 jours)
**Objectif**: Créer la structure du projet iPlug2

- [ ] **1.1** Installer iPlug2 SDK
  - Clone repo: `git clone https://github.com/iPlug2/iPlug2.git`
  - Setup dépendances (macOS: Xcode, VST3 SDK)
  
- [ ] **1.2** Créer projet Sp3ctraVST
  - Utiliser template `IPlugEffect`
  - Configuration CMakeLists.txt
  - Build de test (plugin vide)

- [ ] **1.3** Documenter l'architecture
  - Créer `docs/API.md`
  - Créer `docs/PROTOCOL.md`

**Validation**: Plugin vide compile et se charge dans DAW

---

### Phase 2: UDP Receiver (2-3 jours)
**Objectif**: Recevoir les données CIS via UDP

- [ ] **2.1** Créer `UdpReceiver` class
  ```cpp
  class UdpReceiver {
  public:
      UdpReceiver(int port, SpectralBuffer& buffer);
      ~UdpReceiver();
      void start();
      void stop();
      
  private:
      void receiverThread();
      std::thread thread_;
      std::atomic<bool> running_;
      int socket_;
      SpectralBuffer& buffer_;
  };
  ```

- [ ] **2.2** Implémenter réception UDP
  - Socket POSIX (`socket()`, `bind()`, `recvfrom()`)
  - Thread démarrage/arrêt propre
  - Validation format paquet (3456 bytes)
  - Gestion erreurs réseau

- [ ] **2.3** Tests unitaires
  - Test avec émulateur Python (fake CIS data)
  - Vérifier réception 1kHz stable
  - Mesurer CPU usage thread

**Validation**: Réception stable de 1000 paquets/sec

---

### Phase 3: Lock-free buffer (1-2 jours)
**Objectif**: Buffer RT-safe entre UDP thread et audio callback

- [ ] **3.1** Créer `SpectralBuffer` class
  ```cpp
  class SpectralBuffer {
  public:
      static constexpr size_t LINE_SIZE = 3456;
      static constexpr size_t CAPACITY = 16;  // 16ms @ 1kHz
      
      bool write(const uint8_t* line);  // Non-blocking, called by UDP thread
      bool read(uint8_t* line);         // Non-blocking, called by audio callback
      
  private:
      std::array<std::array<uint8_t, LINE_SIZE>, CAPACITY> buffer_;
      std::atomic<size_t> writePos_{0};
      std::atomic<size_t> readPos_{0};
  };
  ```

- [ ] **3.2** Implémenter SPSC ring buffer
  - Single Producer (UDP thread) / Single Consumer (audio callback)
  - Lockless avec `std::atomic`
  - Gestion overrun (drop oldest)

- [ ] **3.3** Tests de stress
  - Test concurrence (reader + writer threads)
  - Benchmark latency read/write
  - Validation RT-safety (no allocations, no locks)

**Validation**: Buffer lockless, latency < 50µs

---

### Phase 4: Moteur de synthèse (3-5 jours)
**Objectif**: Porter le code de synthèse existant

- [ ] **4.1** Copier code source
  - `src/synthesis/luxstral/` → `engine/synth_luxstral/`
  - `src/synthesis/luxsynth/` → `engine/synth_luxsynth/`
  - `src/synthesis/luxwave/` → `engine/synth_luxwave/`
  - `src/synthesis/common/` → `engine/common/`

- [ ] **4.2** Adapter dépendances
  - Supprimer dépendances RtAudio
  - Supprimer dépendances SFML/display
  - Garder uniquement le DSP pur
  - Adapter includes et paths

- [ ] **4.3** Créer wrapper C++
  ```cpp
  class SpectralSynthEngine {
  public:
      void setMode(SynthMode mode);  // LuxStral, LuxSynth, LuxWave
      void processSpectralLine(const uint8_t* line, size_t lineSize);
      void processAudio(float** outputs, int nFrames);
      
  private:
      SynthMode currentMode_;
      // Pointeurs vers moteurs C
      void* luxstralState_;
      void* luxsynthState_;
      void* luxwaveState_;
  };
  ```

- [ ] **4.4** Tests isolation
  - Test chaque moteur séparément
  - Validation output audio (waveform inspection)
  - Benchmark CPU usage

**Validation**: Audio synthétisé correctement depuis données test

---

### Phase 5: Intégration VST (2-3 jours)
**Objectif**: Connecter tous les composants dans le plugin

- [ ] **5.1** Implémenter `processBlock()`
  ```cpp
  void Sp3ctraPlugin::ProcessBlock(float** inputs, float** outputs, int nFrames) {
      // Read latest spectral line (lock-free)
      uint8_t spectralLine[3456];
      if (spectralBuffer_.read(spectralLine)) {
          synthEngine_.processSpectralLine(spectralLine, 3456);
      }
      
      // Generate audio
      synthEngine_.processAudio(outputs, nFrames);
  }
  ```

- [ ] **5.2** Lifecycle plugin
  - `OnActivate()`: Start UDP receiver
  - `OnDeactivate()`: Stop UDP receiver
  - `OnReset()`: Clear buffers, reset synth state

- [ ] **5.3** Paramètres VST
  - UDP port (éditable)
  - Synth mode selector (LuxStral/LuxSynth/LuxWave)
  - Master volume
  - Mix levels (si plusieurs moteurs)

- [ ] **5.4** Tests intégration
  - Load dans Ableton Live / Logic Pro
  - Test avec émulateur CIS
  - Vérifier latency monitoring
  - Test automation parameters

**Validation**: Plugin fonctionnel end-to-end dans DAW

---

### Phase 6: Interface utilisateur (3-4 jours)
**Objectif**: UI React dans WebView pour monitoring et contrôle

- [ ] **6.1** Setup React app
  - Init projet React dans `web-ui/`
  - Configuration Webpack pour WebView
  - Communication plugin ↔ UI via message passing

- [ ] **6.2** Composants UI
  - `ConnectionStatus`: État UDP (connected, packet rate, errors)
  - `SpectralView`: Visualisation des 3456 valeurs en temps réel
  - `SynthSelector`: Choix du moteur de synthèse
  - `Parameters`: Contrôles des paramètres synth

- [ ] **6.3** Styling
  - Design moderne et minimal
  - Dark theme (standard audio plugins)
  - Responsive (différentes tailles fenêtre)

- [ ] **6.4** Tests UI
  - Test avec Claude/Copilot pour modifications
  - Validation IA-friendly (facile à modifier)

**Validation**: UI fonctionnelle et modifiable facilement par IA

---

### Phase 7: Tests et optimisation (2-3 jours)
**Objectif**: Validation performance et stabilité

- [ ] **7.1** Tests fonctionnels
  - Test avec Sp3ctra réel (firmware hardware)
  - Validation latency < 15ms
  - Test stability (sessions longues 1h+)

- [ ] **7.2** Profiling performance
  - CPU usage audio thread
  - Latency UDP → audio output
  - Memory allocations (zero en RT path)

- [ ] **7.3** Tests compatibilité DAW
  - Ableton Live
  - Logic Pro (AU format)
  - Reaper
  - Bitwig Studio

- [ ] **7.4** Documentation utilisateur
  - Guide d'installation
  - Configuration réseau
  - Troubleshooting

**Validation**: Plugin stable, performant, compatible

---

### Phase 8: Polish et release (1-2 jours)
**Objectif**: Préparation pour release

- [ ] **8.1** Code cleanup
  - Remove debug logs
  - Code review
  - Documentation inline

- [ ] **8.2** Build release
  - macOS: VST3 + AU + Standalone
  - Signature code (Developer ID)
  - Packaging DMG

- [ ] **8.3** Documentation finale
  - README complet
  - CHANGELOG
  - LICENSE

**Validation**: Release candidate prêt

---

## 🔧 Détails techniques critiques

### Protocole UDP
```
Format paquet:
┌────────────────────────────────────────────────────┐
│ Header (8 bytes)                                   │
├────────────────────────────────────────────────────┤
│ Magic: 0x53503352 ("SP3R")          │ 4 bytes     │
│ Frame ID: uint32_t                  │ 4 bytes     │
├────────────────────────────────────────────────────┤
│ Spectral data (3456 bytes)                         │
├────────────────────────────────────────────────────┤
│ Data[0..3455]: uint8_t              │ 3456 bytes  │
├────────────────────────────────────────────────────┤
│ Checksum: CRC32                     │ 4 bytes     │
└────────────────────────────────────────────────────┘
Total: 3468 bytes/packet
```

### Configuration réseau
- **Port par défaut**: 9000 (configurable dans UI)
- **Protocole**: UDP unicast
- **Interface**: localhost (127.0.0.1) ou LAN
- **MTU**: Standard Ethernet (1500 bytes) → plusieurs fragments si besoin

### Synchronisation audio
- **Strategy**: Latest-value (toujours lire la ligne la plus récente)
- **Fallback**: Si aucune donnée reçue, maintenir dernière ligne
- **Overrun**: Si buffer plein, drop oldest (UDP peut perdre paquets)

---

## 📚 Ressources et références

### Frameworks
- **iPlug2**: https://github.com/iPlug2/iPlug2
- **VST3 SDK**: https://github.com/steinbergmedia/vst3sdk

### Documentation
- iPlug2 Examples: https://github.com/iPlug2/iPlug2/tree/master/Examples
- VST3 API: https://steinbergmedia.github.io/vst3_doc/

### Code existant à réutiliser
- `src/communication/network/udp.c` → Base UDP receiver
- `src/audio/buffers/doublebuffer.h` → Base lock-free buffer
- `src/synthesis/*` → Moteurs de synthèse complets

---

## ⏱️ Timeline estimé

| Phase | Durée | Dépendances |
|-------|-------|-------------|
| 1. Setup initial | 1-2 jours | - |
| 2. UDP Receiver | 2-3 jours | Phase 1 |
| 3. Lock-free buffer | 1-2 jours | Phase 1 |
| 4. Moteur synthèse | 3-5 jours | Phase 1 |
| 5. Intégration VST | 2-3 jours | Phase 2, 3, 4 |
| 6. Interface UI | 3-4 jours | Phase 5 |
| 7. Tests | 2-3 jours | Phase 6 |
| 8. Polish | 1-2 jours | Phase 7 |
| **Total** | **15-24 jours** | |

Avec travail régulier: **3-5 semaines**

---

## 🎯 Critères de succès

### Fonctionnels
- ✅ Réception stable 1000 paquets/sec via UDP
- ✅ Audio synthétisé directement depuis données CIS
- ✅ Latency < 15ms (mesurable)
- ✅ Zero dropout audio pendant utilisation normale
- ✅ UI responsive et modifiable par IA

### Techniques
- ✅ Zero allocations dans audio callback
- ✅ CPU usage < 30% (single core @ 3GHz)
- ✅ Compatible VST3 + AU formats
- ✅ Fonctionne dans 3+ DAWs majeures

### Qualité
- ✅ Code documenté (English)
- ✅ Tests unitaires clés composants
- ✅ Guide utilisateur complet
- ✅ Pas de warnings compilation

---

## 📌 Prochaines étapes immédiates

1. **Installer iPlug2 SDK** (aujourd'hui)
2. **Créer structure projet** (demain)
3. **Prototype UDP receiver** (2 jours)

Voulez-vous que je commence par l'installation d'iPlug2 et la création de la structure de base ?
