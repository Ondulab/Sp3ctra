# Markdown to HTML Converter

Script Python pour convertir automatiquement les fichiers Markdown en HTML en respectant la charte définie dans `docs/HTML_EXPORT_CHARTER.md`.

## Installation

### Prérequis

- Python 3.7 ou supérieur
- Bibliothèque `mistune` (v3.0.0+)

### Installation de la dépendance

```bash
pip install mistune>=3.0.0
```

## Usage

### Syntaxe de base

```bash
python scripts/docs/md_to_html.py <input_path> [options]
```

### Exemples d'utilisation

#### Convertir un fichier unique

```bash
# Génère les deux versions (standard + Google Docs)
python scripts/docs/md_to_html.py docs/IMAGE_SEQUENCER_SPECIFICATION.md

# Sortie :
# - docs/IMAGE_SEQUENCER_SPECIFICATION.html
# - docs/IMAGE_SEQUENCER_SPECIFICATION_gdocs.html
```

#### Convertir tous les fichiers d'un dossier

```bash
# Convertit tous les .md du dossier docs/ (non récursif)
python scripts/docs/md_to_html.py docs/

# Convertit récursivement tous les .md
python scripts/docs/md_to_html.py docs/ --recursive
```

#### Choisir le template de sortie

```bash
# Générer uniquement la version standard (avec CSS)
python scripts/docs/md_to_html.py docs/ --template standard

# Générer uniquement la version Google Docs (styles inline)
python scripts/docs/md_to_html.py docs/ --template gdocs

# Générer les deux versions (par défaut)
python scripts/docs/md_to_html.py docs/ --template both
```

#### Exclure des fichiers

```bash
# Ne pas convertir README.md et CHANGELOG.md
python scripts/docs/md_to_html.py docs/ --recursive --exclude README.md CHANGELOG.md
```

## Options

| Option | Description | Valeurs | Défaut |
|--------|-------------|---------|--------|
| `--template` | Type de template HTML à générer | `standard`, `gdocs`, `both` | `both` |
| `--recursive` | Traiter les sous-dossiers | Flag (true/false) | `false` |
| `--exclude` | Fichiers à exclure | Liste de noms de fichiers | Aucun |

## Types de Templates

### Template Standard (`*.html`)

- Utilise des variables CSS (`:root`)
- Styles dans une balise `<style>` dans le `<head>`
- Optimisé pour affichage web et export Word/LibreOffice
- Meilleure maintenabilité du code HTML

**Usage recommandé :** Documentation web, exports vers Word/LibreOffice

### Template Google Docs (`*_gdocs.html`)

- Utilise uniquement des styles inline
- Compatibilité maximale avec Google Docs
- Styles préservés lors du copy-paste
- Format optimisé pour import direct

**Usage recommandé :** Import dans Google Docs via copy-paste

## Spécifications de la Charte

Le script respecte les spécifications suivantes de la charte HTML :

### Typographie

- **H1** : 32px, letter-spacing: -0.01em
- **H2** : 24px, font-weight: 700
- **H3** : 18px, font-weight: 700
- **Corps de texte** : 15px, line-height: 1.55
- **Police** : Inter, Roboto, Segoe UI, Arial, sans-serif

### Code

- **Inline** : `<code>` avec background #f6f8fa, border, border-radius
- **Blocs** : `<pre><code>` avec pre-wrap pour compatibilité Google Docs
- **Police monospace** : ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas

### Tables

- Border-collapse: collapse
- Cellules avec borders et padding
- En-têtes avec background #f3f4f6

### Couleurs (Design Tokens)

```css
--text: #101114
--muted: #5f6673
--border: #DADCE0
--bg: #ffffff
--code-bg: #f6f8fa
--code-fg: #0b1021
--table-head: #f3f4f6
```

## Titre du Document

Le titre est automatiquement généré à partir du nom du fichier :

- `IMAGE_SEQUENCER_SPECIFICATION.md` → `"Image Sequencer Specification"`
- Les underscores sont remplacés par des espaces
- Chaque mot est capitalisé

## Extensions Markdown Supportées

Le script supporte les extensions Markdown suivantes :

- ✅ **Tables** : Tableaux avec en-têtes et alignement
- ✅ **Fenced code blocks** : Blocs de code avec triple backticks
- ✅ **Task lists** : Listes de tâches avec `[ ]` et `[x]`
- ✅ **Strikethrough** : Texte barré avec `~~texte~~`
- ✅ **Footnotes** : Notes de bas de page
- ✅ **Auto-linking URLs** : Conversion automatique des URLs

## Sortie

Les fichiers HTML sont générés dans le **même dossier** que les fichiers Markdown source :

```
docs/
  ├── IMAGE_SEQUENCER_SPECIFICATION.md
  ├── IMAGE_SEQUENCER_SPECIFICATION.html         # Version standard
  ├── IMAGE_SEQUENCER_SPECIFICATION_gdocs.html   # Version Google Docs
  ├── MIDI_SYSTEM_SPECIFICATION.md
  ├── MIDI_SYSTEM_SPECIFICATION.html
  └── MIDI_SYSTEM_SPECIFICATION_gdocs.html
```

## Compatibilité

- **Python** : 3.7+
- **Navigateurs** : Tous les navigateurs modernes
- **Google Docs** : Import via copy-paste (version `_gdocs.html`)
- **Microsoft Word** : Import HTML (version standard)
- **LibreOffice** : Import HTML (version standard)

## Dépannage

### Erreur : `mistune library not found`

```bash
pip install mistune>=3.0.0
```

### Erreur : `Path not found`

Vérifiez que le chemin vers le fichier ou dossier existe :

```bash
ls docs/IMAGE_SEQUENCER_SPECIFICATION.md
```

### Les styles ne sont pas préservés dans Google Docs

Utilisez la version `_gdocs.html` avec styles inline :

```bash
python scripts/docs/md_to_html.py docs/ --template gdocs
```

## Workflow Recommandé

### Pour Documentation Web

```bash
python scripts/docs/md_to_html.py docs/ --recursive --template standard
```

### Pour Import Google Docs

```bash
python scripts/docs/md_to_html.py docs/MON_FICHIER.md --template gdocs
```

Puis :
1. Ouvrir `MON_FICHIER_gdocs.html` dans un navigateur
2. Sélectionner tout le contenu (Cmd+A / Ctrl+A)
3. Copier (Cmd+C / Ctrl+C)
4. Coller dans Google Docs (Cmd+V / Ctrl+V)

### Conversion Batch de Toute la Documentation

```bash
python scripts/docs/md_to_html.py docs/ --recursive --template both
```

## Personnalisation

Pour modifier les styles, éditez le fichier `scripts/docs/md_to_html.py` :

- **Variables CSS** : Section `CSS_VARIABLES`
- **Styles standards** : Section `STANDARD_CSS`
- **Styles inline** : Dictionnaire `INLINE_STYLES`

## Support

Pour toute question ou problème, référez-vous à :

- `docs/HTML_EXPORT_CHARTER.md` : Spécifications complètes de la charte
- Code source : `scripts/docs/md_to_html.py`

## Changelog

### Version 1.0.0 (2025-10-30)

- ✨ Conversion Markdown → HTML avec mistune
- ✨ Support de deux templates (standard + Google Docs)
- ✨ Mode batch avec récursivité
- ✨ Exclusion de fichiers
- ✨ Respect complet de la charte HTML
- ✨ Support des extensions Markdown (tables, code, tasks, etc.)
- ✨ Génération automatique du titre depuis le nom de fichier
