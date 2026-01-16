# 🤖 Prompts LLM pour Analyse Sémantique du Code VST

Ce fichier contient des prompts prêts à copier/coller dans ChatGPT, Claude, ou tout autre LLM pour obtenir une analyse sémantique approfondie de votre code VST.

## 📋 Comment Utiliser

1. Copier le prompt ci-dessous
2. Ouvrir ChatGPT/Claude/autre LLM
3. Coller le prompt
4. Copier le code du fichier à analyser
5. Envoyer et analyser les résultats

---

## 🎯 Prompt Général - Analyse VST Sp3ctra

```
Tu es un développeur C++ senior spécialisé en audio temps-réel (RT-audio) et plugins VST avec le framework JUCE.

Analyse ce fichier de code VST pour le projet Sp3ctra en te concentrant sur :

1. **RT-Audio Safety** : Identifie toute opération non-sûre dans le callback audio (malloc/new, mutex, syscalls, logging, std::vector::push_back, std::string operations, allocations cachées)

2. **Architecture** : Séparation des responsabilités, injection de dépendances, état global, couplage entre composants

3. **JUCE Best Practices** : Utilisation correcte d'AudioProcessor, APVTS, thread-safety UI, gestion des paramètres

4. **Bugs Subtils** : Erreurs logiques, cas limites non gérés, fuites de ressources, déréférencements nullptr

5. **Code Smells** : God objects, feature envy, primitive obsession, duplications, complexité excessive

Fournis une analyse structurée avec :
- Niveau de sévérité (ERROR/WARNING/INFO)
- Numéro de ligne si applicable
- Explication claire du problème
- Suggestion de correction

Code à analyser :

[COLLER LE CODE ICI]
```

---

## 🏗️ Prompt Spécialisé - PluginProcessor

```
Analyse spécifique pour PluginProcessor.cpp (VST JUCE) :

Contexte : Plugin VST audio temps-réel pour synthèse sonore

Points critiques à vérifier :

1. **processBlock()** :
   - Aucune allocation dynamique (directe ou indirecte)
   - Pas de locks/mutex
   - Pas d'I/O ou logging
   - Traitement borné en temps

2. **prepareToPlay()** :
   - Toutes les allocations faites ici
   - Buffers pré-alloués
   - Initialisation complète

3. **Multi-Instance** :
   - Pas d'état global/singleton
   - Chaque instance isolée
   - Thread-safe

4. **APVTS** :
   - Paramètres liés correctement
   - Atomics pour accès cross-thread
   - Aucune dépendance .ini

Code PluginProcessor.cpp :

[COLLER LE CODE ICI]
```

---

## 🎨 Prompt Spécialisé - UI (PluginEditor)

```
Analyse spécifique pour PluginEditor.cpp (Interface JUCE) :

Vérifie :

1. **Thread Safety** :
   - Aucun appel audio thread → UI
   - MessageManager pour updates UI
   - Listeners/Attachments corrects

2. **APVTS Bindings** :
   - SliderAttachment pour chaque contrôle
   - Pas d'accès direct aux paramètres
   - Updates bidirectionnels

3. **Layout** :
   - Responsive (pas de hardcoded setBounds)
   - FlexBox ou Grid recommandé
   - Gestion resize

4. **Consistance** :
   - Palette de couleurs unifiée
   - Tailles de police cohérentes
   - Nommage des composants

Code PluginEditor.cpp :

[COLLER LE CODE ICI]
```

---

## 🔧 Prompt Spécialisé - Sp3ctraCore

```
Analyse spécifique pour Sp3ctraCore.cpp (Logique métier) :

Contexte : Couche intermédiaire entre PluginProcessor et moteurs de synthèse

Vérifie :

1. **Séparation des Responsabilités** :
   - Pas de logique UI ici
   - Pas d'accès direct aux paramètres VST
   - Interface claire avec PluginProcessor

2. **RT-Safety** :
   - Toutes les méthodes appelées depuis processBlock sont RT-safe
   - Pas d'allocations dynamiques
   - Pas de locks

3. **Gestion Réseau** :
   - UDP géré en thread séparé
   - Lock-free pour communication audio thread
   - Pas de blocage

4. **Architecture** :
   - Couplage faible avec les dépendances
   - Injection possible pour tests
   - Gestion d'erreurs robuste

Code Sp3ctraCore.cpp :

[COLLER LE CODE ICI]
```

---

## 🎛️ Prompt Spécialisé - SettingsWindow

```
Analyse spécifique pour SettingsWindow.cpp (Fenêtre de configuration) :

Vérifie :

1. **Isolation UI** :
   - Pas d'impact sur processBlock
   - Thread UI uniquement
   - Fermeture propre

2. **Configuration** :
   - Validation des entrées
   - Sauvegarde persistante
   - Gestion erreurs réseau

3. **JUCE Components** :
   - Utilisation correcte des composants
   - Gestion mémoire (smart pointers)
   - Cleanup dans destructeur

4. **User Experience** :
   - Messages d'erreur clairs
   - Feedback visuel
   - États cohérents

Code SettingsWindow.cpp :

[COLLER LE CODE ICI]
```

---

## 🔍 Prompt - Analyse Comparative

```
Compare ces deux fichiers pour détecter :

1. **Code Dupliqué** : Fonctions similaires qui devraient être mutualisées
2. **Incohérences** : Patterns différents pour même objectif
3. **Opportunités de Refactoring** : Code qui devrait être dans une classe commune

Fichier 1 - [NOM] :
[CODE 1]

Fichier 2 - [NOM] :
[CODE 2]

Propose des refactorings concrets avec exemples de code.
```

---

## 📊 Prompt - Métriques et Complexité

```
Analyse la complexité de ce code :

1. **Complexité Cyclomatique** : Identifie les fonctions trop complexes (>15)
2. **Taille des Fonctions** : Fonctions trop longues (>100 lignes)
3. **Profondeur d'Imbrication** : If/for imbriqués >3 niveaux
4. **Nombre de Responsabilités** : Classes avec trop de rôles

Pour chaque problème :
- Propose un refactoring
- Montre comment simplifier
- Explique les bénéfices

Code :

[COLLER LE CODE ICI]
```

---

## 🚀 Workflow Recommandé

### Analyse Complète d'un Fichier

1. **Première passe** : Utiliser le Prompt Général
2. **Deuxième passe** : Utiliser le Prompt Spécialisé correspondant
3. **Troisième passe** : Prompt Métriques et Complexité

### Analyse Croisée

- Utiliser Prompt Comparatif pour PluginProcessor vs Standalone
- Comparer PluginEditor avec patterns JUCE standards

### Documentation des Résultats

Créer un fichier `LLM_ANALYSIS_[DATE].md` avec :
- Prompts utilisés
- Réponses LLM
- Actions à prendre
- Priorités

---

## 💡 Conseils d'Utilisation

1. **Contexte** : Toujours fournir le contexte du projet (VST, RT-audio, JUCE)
2. **Itératif** : Poser des questions de suivi sur les points peu clairs
3. **Code Snippets** : Si le fichier est trop long, analyser par sections
4. **Cross-Check** : Comparer les suggestions LLM avec les rapports des agents automatiques
5. **Validation** : Tester toute modification suggérée avant de l'appliquer

---

## 📝 Template de Rapport

```markdown
# Analyse LLM - [FICHIER] - [DATE]

## Contexte
- Fichier analysé : [nom]
- Lignes de code : [nombre]
- LLM utilisé : [ChatGPT/Claude/autre]

## Issues Identifiées

### RT-Audio Safety
1. [Description issue]
   - Ligne : [numéro]
   - Sévérité : [ERROR/WARNING]
   - Fix suggéré : [description]

### Architecture
1. [Description issue]

### Bugs Potentiels
1. [Description issue]

## Recommandations Prioritaires
1. [Recommandation 1]
2. [Recommandation 2]
3. [Recommandation 3]

## Actions
- [ ] Action 1
- [ ] Action 2
- [ ] Action 3
```

---

**Auteur** : Système de Revue de Code Sp3ctra  
**Dernière mise à jour** : 2026-01-16
