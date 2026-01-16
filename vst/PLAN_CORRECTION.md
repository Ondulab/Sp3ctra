# Plan de Correction VST Sp3ctra
**Date:** 2026-01-16  
**Basé sur:** semantic_analysis_vst.md + CONSOLIDATED_REPORT.txt

---

## 📊 Vue d'Ensemble

**Total des issues identifiées:** 44
- 🔴 **Critiques:** 1 (ERROR #2 - Multi-instance unsafe)
- ⚠️ **Importantes:** 9 warnings fonctionnels
- ℹ️ **Améliorations:** 34 suggestions d'amélioration

**État actuel:**
- ✅ Test tone 440Hz fonctionnel (fix static variable appliqué)
- ⚠️ Pas d'intégration audio réelle (processBlock stub)
- ⚠️ Configuration globale partagée entre instances
- ✅ APVTS correctement implémenté
- ✅ Thread safety UI respectée

---

## 🎯 Priorités de Correction

### 🔴 PHASE 1: CRITIQUES (À corriger IMMÉDIATEMENT)
**Durée estimée:** 1-2 jours

#### 1.1 Multi-Instance Safety (ERROR #2)
**Problème:** `g_sp3ctra_config` est une variable globale partagée entre toutes les instances du VST.

**Impact:** 
- Race conditions si plusieurs instances sont chargées
- Dernière instance écrase la config des autres
- Non-conforme aux standards VST

**Fichiers concernés:**
- `vst/source/PluginProcessor.cpp` (ligne 358)
- `vst/source/Sp3ctraCore.cpp` (ligne 195)

**Solution proposée (Option A - Recommandée):**
```cpp
// Dans Sp3ctraCore.h
class Sp3ctraCore {
private:
    sp3ctra_config_t instanceConfig;  // Config par instance
    
    // Helper pour passer config aux fonctions C
    void applyConfigToC();
};

// Dans Sp3ctraCore.cpp
void Sp3ctraCore::applyConfigToC() {
    // Copier instanceConfig dans les structures C juste avant utilisation
    // Ou refactorer les fonctions C pour accepter la config en paramètre
}
```

**Solution alternative (Option B - Court terme):**
```cpp
// Documenter clairement la limitation
// Dans README.md et PluginProcessor.h:

/**
 * ⚠️ LIMITATION ACTUELLE: Une seule instance du plugin peut être 
 * utilisée à la fois dans le DAW en raison de la configuration globale.
 * TODO: Refactorer pour supporter multi-instance.
 */
```

**Actions:**
- [ ] Choisir l'approche (A ou B)
- [ ] Si Option A: Refactorer Sp3ctraCore avec config instance
- [ ] Si Option A: Modifier fonctions C pour accepter config en param
- [ ] Si Option B: Documenter limitation dans README + code
- [ ] Tester avec 2-3 instances dans Reaper/Ableton
- [ ] Vérifier qu'il n'y a pas d'interférences

---

### ⚠️ PHASE 2: FONCTIONNELLES (Cette semaine)
**Durée estimée:** 3-5 jours

#### 2.1 Implémenter le Traitement Audio Réel (WARNING #1, #7)
**Problème:** `processBlock()` génère uniquement un test tone, pas d'audio depuis les données UDP/synthèse.

**Fichiers concernés:**
- `vst/source/PluginProcessor.cpp` (ligne 239-266)
- `vst/source/Sp3ctraCore.cpp` (méthode manquante)

**Solution:**
```cpp
// 1. Dans Sp3ctraCore.h
class Sp3ctraCore {
public:
    // Nouvelle méthode RT-safe
    void fillAudioBuffer(juce::AudioBuffer<float>& buffer);
    void prepareAudioProcessing(double sampleRate, int samplesPerBlock);
    
private:
    // Buffers pré-alloués
    std::vector<float> tempBuffer;
    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;
};

// 2. Dans Sp3ctraCore.cpp
void Sp3ctraCore::prepareAudioProcessing(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;
    maxBlockSize = samplesPerBlock;
    
    // Pré-allouer tous les buffers nécessaires
    tempBuffer.resize(samplesPerBlock * 2); // Stereo
    
    // Initialiser les moteurs de synthèse avec nouveaux params
    if (context && context->audioImageBuffers) {
        // Configure synthesis engines...
    }
}

void Sp3ctraCore::fillAudioBuffer(juce::AudioBuffer<float>& buffer) {
    // RT-SAFE: Aucune allocation, aucun lock (sauf lock-free), aucune I/O
    
    if (!initialized.load() || !context || !audioImageBuffers) {
        buffer.clear();
        return;
    }
    
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    
    // Lire depuis audioImageBuffers (lock-free read)
    // Appeler les moteurs de synthèse (luxstral, luxsynth, luxwave)
    // Mixer les sorties
    // Appliquer panoramique lock-free
    
    for (int channel = 0; channel < numChannels; ++channel) {
        float* channelData = buffer.getWritePointer(channel);
        
        // TODO: Intégrer votre pipeline de synthèse ici
        // Exemple: synth_luxstral_process(context, channelData, numSamples);
        
        // Pour l'instant: silence
        std::fill_n(channelData, numSamples, 0.0f);
    }
}

// 3. Dans PluginProcessor.cpp
void Sp3ctraAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    if (sp3ctraCore) {
        sp3ctraCore->prepareAudioProcessing(sampleRate, samplesPerBlock);
    }
    
    testTonePhase = 0.0f;
}

void Sp3ctraAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, 
                                         juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    
    // Mode de fonctionnement: choisir entre test tone et synthèse réelle
    bool useTestTone = false; // À rendre configurable via paramètre
    
    if (useTestTone) {
        // Garder le test tone pour debugging
        generateTestTone(buffer);
    } else {
        // Production: utiliser la synthèse réelle
        if (sp3ctraCore && sp3ctraCore->isInitialized()) {
            sp3ctraCore->fillAudioBuffer(buffer);
        } else {
            buffer.clear();
        }
    }
}
```

**Actions:**
- [ ] Ajouter méthode `prepareAudioProcessing()` dans Sp3ctraCore
- [ ] Ajouter méthode `fillAudioBuffer()` RT-safe dans Sp3ctraCore
- [ ] Implémenter `prepareToPlay()` dans PluginProcessor
- [ ] Modifier `processBlock()` pour appeler fillAudioBuffer
- [ ] Créer paramètre "Test Tone Enable" pour basculer entre modes
- [ ] Tester avec profiler (Instruments sur macOS)
- [ ] Vérifier: pas d'allocations dans fillAudioBuffer
- [ ] Vérifier: pas de underruns audio
- [ ] Mesurer temps d'exécution: < 50% du buffer duration

#### 2.2 Améliorer la Validation des Entrées (WARNING #8, #9)
**Problème:** Validation IP address trop faible, adresses custom non supportées.

**Fichiers concernés:**
- `vst/source/SettingsWindow.cpp` (ligne 78-98)

**Solution:**
```cpp
// Dans SettingsWindow.cpp

// Helper de validation IP
static bool isValidIPv4Address(const juce::String& ip) {
    juce::StringArray parts;
    parts.addTokens(ip, ".", "");
    
    if (parts.size() != 4)
        return false;
    
    for (const auto& part : parts) {
        if (part.isEmpty())
            return false;
            
        // Vérifier que c'est un nombre
        for (int i = 0; i < part.length(); ++i) {
            if (!juce::CharacterFunctions::isDigit(part[i]))
                return false;
        }
        
        int val = part.getIntValue();
        if (val < 0 || val > 255)
            return false;
    }
    
    return true;
}

// Dans le callback de udpAddressEditor
void SettingsComponent::udpAddressTextChanged() {
    juce::String address = udpAddressEditor.getText().trim();
    
    if (isValidIPv4Address(address)) {
        // Adresse valide
        udpAddressEditor.setColour(juce::TextEditor::outlineColourId, 
                                    juce::Colours::green);
        
        // Appliquer la config
        applyUdpSettings();
    } else {
        // Adresse invalide
        udpAddressEditor.setColour(juce::TextEditor::outlineColourId, 
                                    juce::Colours::red);
        
        statusLabel.setText("⚠️ Invalid IP address format", 
                           juce::dontSendNotification);
    }
}
```

**Actions:**
- [ ] Ajouter fonction `isValidIPv4Address()`
- [ ] Améliorer feedback visuel (couleur verte/rouge)
- [ ] Considérer: ajouter preset "Custom" dans dropdown
- [ ] Tester avec adresses valides/invalides
- [ ] Documenter format accepté dans l'UI

#### 2.3 Nettoyer le Code AI-Generated (WARNING #2-#7 du rapport AI Bias)
**Problème:** Paramètres non utilisés, TODOs, magic numbers.

**Fichiers concernés:**
- `vst/source/PluginProcessor.cpp`
- `vst/source/SettingsWindow.cpp`
- `vst/source/PluginEditor.cpp`

**Actions:**
- [ ] Remplacer magic numbers par constantes nommées
  ```cpp
  // Dans Sp3ctraConstants.h
  constexpr int MIN_UDP_PORT = 1024;
  constexpr int MAX_UDP_PORT = 65535;
  constexpr int SENSOR_DPI_200 = 200;
  constexpr int SENSOR_DPI_400 = 400;
  constexpr int UI_HEADER_HEIGHT = 60;
  constexpr int UI_PADDING = 20;
  ```
- [ ] Nettoyer `juce::ignoreUnused()` ou implémenter les fonctions
- [ ] Traiter ou supprimer les TODOs
- [ ] Documenter pourquoi certains params ne sont pas utilisés

---

### 🎨 PHASE 3: UI/UX (Ce mois)
**Durée estimée:** 2-3 jours

#### 3.1 Refactoring Layout UI vers FlexBox (WARNING #3, #10)
**Problème:** Layout hardcodé avec `setBounds()`, difficile à maintenir.

**Fichiers concernés:**
- `vst/source/PluginEditor.cpp` (ligne 51-66)
- `vst/source/SettingsWindow.cpp` (ligne 195-225)

**Solution:**
```cpp
// Dans PluginEditor.cpp
void Sp3ctraAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds();
    
    // Header
    auto headerArea = bounds.removeFromTop(Sp3ctraConstants::UI_HEADER_HEIGHT);
    
    // Content avec FlexBox
    juce::FlexBox contentFlex;
    contentFlex.flexDirection = juce::FlexBox::Direction::column;
    contentFlex.justifyContent = juce::FlexBox::JustifyContent::flexStart;
    contentFlex.alignItems = juce::FlexBox::AlignItems::stretch;
    
    contentFlex.items.add(juce::FlexItem(settingsButton)
        .withHeight(40)
        .withMargin(juce::FlexItem::Margin(10, 80, 10, 80)));
    
    contentFlex.items.add(juce::FlexItem(statusLabel)
        .withHeight(30)
        .withMargin(5));
    
    contentFlex.items.add(juce::FlexItem(infoLabel)
        .withHeight(60)
        .withMargin(5));
    
    auto contentArea = bounds.reduced(Sp3ctraConstants::UI_PADDING, 10);
    contentFlex.performLayout(contentArea);
}
```

**Actions:**
- [ ] Migrer PluginEditor vers FlexBox
- [ ] Migrer SettingsWindow vers FlexBox
- [ ] Tester redimensionnement fenêtre
- [ ] Vérifier sur différentes tailles d'écran

#### 3.2 Créer un LookAndFeel Custom (INFO #4)
**Problème:** Couleurs hardcodées dans paint(), pas de thème cohérent.

**Solution:**
```cpp
// Créer vst/source/Sp3ctraLookAndFeel.h
#pragma once
#include <JuceHeader.h>

class Sp3ctraLookAndFeel : public juce::LookAndFeel_V4 {
public:
    Sp3ctraLookAndFeel();
    
    // Palette de couleurs
    static const juce::Colour BACKGROUND_DARK;
    static const juce::Colour BACKGROUND_MEDIUM;
    static const juce::Colour BACKGROUND_LIGHT;
    static const juce::Colour ACCENT_PRIMARY;
    static const juce::Colour ACCENT_SECONDARY;
    static const juce::Colour TEXT_PRIMARY;
    static const juce::Colour TEXT_SECONDARY;
    
    // Overrides JUCE
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                             const juce::Colour& backgroundColour,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override;
};

// Dans Sp3ctraLookAndFeel.cpp
const juce::Colour Sp3ctraLookAndFeel::BACKGROUND_DARK = juce::Colour(0xff2a2a2a);
const juce::Colour Sp3ctraLookAndFeel::BACKGROUND_MEDIUM = juce::Colour(0xff404040);
const juce::Colour Sp3ctraLookAndFeel::ACCENT_PRIMARY = juce::Colour(0xff00a8ff);
// ...

// Dans PluginEditor.cpp
Sp3ctraAudioProcessorEditor::Sp3ctraAudioProcessorEditor(...)
    : AudioProcessorEditor(&p), audioProcessor(p) {
    
    // Appliquer le custom look and feel
    setLookAndFeel(&customLookAndFeel);
    
    // ...
}
```

**Actions:**
- [ ] Créer `Sp3ctraLookAndFeel.h/cpp`
- [ ] Définir palette de couleurs cohérente
- [ ] Appliquer dans PluginEditor
- [ ] Appliquer dans SettingsWindow
- [ ] Documenter la palette dans un guide de style

#### 3.3 Améliorer UI Feedback (WARNING #4, INFO #6)
**Problème:** Fenêtre settings reste en mémoire, bouton Apply désactivé confus.

**Actions:**
- [ ] Option 1: Détruire `settingsWindow` à la fermeture
  ```cpp
  void closeButtonPressed() override {
      setVisible(false);
      // Optionnel: se supprimer pour libérer mémoire
      // delete this; // Attention: si unique_ptr, laisser le faire
  }
  ```
- [ ] Option 2: Remplacer bouton Apply par "Test Connection"
  ```cpp
  testConnectionButton.onClick = [this]() {
      // Tester la connexion UDP
      bool success = testUdpConnection();
      if (success) {
          statusLabel.setText("✅ UDP Connection OK", ...);
      } else {
          statusLabel.setText("❌ UDP Connection Failed", ...);
      }
  };
  ```

---

### 🔧 PHASE 4: QUALITÉ & MAINTENANCE (Ce mois)
**Durée estimée:** 2-3 jours

#### 4.1 Centraliser la Gestion de Configuration
**Problème:** Duplication du handling de `udp_address` dans 4 fichiers.

**Solution:**
```cpp
// Créer vst/source/ConfigManager.h
class ConfigManager {
public:
    static constexpr const char* DEFAULT_UDP_ADDRESS = "239.100.100.100";
    static constexpr int DEFAULT_UDP_PORT = 60000;
    
    struct UdpConfig {
        std::string address;
        int port;
        
        bool isValid() const {
            return isValidIPv4(address) && 
                   port >= MIN_UDP_PORT && 
                   port <= MAX_UDP_PORT;
        }
    };
    
    // Helpers
    static bool isValidIPv4(const std::string& ip);
    static UdpConfig fromAPVTS(AudioProcessorValueTreeState& apvts);
};
```

**Actions:**
- [ ] Créer `ConfigManager` centralisé
- [ ] Refactorer PluginProcessor pour utiliser ConfigManager
- [ ] Refactorer SettingsWindow pour utiliser ConfigManager
- [ ] Refactorer Sp3ctraCore pour utiliser ConfigManager
- [ ] Éliminer duplication de code

#### 4.2 Ajouter Tests Unitaires
**Actions:**
- [ ] Créer `vst/tests/` directory
- [ ] Tests pour ConfigManager
- [ ] Tests pour validation IP
- [ ] Tests pour APVTS sérialisation
- [ ] Intégrer dans CI/CD si disponible

#### 4.3 Améliorer Documentation
**Actions:**
- [ ] Documenter limitation multi-instance (si non corrigée)
- [ ] Créer guide d'intégration des synthés
- [ ] Documenter contraintes RT-audio
- [ ] Ajouter schéma d'architecture dans NOTES_ARCHITECTURE.md
- [ ] Créer TROUBLESHOOTING.md pour erreurs courantes

#### 4.4 Nettoyage Code Mort (INFO #1-#4 Duplication Report)
**Problème:** Stubs jamais appelés dans le VST.

**Fichiers concernés:**
- `vst/source/global_stubs.c`

**Actions:**
- [ ] Vérifier quels stubs sont réellement nécessaires
- [ ] Supprimer stubs inutilisés:
  - `image_preprocess_frame` (si non utilisé)
  - `image_sequencer_process_frame` (si non utilisé)
  - `synth_luxwave_set_image_line` (si non utilisé)
  - `synth_AudioProcess` (si non utilisé)
- [ ] Documenter les stubs restants avec commentaires

---

## 🧪 Tests de Régression

### Test Suite Minimale
À exécuter après chaque phase:

#### T1: Test Tone Quality
```bash
# Build
./scripts/build_vst.sh clean install

# Test dans DAW
# 1. Charger 1 instance → vérifier son propre
# 2. Charger 3 instances → vérifier pas d'interférence
# 3. Automatiser volume → vérifier pas de clics
```

#### T2: Multi-Instance Safety
```bash
# Dans DAW:
# 1. Charger instance A, configurer UDP port 60000
# 2. Charger instance B, configurer UDP port 60001
# 3. Vérifier que chaque instance utilise son propre port
# 4. Envoyer données UDP sur chaque port
# 5. Vérifier que chaque instance reçoit uniquement ses données
```

#### T3: Parameter Automation
```bash
# Dans DAW:
# 1. Créer automation du paramètre UDP Port
# 2. Faire varier entre 60000 et 60010
# 3. Vérifier que le port change sans crash
# 4. Vérifier logs pour erreurs
```

#### T4: Save/Restore
```bash
# Dans DAW:
# 1. Configurer tous les paramètres (UDP, DPI, Log Level)
# 2. Sauvegarder le projet
# 3. Fermer le DAW
# 4. Rouvrir le projet
# 5. Vérifier que tous les paramètres sont restaurés
```

#### T5: RT-Audio Performance
```bash
# Avec Instruments (macOS) ou perf (Linux):
# 1. Profiler processBlock() sous charge
# 2. Vérifier: temps d'exécution < 50% buffer duration
# 3. Vérifier: zéro allocations dans processBlock()
# 4. Vérifier: zéro underruns
```

---

## 📊 Métriques de Succès

### Critères d'Acceptation

| Critère | Objectif | Mesure |
|---------|----------|--------|
| Multi-instance | Pas d'interférence | 3+ instances avec configs différentes |
| RT-Safety | Zéro allocations en RT | Profiler + ASan |
| Performance | < 50% buffer time | Instruments/perf |
| Underruns | Zéro en nominal | DAW buffer 512@48kHz |
| Qualité audio | SNR > 90dB | Analyser test tone |
| Save/Restore | 100% des params | Test manuel |

---

## 📅 Planning Recommandé

### Semaine 1
- ✅ **Jour 1-2:** PHASE 1 - Multi-instance safety
- ⚠️ **Jour 3-5:** PHASE 2.1 - Audio processing (début)

### Semaine 2
- ⚠️ **Jour 1-3:** PHASE 2.1 - Audio processing (fin + tests)
- ⚠️ **Jour 4-5:** PHASE 2.2 - Validation entrées

### Semaine 3
- 🎨 **Jour 1-2:** PHASE 3.1 - FlexBox layout
- 🎨 **Jour 3:** PHASE 3.2 - LookAndFeel custom
- 🎨 **Jour 4-5:** PHASE 3.3 - UI feedback

### Semaine 4
- 🔧 **Jour 1-2:** PHASE 4.1 - ConfigManager
- 🔧 **Jour 3:** PHASE 4.2 - Tests unitaires
- 🔧 **Jour 4-5:** PHASE 4.3-4.4 - Doc + cleanup

---

## 🚨 Risques Identifiés

### R1: Intégration Audio Complexe
**Risque:** Les moteurs de synthèse existants (luxstral, luxsynth) peuvent ne pas être RT-safe.  
**Mitigation:** 
- Auditer chaque synthé pour RT-safety
- Créer wrapper lock-free si nécessaire
- Tester avec ASan/TSan

### R2: Refactoring Configuration Globale
**Risque:** Modification des fonctions C peut casser le standalone.  
**Mitigation:**
- Tests régression sur standalone après chaque modif
- Garder API C rétrocompatible
- Utiliser feature flags si nécessaire

### R3: Performance Audio
**Risque:** Pipeline de synthèse trop lourd pour processBlock().  
**Mitigation:**
- Profiler tôt et souvent
- Optimiser algorithmes critiques (NEON sur ARM)
- Prévoir fallback vers rendering offline si trop lourd

---

## 📝 Notes d'Implémentation

### Conformité .clinerules
- ✅ Code et commentaires en anglais
- ✅ Documentation formelle en français (ce document)
- ✅ Commits en Conventional Commits (anglais)
- ✅ Respect contraintes RT-audio
- ✅ Build targets: macOS + Raspberry Pi 5

### Exemples de Commits
```bash
fix(vst): resolve multi-instance config race condition
feat(vst): implement real-time audio synthesis in processBlock
refactor(vst): centralize UDP configuration management
style(vst): migrate UI layout to FlexBox
docs(vst): add multi-instance limitation notice
test(vst): add unit tests for IP validation
```

---

## ✅ Checklist Globale

### Phase 1: Critiques
- [ ] Résoudre multi-instance safety (Option A ou B)
- [ ] Tester avec plusieurs instances
- [ ] Documenter limitation si Option B

### Phase 2: Fonctionnelles
- [ ] Implémenter fillAudioBuffer() RT-safe
- [ ] Implémenter prepareToPlay()
- [ ] Améliorer validation IP
- [ ] Nettoyer code AI-generated
- [ ] Tests de performance audio

### Phase 3: UI/UX
- [ ] Migrer vers FlexBox
- [ ] Créer Sp3ctraLookAndFeel
- [ ] Améliorer feedback UI
- [ ] Tests multi-résolutions

### Phase 4: Qualité
- [ ] Centraliser ConfigManager
- [ ] Ajouter tests unitaires
- [ ] Améliorer documentation
- [ ] Nettoyer code mort

### Tests Finaux
- [ ] Test multi-instance (T2)
- [ ] Test parameter automation (T3)
- [ ] Test save/restore (T4)
- [ ] Test RT-performance (T5)
- [ ] Test qualité audio (T1)

---

## 📚 Références

- [semantic_analysis_vst.md](scripts/code_review/reports/semantic_analysis_vst.md) - Analyse détaillée
- [CONSOLIDATED_REPORT.txt](scripts/code_review/reports/CONSOLIDATED_REPORT.txt) - Tous les agents
- [AUDIO_QUALITY_FIXES.md](vst/AUDIO_QUALITY_FIXES.md) - Fix test tone appliqué
- [NOTES_ARCHITECTURE.md](vst/NOTES_ARCHITECTURE.md) - Architecture VST
- [.clinerules/](../.clinerules/) - Standards de développement

---

**Auteur:** Plan de correction automatisé  
**Dernière mise à jour:** 2026-01-16  
**Statut:** À valider et démarrer PHASE 1
