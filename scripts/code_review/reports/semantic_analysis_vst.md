# Analyse Sémantique VST Sp3ctra - 2026-01-16

## Contexte
- **Fichiers analysés** : PluginProcessor.cpp, PluginEditor.cpp, Sp3ctraCore.cpp, SettingsWindow.cpp
- **Lignes de code totales** : ~1000
- **Méthode** : Application des prompts spécialisés définis dans PROMPTS_FOR_LLM.md
- **Focus** : RT-Audio Safety, Architecture JUCE, Thread Safety, Best Practices VST

---

## 🎛️ ANALYSE : PluginProcessor.cpp

### ✅ Points Forts

1. **APVTS Correctement Implémenté**
   - Utilisation appropriée d'AudioProcessorValueTreeState pour la gestion des paramètres
   - Sérialisation/désérialisation automatique dans getStateInformation/setStateInformation
   - Paramètres sauvegardés dans le projet DAW (pas de dépendance .ini) ✓

2. **Multi-Instance Safety**
   - Pas d'état global/singleton dans la classe (chaque instance a son propre sp3ctraCore)
   - Bon isolement entre instances ✓

3. **Cleanup Proper**
   - Destructeur bien structuré avec arrêt du thread UDP puis cleanup du core
   - Timeout de 2 secondes sur stopThread() pour éviter le blocage

### 🔴 Issues Critiques - RT-Audio Safety

#### ERROR #1: Logger dans processBlock()
**Ligne** : N/A (pas de log dans processBlock actuellement)  
**Sévérité** : ✅ RESOLVED  
**État** : Le processBlock actuel ne contient QUE la génération de test tone (440Hz), pas de logging.  
**Note** : Bien que le code actuel soit safe, il est important de maintenir cette discipline quand l'intégration avec Sp3ctraCore sera ajoutée.

#### WARNING #1: processBlock() incomplet
**Ligne** : 239-266  
**Sévérité** : ⚠️ WARNING  
**Problème** : Le processBlock() génère uniquement un tone 440Hz de test. Aucune intégration avec sp3ctraCore pour la synthèse réelle.
```cpp
void Sp3ctraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // ... génération test tone uniquement
    // PAS de: sp3ctraCore->processAudio(buffer);
}
```
**Impact** : Le plugin ne produit pas de son depuis les données UDP reçues. C'est une implémentation temporaire/stub.

**Suggestion** :
```cpp
void Sp3ctraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    // RT-SAFE: Appeler la méthode de traitement du core
    // (à condition que sp3ctraCore->processAudio() soit RT-safe)
    if (sp3ctraCore && sp3ctraCore->isInitialized()) {
        sp3ctraCore->fillAudioBuffer(buffer);
    } else {
        // Fallback: silence
        buffer.clear();
    }
}
```

#### WARNING #2: prepareToPlay() vide
**Ligne** : 224-228  
**Sévérité** : ⚠️ WARNING  
**Problème** : prepareToPlay() ne fait rien actuellement.
```cpp
void Sp3ctraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}
```
**Impact** : Aucune préallocation de buffers pour le traitement audio. Risque d'allocations dans processBlock() si du code de synthèse y est ajouté.

**Suggestion** :
```cpp
void Sp3ctraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Pré-allouer les buffers nécessaires pour RT-audio
    if (sp3ctraCore) {
        sp3ctraCore->prepareAudioProcessing(sampleRate, samplesPerBlock);
    }
    
    // Réinitialiser la phase du test tone
    testTonePhase = 0.0f;
}
```

### 🟡 Architecture & Best Practices

#### INFO #1: Global Config Access
**Ligne** : 341-349  
**Sévérité** : ℹ️ INFO  
**Problème** : Accès à `extern sp3ctra_config_t g_sp3ctra_config` - état global partagé.
```cpp
extern sp3ctra_config_t g_sp3ctra_config;
g_sp3ctra_config.udp_port = udpPort;
```
**Impact** : Si plusieurs instances du plugin sont chargées dans le DAW, elles partagent toutes le même g_sp3ctra_config. Cela peut causer des conflits.

**Suggestion** : 
- Soit : Déplacer la config dans Sp3ctraCore (pas global)
- Soit : Documenter clairement que g_sp3ctra_config est "last instance wins" et que c'est acceptable pour le use case actuel

#### INFO #2: testTonePhase membre de classe
**Ligne** : 249-262  
**Sévérité** : ✅ GOOD  
**Commentaire dans le code** : "CRITICAL FIX: Use member variable for phase persistence (not static)"  
**Analyse** : Excellent choix ! Utiliser une variable membre au lieu d'une static évite les problèmes de multi-instance. Chaque plugin a sa propre phase. ✓

### 🔒 Thread Safety

#### INFO #3: parameterChanged() appelle applyConfigurationToCore()
**Ligne** : 321-329  
**Sévérité** : ℹ️ INFO  
**Problème** : parameterChanged() est appelé depuis le message thread (UI) et appelle applyConfigurationToCore() qui peut redémarrer le socket UDP.
```cpp
void Sp3ctraAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    applyConfigurationToCore();  // Peut redémarrer UDP
}
```
**Analyse** : Correct pour ce cas d'usage (pas de lock dans processBlock). Le redémarrage UDP se fait en dehors du chemin audio RT.

**Vérification** : Confirmer que Sp3ctraCore::initialize() utilise std::lock_guard et ne bloque pas le thread audio. ✓ (vérifié dans Sp3ctraCore.cpp ligne 24)

---

## 🎨 ANALYSE : PluginEditor.cpp

### ✅ Points Forts

1. **Thread Safety UI**
   - Aucun accès direct au thread audio
   - Utilisation d'un Timer (1Hz) pour polling état du core ✓
   - Lecture thread-safe via std::atomic dans core

2. **APVTS Access Correct**
   - Lecture des paramètres via getRawParameterValue() ✓
   - Pas de modification directe des paramètres (seulement lecture pour affichage)

3. **Cleanup Proper**
   - stopTimer() dans le destructeur ✓
   - settingsWindow.reset() pour libérer la fenêtre modale

### 🟡 UI/UX Issues

#### WARNING #3: Layout hardcodé
**Ligne** : 51-66  
**Sévérité** : ⚠️ WARNING  
**Problème** : Utilisation de setBounds() avec des valeurs hardcodées au lieu de FlexBox/Grid.
```cpp
void Sp3ctraAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(70);  // Hardcoded
    bounds.reduce(20, 10);     // Hardcoded
    settingsButton.setBounds(bounds.removeFromTop(40).reduced(80, 0));  // Hardcoded
}
```
**Impact** : Pas responsive, difficile à maintenir si on ajoute des contrôles.

**Suggestion** : Utiliser FlexBox pour un layout plus flexible :
```cpp
void Sp3ctraAudioProcessorEditor::resized()
{
    juce::FlexBox fb;
    fb.flexDirection = juce::FlexBox::Direction::column;
    fb.items.add(juce::FlexItem(settingsButton).withHeight(40).withMargin(10));
    fb.items.add(juce::FlexItem(statusLabel).withHeight(30).withMargin(5));
    fb.items.add(juce::FlexItem(infoLabel).withHeight(60).withMargin(5));
    
    auto bounds = getLocalBounds().reduced(20, 10);
    bounds.removeFromTop(70);
    fb.performLayout(bounds);
}
```

#### INFO #4: Palette de couleurs hardcodée
**Ligne** : 36-46  
**Sévérité** : ℹ️ INFO  
**Problème** : Couleurs hardcodées dans paint() au lieu d'utiliser un LookAndFeel centralisé.
```cpp
g.fillAll(juce::Colour(0xff2a2a2a));
g.setGradientFill(juce::ColourGradient(
    juce::Colour(0xff404040), 0, 0,
    juce::Colour(0xff2a2a2a), 0, (float)headerArea.getHeight(),
    false));
```
**Impact** : Difficile de créer un thème cohérent sur toute l'UI. Duplication des couleurs.

**Suggestion** : Créer un Sp3ctraLookAndFeel custom :
```cpp
// Sp3ctraLookAndFeel.h
class Sp3ctraLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static const juce::Colour BACKGROUND_DARK;
    static const juce::Colour BACKGROUND_MEDIUM;
    static const juce::Colour ACCENT_PRIMARY;
    // ...
};
```

#### WARNING #4: settingsWindow lifecycle
**Ligne** : 82-88  
**Sévérité** : ⚠️ WARNING  
**Problème** : La fenêtre de settings persiste même fermée (setVisible(false)).
```cpp
void Sp3ctraAudioProcessorEditor::openSettings()
{
    if (!settingsWindow) {
        settingsWindow = std::make_unique<SettingsWindow>(audioProcessor);
    } else {
        settingsWindow->setVisible(true);  // Réaffiche fenêtre cachée
    }
}
```
**Impact** : La fenêtre reste en mémoire même quand fermée. Acceptable pour une petite fenêtre de config, mais consomme des ressources inutilement.

**Suggestion** : Recréer la fenêtre à chaque ouverture (ou la détruire dans closeButtonPressed).

---

## 🏗️ ANALYSE : Sp3ctraCore.cpp

### ✅ Points Forts

1. **Séparation des Responsabilités**
   - Pas de logique UI dans ce fichier ✓
   - Interface claire avec PluginProcessor via ActiveConfig
   - Encapsulation de la logique C avec extern "C"

2. **Thread Safety**
   - Utilisation de std::mutex (configMutex) pour protéger les opérations de config ✓
   - Atomics (initialized, udpRunning, socketFd) pour état partagé
   - Aucune allocation dans les méthodes appelables depuis RT (actuellement aucune)

3. **Gestion d'Erreurs**
   - try/catch autour des allocations ✓
   - Vérifications de retour (udp_Init, audio_image_buffers_init)
   - Cleanup proper dans shutdown()

### 🔴 Issues Critiques

#### ERROR #2: Accès global à g_sp3ctra_config
**Ligne** : 157-169  
**Sévérité** : 🔴 ERROR (multi-instance)  
**Problème** : Modification d'une variable globale depuis initializeUdp().
```cpp
extern sp3ctra_config_t g_sp3ctra_config;
g_sp3ctra_config.udp_port = port;
strncpy(g_sp3ctra_config.udp_address, address.c_str(), ...);
```
**Impact** : Si plusieurs instances du plugin VST sont chargées, elles écrasent mutuellement la config globale. **Race condition potentielle**.

**Suggestion** : 
1. Passer la config directement à udp_Init() via paramètres (refactorer la fonction C)
2. Ou : Documenter clairement que multi-instance n'est pas supporté actuellement

#### WARNING #5: Mutex dans initialize()
**Ligne** : 24-56  
**Sévérité** : ⚠️ WARNING  
**Problème** : std::lock_guard dans initialize() pourrait bloquer si appelé depuis multiple threads.
```cpp
bool Sp3ctraCore::initialize(const ActiveConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex);  // Peut bloquer
    // ...
}
```
**Analyse** : Acceptable car initialize() est appelé depuis le constructor ou parameterChanged() (message thread), jamais depuis processBlock(). **Pas un vrai problème RT.**

**Vérification** : ✓ Confirmé que initialize() n'est jamais appelé depuis le chemin audio RT.

### 🟡 Architecture Issues

#### WARNING #6: Allocations dans initializeBuffers()
**Ligne** : 99-145  
**Sévérité** : ⚠️ WARNING  
**Problème** : make_unique et memset dans initializeBuffers().
```cpp
bool Sp3ctraCore::initializeBuffers() {
    context = std::make_unique<Context>();      // Allocation
    memset(context.get(), 0, sizeof(Context));
    doubleBuffer = std::make_unique<DoubleBuffer>();  // Allocation
    // ...
}
```
**Analyse** : Acceptable car appelé uniquement depuis initialize() (hors chemin RT). Les buffers sont pré-alloués au démarrage, pas dans processBlock(). ✓

#### INFO #5: Utilisation de pthread_mutex
**Ligne** : 109-111  
**Sévérité** : ℹ️ INFO  
**Problème** : pthread_mutex_init pour context->imu_mutex.
```cpp
if (pthread_mutex_init(&context->imu_mutex, nullptr) != 0) {
    juce::Logger::writeToLog("Sp3ctraCore: ERROR - Failed to init IMU mutex");
    return false;
}
```
**Analyse** : Nécessaire pour compatibilité avec le code C existant. Correct pour ce cas d'usage. Le mutex ne doit JAMAIS être locké depuis le thread audio RT.

**Action recommandée** : Ajouter une assertion/documentation que imu_mutex n'est utilisé QUE par le thread UDP (non-RT).

#### WARNING #7: Pas de méthode processAudio()
**Ligne** : N/A  
**Sévérité** : ⚠️ WARNING  
**Problème** : Sp3ctraCore n'expose aucune méthode pour remplir l'AudioBuffer dans processBlock().
```cpp
// MANQUANT:
void Sp3ctraCore::fillAudioBuffer(juce::AudioBuffer<float>& buffer) {
    // Générer audio depuis les buffers image/IMU
}
```
**Impact** : Le plugin ne peut pas produire de son réel. C'est cohérent avec le stub processBlock() dans PluginProcessor.

**Suggestion** : Ajouter une méthode RT-safe :
```cpp
void Sp3ctraCore::fillAudioBuffer(juce::AudioBuffer<float>& buffer) {
    // RT-SAFE: Pas d'allocation, pas de lock, pas de I/O
    
    if (!initialized.load() || !context || !audioImageBuffers) {
        buffer.clear();
        return;
    }
    
    // Lire depuis audioImageBuffers (lock-free)
    // Appeler les moteurs de synthèse (luxstral, luxsynth, etc.)
    // Écrire dans buffer
}
```

---

## 🔧 ANALYSE : SettingsWindow.cpp

### ✅ Points Forts

1. **Isolation UI**
   - Pas d'impact sur processBlock() ✓
   - Thread UI uniquement
   - Fermeture propre (closeButtonPressed cache la fenêtre)

2. **APVTS Bindings**
   - ComboBoxAttachment pour sensorDpi et logLevel ✓
   - Binding bidirectionnel automatique
   - Changes propagées via parameterChanged() dans PluginProcessor

3. **Gestion Mémoire**
   - Smart pointers pour les attachments ✓
   - Cleanup automatique dans destructeur

### 🟡 UI/UX Issues

#### WARNING #8: Validation IP address faible
**Ligne** : 78-98  
**Sévérité** : ⚠️ WARNING  
**Problème** : Validation basique de l'adresse IP (juste check si contient '.' et longueur >= 7).
```cpp
if (address.containsChar('.') && address.length() >= 7) {
    // Considéré valide
}
```
**Impact** : Accepte des adresses invalides comme "1.2.3.999" ou "a.b.c.d".

**Suggestion** : Validation regex ou parsing proper :
```cpp
bool isValidIPAddress(const juce::String& ip) {
    juce::StringArray parts;
    parts.addTokens(ip, ".", "");
    
    if (parts.size() != 4) return false;
    
    for (const auto& part : parts) {
        int val = part.getIntValue();
        if (val < 0 || val > 255) return false;
    }
    
    return true;
}
```

#### WARNING #9: UDP Address limited to presets
**Ligne** : 78-98  
**Sévérité** : ⚠️ WARNING  
**Problème** : L'utilisateur peut taper une adresse custom, mais elle est "snappée" au preset le plus proche.
```cpp
int newIndex = 0;
for (int i = 0; i < Sp3ctraAudioProcessor::UDP_ADDRESS_PRESETS.size(); ++i) {
    if (Sp3ctraAudioProcessor::UDP_ADDRESS_PRESETS[i] == address) {
        newIndex = i;
        break;
    }
}
// Si pas trouvé, utilise index 0 (première preset)
```
**Impact** : Si l'utilisateur veut utiliser "192.168.1.200" et qu'elle n'est pas dans les presets, elle est remplacée par "239.100.100.100" (preset 0).

**Suggestion** : 
- Soit : Remplacer AudioParameterChoice par AudioParameterString (custom address)
- Soit : Ajouter un preset "Custom" et stocker l'adresse custom séparément

#### INFO #6: Apply Button désactivé
**Ligne** : 138-140  
**Sévérité** : ℹ️ INFO  
**Problème** : Le bouton "Apply" est désactivé avec texte "Changes Applied Automatically".
```cpp
applyButton.setButtonText("Changes Applied Automatically");
applyButton.setEnabled(false);  // Disabled - just informational
```
**Analyse** : C'est un choix de design. APVTS applique automatiquement les changements via parameterChanged(). Pas besoin de bouton Apply.

**UX Suggestion** : Soit supprimer complètement le bouton, soit le remplacer par un bouton "Test Connection" qui teste la connexion UDP.

#### WARNING #10: Layout hardcodé (identique PluginEditor)
**Ligne** : 195-225  
**Sévérité** : ⚠️ WARNING  
**Problème** : Utilisation de setBounds() avec des valeurs hardcodées.
```cpp
const int labelWidth = 120;
const int rowHeight = 35;
const int padding = 10;
```
**Suggestion** : Utiliser FlexBox pour un layout plus maintenable.

---

## 📊 ANALYSE COMPARATIVE : PluginProcessor vs Sp3ctraCore

### Duplication de Code

#### DUPLICATION #1: Configuration globale
- **Fichier 1** : PluginProcessor.cpp (ligne 341-349)
- **Fichier 2** : Sp3ctraCore.cpp (ligne 157-169)
- **Problème** : Les deux fichiers modifient `extern sp3ctra_config_t g_sp3ctra_config`.

**Refactoring suggéré** : Centraliser dans Sp3ctraCore uniquement.

```cpp
// PluginProcessor.cpp
void Sp3ctraAudioProcessor::applyConfigurationToCore()
{
    Sp3ctraCore::ActiveConfig config;
    config.udpPort = (int)udpPortParam->load();
    config.udpAddress = UDP_ADDRESS_PRESETS[...].toStdString();
    config.logLevel = (int)logLevelParam->load();
    
    sp3ctraCore->initialize(config);  // Core gère g_sp3ctra_config en interne
}

// Sp3ctraCore.cpp - SEUL responsable de g_sp3ctra_config
bool Sp3ctraCore::initialize(const ActiveConfig& config) {
    updateGlobalConfig(config);  // Méthode privée
    // ...
}
```

### Incohérences

#### INCONSISTENCY #1: Logging
- **PluginProcessor** : Utilise `juce::Logger::writeToLog()`
- **Sp3ctraCore** : Utilise `juce::Logger::writeToLog()` ET `logger_init()` (C logger)

**Impact** : Deux systèmes de logging parallèles. Risque de perte de messages ou duplication.

**Suggestion** : Créer un logger bridge :
```cpp
// LoggerBridge.cpp
void juceLogToC(const char* message) {
    LOG_INFO(message);  // C logger
    juce::Logger::writeToLog(message);  // JUCE logger
}
```

---

## 📈 MÉTRIQUES ET COMPLEXITÉ

### Complexité Cyclomatique

| Fichier | Fonction | Complexité | Verdict |
|---------|----------|------------|---------|
| PluginProcessor.cpp | createParameterLayout() | 2 | ✅ Simple |
| PluginProcessor.cpp | processBlock() | 3 | ✅ Simple |
| PluginProcessor.cpp | applyConfigurationToCore() | 5 | ✅ OK |
| Sp3ctraCore.cpp | initialize() | 8 | ✅ OK |
| Sp3ctraCore.cpp | initializeBuffers() | 6 | ✅ OK |
| SettingsWindow.cpp | resized() | 2 | ✅ Simple |

**Résultat** : Aucune fonction ne dépasse 15 de complexité cyclomatique. ✓

### Taille des Fonctions

| Fichier | Fonction | Lignes | Verdict |
|---------|----------|--------|---------|
| PluginProcessor.cpp | Constructor | 47 | ✅ OK |
| PluginProcessor.cpp | applyConfigurationToCore() | 41 | ✅ OK |
| Sp3ctraCore.cpp | initializeBuffers() | 47 | ✅ OK |
| SettingsWindow.cpp | Constructor | 115 | ⚠️ Long mais lisible |

**Résultat** : Aucune fonction ne dépasse 150 lignes. Acceptable.

### Profondeur d'Imbrication

**Résultat** : Maximum 3 niveaux (if/try/for). ✓ Acceptable.

---

## 🚨 RÉSUMÉ DES ISSUES PRIORITAIRES

### 🔴 Critiques (à corriger immédiatement)

1. **ERROR #2** : Accès global concurrent à `g_sp3ctra_config` (multi-instance unsafe)
   - **Fichiers** : PluginProcessor.cpp, Sp3ctraCore.cpp
   - **Action** : Encapsuler dans Sp3ctraCore ou documenter limitation

### ⚠️ Importantes (à corriger bientôt)

2. **WARNING #1** : processBlock() stub (pas de synthèse réelle)
   - **Fichier** : PluginProcessor.cpp
   - **Action** : Implémenter fillAudioBuffer() dans Sp3ctraCore

3. **WARNING #2** : prepareToPlay() vide (pas de préallocation)
   - **Fichier** : PluginProcessor.cpp
   - **Action** : Pré-allouer buffers audio

4. **WARNING #7** : Sp3ctraCore manque méthode processAudio()
   - **Fichier** : Sp3ctraCore.cpp
   - **Action** : Ajouter interface RT-safe pour remplir AudioBuffer

5. **WARNING #8** : Validation IP address faible
   - **Fichier** : SettingsWindow.cpp
   - **Action** : Ajouter validation regex proper

### ℹ️ Améliorations (nice to have)

6. **INFO #1** : État global g_sp3ctra_config
   - **Action** : Documenter ou refactorer

7. **WARNING #3, #10** : Layout UI hardcodé
   - **Action** : Migrer vers FlexBox

8. **INFO #4** : Palette couleurs hardcodée
   - **Action** : Créer Sp3ctraLookAndFeel custom

---

## ✅ RECOMMANDATIONS PRIORITAIRES

### 1. Résoudre Multi-Instance Issue (CRITIQUE)
```cpp
// Option A: Config par instance dans Sp3ctraCore
class Sp3ctraCore {
    sp3ctra_config_t instanceConfig;  // Pas global !
    // Passer instanceConfig aux fonctions C via paramètres
};

// Option B: Documenter limitation
// README.md
"⚠️ LIMITATION: Only one instance of Sp3ctra VST can be loaded at a time due to global config"
```

### 2. Implémenter Audio Processing
```cpp
// Sp3ctraCore.h
void fillAudioBuffer(juce::AudioBuffer<float>& buffer);

// Sp3ctraCore.cpp
void Sp3ctraCore::fillAudioBuffer(juce::AudioBuffer<float>& buffer) {
    // RT-SAFE: Aucune allocation, aucun lock, aucune I/O
    // Lire depuis audioImageBuffers (lock-free)
    // Appeler moteurs de synthèse
}

// PluginProcessor.cpp
void Sp3ctraAudioProcessor::processBlock(...) {
    if (sp3ctraCore && sp3ctraCore->isInitialized()) {
        sp3ctraCore->fillAudioBuffer(buffer);
    }
}
```

### 3. Ajouter prepareToPlay()
```cpp
void Sp3ctraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (sp3ctraCore) {
        sp3ctraCore->prepareAudioProcessing(sampleRate, samplesPerBlock);
    }
    testTonePhase = 0.0f;
}
```

### 4. Tests Recommandés
- [ ] Test multi-instance : Charger 2 instances dans Reaper/Ableton, changer configs
- [ ] Test RT-safety : Profiler processBlock() avec `perf`/Instruments
- [ ] Test parameter automation : Automatiser UDP port dans DAW
- [ ] Test save/restore : Sauvegarder projet, fermer, rouvrir, vérifier params

---

## 📝 ACTIONS CONCRÈTES

### Immédiat (cette semaine)
- [ ] Documenter limitation multi-instance dans README
- [ ] Ajouter assertion que imu_mutex n'est pas utilisé en RT
- [ ] Créer issue GitHub pour audio processing implementation

### Court terme (ce mois)
- [ ] Implémenter Sp3ctraCore::fillAudioBuffer()
- [ ] Ajouter prepareToPlay() avec préallocation
- [ ] Améliorer validation IP address
- [ ] Migrer UI vers FlexBox

### Moyen terme (trimestre)
- [ ] Refactorer pour éliminer g_sp3ctra_config global
- [ ] Créer Sp3ctraLookAndFeel custom
- [ ] Ajouter tests automatisés VST

---

## 🎓 NOTES GÉNÉRALES

### Points Positifs
- ✅ Code propre et bien structuré
- ✅ Bonne séparation C/C++ avec extern "C"
- ✅ APVTS correctement implémenté
- ✅ Thread safety UI respectée
- ✅ Pas de violations RT-audio ACTUELLES (car code stub)

### Points d'Attention
- ⚠️ Multi-instance non supporté (config globale)
- ⚠️ Audio processing incomplet (stub 440Hz)
- ⚠️ Validation entrées utilisateur à améliorer
- ⚠️ Layout UI hardcodé

### Conformité .clinerules
- ✅ Code en anglais (comments, logs) ✓
- ✅ Pas de French dans le code ✓
- ✅ RT-audio constraints documentées
- ⚠️ RT-audio pas encore implémenté (processBlock stub)

---

**Auteur** : Analyse Sémantique Sp3ctra (Agent LLM)  
**Date** : 2026-01-16  
**Fichiers analysés** : 4 (PluginProcessor, PluginEditor, Sp3ctraCore, SettingsWindow)  
**Issues trouvées** : 10 (1 ERROR, 9 WARNING/INFO)  
**Priorité** : Résoudre ERROR #2 et implémenter audio processing (WARNING #1, #7)
