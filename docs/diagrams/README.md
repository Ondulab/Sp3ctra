# Diagrammes PlantUML - Synthèse LuxStral Sp3ctra

Ce répertoire contient les diagrammes PlantUML documentant l'architecture complète du système de synthèse additive.

## 📋 Fichiers Disponibles

### 1. `additive_synthesis_architecture.puml`
**Vue d'ensemble globale** - Architecture complète en 5 phases

### 2. `additive_synthesis_threading.puml`
**Threading & Parallélisme** - Diagramme de séquence détaillé

### 3. `additive_synthesis_signal_flow.puml`
**Flux de traitement signal** - Diagramme d'activité par oscillateur

## 🚀 Génération des Images

### Installation PlantUML

```bash
# macOS
brew install plantuml

# Linux
sudo apt-get install plantuml
```

### Génération PNG

```bash
# Tous les diagrammes
plantuml -tpng docs/diagrams/*.puml

# Un seul diagramme
plantuml -tpng docs/diagrams/additive_synthesis_architecture.puml
```

### Génération SVG (vectoriel)

```bash
plantuml -tsvg docs/diagrams/*.puml
```

## 🎨 Visualisation dans VSCode

1. Installer l'extension **PlantUML** 
2. Ouvrir un fichier `.puml`
3. Appuyer sur `Alt+D` (ou `Cmd+D` sur macOS)

## 📚 Ressources

- [PlantUML Documentation](https://plantuml.com/)
- [VSCode Extension](https://marketplace.visualstudio.com/items?itemName=jebbs.plantuml)
