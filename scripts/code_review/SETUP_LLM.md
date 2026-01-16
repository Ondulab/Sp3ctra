# 🤖 Setup Guide - Agent LLM (Claude)

## Vue d'ensemble

L'Agent 7 (LLM Semantic Analyzer) utilise Claude d'Anthropic pour une analyse sémantique approfondie du code VST. Contrairement aux agents pattern-based (regex) ou AST-based (clang-tidy), cet agent comprend le **contexte** et la **sémantique** du code.

## 🎯 Pourquoi Claude ?

**Avantages vs analyse traditionnelle :**
- ✅ Comprend l'intention du code (pas juste la syntaxe)
- ✅ Détecte des patterns subtils impossibles avec regex
- ✅ Analyse contextuelle (RT-audio safety, JUCE best practices)
- ✅ Suggestions d'amélioration intelligentes
- ✅ Détection de bugs logiques complexes

**Limitations :**
- ⚠️ Coût par analyse (~$0.10-0.50 selon la taille du code)
- ⚠️ Nécessite connexion internet
- ⚠️ Plus lent que les analyses locales

## 📋 Prérequis

1. **Compte Anthropic** (gratuit avec crédits de démarrage)
2. **Clé API** (récupérable sur la console)
3. **Package Python anthropic**

## 🚀 Installation

### Étape 1 : Créer un compte Anthropic

Aller sur : https://console.anthropic.com/

1. Créer un compte (gratuit)
2. Vérifier l'email
3. Récupérer les crédits gratuits ($5 offerts pour débuter)

### Étape 2 : Obtenir une clé API

1. Aller dans **Settings** → **API Keys**
2. Cliquer sur **Create Key**
3. Copier la clé (format : `sk-ant-api03-...`)
4. **⚠️ IMPORTANT** : Ne jamais commiter la clé dans Git !

### Étape 3 : Installer le package Python

```bash
pip3 install anthropic
```

### Étape 4 : Configurer la clé API

**Permanent (recommandé) :**
```bash
# Ajouter à ~/.zshrc ou ~/.bash_profile
echo 'export ANTHROPIC_API_KEY="sk-ant-api03-your-key-here"' >> ~/.zshrc
source ~/.zshrc
```

**Pour une session uniquement :**
```bash
export ANTHROPIC_API_KEY='sk-ant-api03-your-key-here'
```

## ✅ Vérification

```bash
# Tester que la clé API fonctionne
python3 scripts/code_review/agent_llm_semantic.py
```

**Output attendu :**
```
✓ Claude API available
🤖 Starting LLM Semantic Analyzer Agent (Claude)...
```

## 🎬 Utilisation

### Analyse Complète (Tous les Agents)

```bash
# Exécuter tous les agents incluant Claude
python3 scripts/code_review/run_code_review.py
```

L'Agent 7 sera automatiquement inclus si la clé API est configurée.

## 💰 Coûts

**Modèle utilisé :** Claude 3.5 Sonnet

**Estimation pour ce projet :**
- 4 fichiers VST (~2,000 lignes total)
- **Coût par analyse complète : ~$0.05-0.10**

**Crédits gratuits :** $5 offerts = ~50-100 analyses gratuites

## 🔒 Sécurité

### ⚠️ NE JAMAIS :
- ❌ Commiter la clé API dans Git
- ❌ Partager la clé publiquement
- ❌ Hardcoder la clé dans le code source

### ✅ BONNES PRATIQUES :
- ✅ Utiliser variable d'environnement
- ✅ Ajouter `.env` au `.gitignore` si utilisé
- ✅ Révoquer et recréer la clé si compromise

## 🐛 Dépannage

### Erreur : "anthropic module not found"
```bash
pip3 install anthropic
```

### Erreur : "API key not set"
```bash
# Vérifier que la variable existe
echo $ANTHROPIC_API_KEY

# Si vide, la définir
export ANTHROPIC_API_KEY='your-key'
```

### Erreur : "Authentication error"
- Clé invalide ou expirée
- Vérifier sur console.anthropic.com
- Créer une nouvelle clé

### Erreur : "Insufficient credits"
- Crédits épuisés
- Ajouter des crédits sur console.anthropic.com

## 📚 Ressources

- **Documentation :** https://docs.anthropic.com/
- **Console :** https://console.anthropic.com/
- **Pricing :** https://www.anthropic.com/pricing

---

**Créé le :** 2026-01-16  
**Version :** 1.0
