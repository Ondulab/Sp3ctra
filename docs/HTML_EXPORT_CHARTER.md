# Charte d'Export HTML pour Documentation
## Spécification de Génération HTML Optimisée Google Docs

### Vue d'ensemble
Cette charte définit les standards pour générer des pages HTML autonomes au style minimaliste, importables dans Google Docs sans perte de structure. L'accent est mis sur la mise en forme des blocs de code et la compatibilité maximale.

## 1. Scope & Objectifs
- **Générer une page HTML autonome** (no external CSS/JS) au style minimaliste, importable dans Google Docs sans perte de structure (titres, paragraphes, tables, blocs de code)
- **Mettre en avant deux types de code** : inline et bloc multi-lignes
- **Garantir lisibilité, impression propre et accessibilité basique**

## 2. Structure HTML Minimale
- Un seul document avec `<main class="doc">` comme conteneur
- Titres hiérarchisés (H1/H2/H3/H4) ; contenu en `<p>`, listes, `<table>`, et blocs de code en `<pre><code>...</code></pre>`
- Aucun JS ; pas d'images obligatoires

### Template de Base
```html
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>{Document Title}</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <!-- Embedded CSS only -->
  <style>
    /* See "3) Design tokens & base" and "4) Code formatting spec" */
  </style>
</head>
<body>
  <main class="doc">
    <h1>{Top Title}</h1>
    <p class="lead">{Short lead}</p>

    <h2 id="section-x">{Section}</h2>
    <p>Inline <code>token</code> example.</p>

    <pre><code>command subcmd --option value
# comments preserved
    </code></pre>

    <p class="caption"><strong>Note :</strong> {Short hint}</p>
  </main>
</body>
</html>
```

## 3. Design Tokens & Base
Déclarer des variables CSS (thème clair) pour cohérence et facilité de déclinaison :

```css
:root {
  --text: #101114;
  --muted: #5f6673;
  --border: #DADCE0;
  --bg: #ffffff;
  --code-bg: #f6f8fa;
  --code-fg: #0b1021;
  --accent: #0B57D0;
  --table-head: #f3f4f6;
}

html, body {
  background: var(--bg);
  color: var(--text);
  font-family: "Calibri", "Segoe UI", Arial, Helvetica, sans-serif;
  font-size: 12pt;
  line-height: 1.55;
  margin: 0;
  padding: 0;
}

.doc {
  max-width: 940px;
  margin: 48px auto 96px;
  padding: 0 24px;
}

h1, h2, h3, h4 {
  color: var(--text);
  margin: 1.6em 0 0.6em;
  line-height: 1.25;
}

h1 { 
  font-family: "Rubik", "Segoe UI", Arial, Helvetica, sans-serif;
  font-size: 16pt; 
  font-weight: 500;
  letter-spacing: -0.01em; 
}

h2 { 
  font-size: 18pt; 
  font-weight: 700;
}

h3 { 
  font-size: 14pt; 
  font-weight: 700;
  color: #404040; 
}

p { 
  margin: 0.6em 0 1em; 
}

.lead {
  color: var(--muted);
  margin-top: -8px;
}
```

## 4. Code Formatting Spec (Bloc & Inline)
### Pile Monospace Commune
Appliquée à `pre`, `code`, `kbd`, `samp` :

```css
pre, code, kbd, samp {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas,
               "Liberation Mono", "Courier New", monospace;
}
```

### 4.1 Inline Code
**Usage** : mots-clés, options, paramètres au sein d'un paragraphe, titres, tables.  
**Rendu** "capsule" sobre ; ne pas utiliser pour des lignes entières.

```css
code {
  background: var(--code-bg);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 1px 5px;
}
```
**Règles d'authoring** :
- Pas de retour à la ligne dans un `<code>` inline
- Échapper `<`, `>`, `&` (`&lt;`, `&gt;`, `&amp;`) si nécessaire

### 4.2 Bloc de Code Multi-lignes
**Structure obligatoire** : `<pre><code>…</code></pre>`  
Le style "bloc" est porté par `<pre>` ; `<code>` n'apporte que la police.

```css
pre {
  background: var(--code-bg);
  color: var(--code-fg);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 14px 16px;
  margin: 14px 0 18px;

  /* Wrapping compatible Google Docs */
  white-space: pre-wrap;   /* keep newlines, allow wraps */
  word-break: break-word;  /* break very long tokens if needed */
  overflow: auto;          /* scroll if still needed */
}
```
**Règles d'authoring** :
- Conserver la sémantique : pas de `<br>` à l'intérieur ; insérer de vrais sauts de ligne
- Pour les symboles, échapper comme en inline (`&lt;`, `&gt;`) — utile pour EBNF/HTML
- Éviter les tabulations dures si l'alignement exact est critique (préférer espaces)
- Ne pas imbriquer de listes/balises riches dans un bloc code

### Exemple — Mode Personnalisé

```html
<p>Le mode <code>custom</code> permet de spécifier une température de couleur précise en Kelvin :</p>
<pre><code>wb custom 5200K    # Balance des blancs à 5200 Kelvin
wb custom          # Mode personnalisé sans valeur spécifique</code></pre>
<p class="caption"><strong>Plage recommandée :</strong> 2000K à 10000K</p>
```

## 5. Tables, Captions, Séparateurs

```css
table {
  width: 100%;
  border-collapse: collapse;
  margin: 14px 0 20px;
  font-variant-numeric: tabular-nums;
}

th, td {
  border: 1px solid var(--border);
  padding: 8px 10px;
  vertical-align: top;
}

thead th {
  background: var(--table-head);
  text-align: left;
}

.caption {
  font-size: 12px;
  color: var(--muted);
  margin-top: -8px;
  margin-bottom: 12px;
}

.hr {
  height: 1px;
  background: var(--border);
  border: 0;
  margin: 24px 0;
}
```

## 6. Impression (Print) & Pagination
Éviter les coupures internes sur `pre` et `table` ; empêcher les orphelins de titres.

```css
@media print {
  .doc { margin: 0; }
  h2 { page-break-before: auto; }
  h2, h3 { page-break-after: avoid; }
  pre, table { page-break-inside: avoid; }
}
```

## 7. Accessibilité & UX

- **Contraste suffisant** du code (`--code-fg` vs `--code-bg`)
- **Pas de coloration syntaxique** (pour rester neutre et compatible GDoc) ; si nécessaire, l'ajouter plus tard côté web, pas dans la version à importer
- **Titres avec id** pour liens internes (`<h2 id="...">`)

## 8. Contraintes & Compatibilité Google Docs
### Limitations Google Docs
Google Docs présente des limitations importantes lors de l'import HTML :
- **CSS externe non supporté** lors du copy-paste
- **Classes CSS ignorées** dans certains contextes
- **Formatage des blocs de code** souvent perdu
- **Polices personnalisées** remplacées par défaut

### Recommandations de Compatibilité
- **CSS inline uniquement** (dans `<style>`)
- **Pas de position: fixed/sticky**, pas d'animations, pas de fontes custom via `@font-face`
- **Éviter les pseudo-éléments décoratifs** sur du contenu crucial
- **Préférer pre-wrap** pour conserver lisibilité après import (GDoc gère mieux les retours à la ligne que les scrolls horizontaux)
- **Éviter les colonnes CSS, floats complexes, grid imbriquées** — rester simple et fluide

### Solution : Styles Inline pour Compatibilité Maximale
Pour garantir la compatibilité maximale, utiliser **exclusivement des styles inline** :

```html
<!-- ❌ Éviter les classes CSS -->
<h2 class="title-style">Titre</h2>

<!-- ✅ Utiliser les styles inline -->
<h2 style="font-family: 'Inter', 'Roboto', 'Segoe UI', Arial, sans-serif; font-size: 24px; font-weight: 400; color: #101114; margin: 1.6em 0 0.6em; line-height: 1.25;">Titre</h2>
```

### Code Inline Optimisé
```html
<span style="font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, 'Liberation Mono', 'Courier New', monospace; background: #f6f8fa; border: 1px solid #DADCE0; border-radius: 6px; padding: 1px 5px;">Settings.Secure.ANDROID_ID</span>
```

### Bloc de Code Optimisé
```html
<pre style="background: #f6f8fa; color: #0b1021; border: 1px solid #DADCE0; border-radius: 8px; padding: 14px 16px; margin: 14px 0 18px; white-space: pre-wrap; word-break: break-word; overflow: auto; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, 'Liberation Mono', 'Courier New', monospace;"><code>// Exemple de code Kotlin
fun generateDeviceId(): String {
    val androidId = Settings.Secure.getString(
        contentResolver, 
        Settings.Secure.ANDROID_ID
    )
    return androidId ?: "unknown"
}</code></pre>
```

## 9. Checklist Auteur

- [ ] Chaque bloc de code est bien en `<pre><code>…</code></pre>`
- [ ] Les entités HTML sont échappées dans le code (`<`, `>`, `&`)
- [ ] Pas de CSS/JS externe ; pas d'images nécessaires à la structure
- [ ] Titres H1→H4 présents et ordonnés
- [ ] Les notes sous les blocs utilisent `.caption`
- [ ] Impression testée (aucun bloc code/table coupé)
- [ ] Import GDoc testé : titres, tables et blocs conservent leur forme

## 10. Templates Complets
### Template Standard (avec CSS)
```html
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Document Technique</title>
  <style>
    :root {
      --text: #101114;
      --muted: #5f6673;
      --border: #DADCE0;
      --bg: #ffffff;
      --code-bg: #f6f8fa;
      --code-fg: #0b1021;
      --accent: #0B57D0;
      --table-head: #f3f4f6;
    }

    html, body {
      background: var(--bg);
      color: var(--text);
      font-family: "Inter", "Roboto", "Segoe UI", Arial, Helvetica, sans-serif;
      font-size: 15px;
      line-height: 1.55;
      margin: 0;
      padding: 0;
    }

    .doc {
      max-width: 940px;
      margin: 48px auto 96px;
      padding: 0 24px;
    }

    h1, h2, h3, h4 {
      color: var(--text);
      margin: 1.6em 0 0.6em;
      line-height: 1.25;
    }

    h1 { font-size: 32px; letter-spacing: -0.01em; }
    h2 { font-size: 24px; }
    h3 { font-size: 18px; color: #161c2d; }
    p { margin: 0.6em 0 1em; }

    .lead {
      color: var(--muted);
      margin-top: -8px;
    }

    pre, code, kbd, samp {
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas,
                   "Liberation Mono", "Courier New", monospace;
    }

    code {
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 1px 5px;
    }

    pre {
      background: var(--code-bg);
      color: var(--code-fg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 14px 16px;
      margin: 14px 0 18px;
      white-space: pre-wrap;
      word-break: break-word;
      overflow: auto;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      margin: 14px 0 20px;
      font-variant-numeric: tabular-nums;
    }

    th, td {
      border: 1px solid var(--border);
      padding: 8px 10px;
      vertical-align: top;
    }

    thead th {
      background: var(--table-head);
      text-align: left;
    }

    .caption {
      font-size: 12px;
      color: var(--muted);
      margin-top: -8px;
      margin-bottom: 12px;
    }

    @media print {
      .doc { margin: 0; }
      h2 { page-break-before: auto; }
      h2, h3 { page-break-after: avoid; }
      pre, table { page-break-inside: avoid; }
    }
  </style>
</head>
<body>
  <main class="doc">
    <h1>Titre Principal</h1>
    <p class="lead">Description courte du document.</p>

    <h2 id="section-1">Section Principale</h2>
    <p>Texte avec du code inline : <code>Settings.Secure.ANDROID_ID</code></p>

    <pre><code>// Exemple de bloc de code
fun generateDeviceId(): String {
    val androidId = Settings.Secure.getString(
        contentResolver, 
        Settings.Secure.ANDROID_ID
    )
    return androidId ?: "unknown"
}</code></pre>

    <p class="caption"><strong>Note :</strong> Exemple d'utilisation des APIs Android.</p>
  </main>
</body>
</html>
```

### Template Optimisé Google Docs (styles inline)
```html
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Document Technique - Google Docs Optimized</title>
</head>
<body style="background: #ffffff; color: #101114; font-family: 'Inter', 'Roboto', 'Segoe UI', Arial, Helvetica, sans-serif; font-size: 15px; line-height: 1.55; margin: 0; padding: 0;">
  
  <main style="max-width: 940px; margin: 48px auto 96px; padding: 0 24px;">
    
    <!-- Titre H1 -->
    <h1 style="color: #101114; margin: 1.6em 0 0.6em; line-height: 1.25; font-size: 32px; letter-spacing: -0.01em;">Titre Principal</h1>
    
    <!-- Lead paragraph -->
    <p style="color: #5f6673; margin-top: -8px; margin: 0.6em 0 1em;">Description courte du document.</p>
    
    <!-- Titre H2 -->
    <h2 style="color: #101114; margin: 1.6em 0 0.6em; line-height: 1.25; font-size: 24px;">Section Principale</h2>
    
    <!-- Paragraphe avec code inline -->
    <p style="margin: 0.6em 0 1em;">Texte avec du code inline : <span style="font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, 'Liberation Mono', 'Courier New', monospace; background: #f6f8fa; border: 1px solid #DADCE0; border-radius: 6px; padding: 1px 5px;">Settings.Secure.ANDROID_ID</span></p>
    
    <!-- Bloc de code -->
    <pre style="background: #f6f8fa; color: #0b1021; border: 1px solid #DADCE0; border-radius: 8px; padding: 14px 16px; margin: 14px 0 18px; white-space: pre-wrap; word-break: break-word; overflow: auto; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, 'Liberation Mono', 'Courier New', monospace;"><code>// Exemple de bloc de code
fun generateDeviceId(): String {
    val androidId = Settings.Secure.getString(
        contentResolver, 
        Settings.Secure.ANDROID_ID
    )
    return androidId ?: "unknown"
}</code></pre>
    
    <!-- Caption -->
    <p style="font-size: 12px; color: #5f6673; margin-top: -8px; margin-bottom: 12px;"><strong>Note :</strong> Exemple d'utilisation des APIs Android.</p>
    
  </main>
</body>
</html>
```

## 11. Workflow de Génération
### Étapes de Création
1. **Analyse du contenu** : identifier les éléments (titres, code, arbres, etc.)
2. **Choix du template** : standard (CSS) ou optimisé Google Docs (inline)
3. **Application de la charte** : respecter H1: 32px, H2: 24px, H3: 18px, texte: 15px
4. **Optimisation des blocs de code** : utiliser `<pre><code>` avec `pre-wrap`
5. **Test de compatibilité** : vérifier le rendu dans Google Docs

### Décision Template
- **Template Standard** : pour exports vers Word, LibreOffice, ou affichage web
- **Template Optimisé** : pour import direct dans Google Docs via copy-paste

### Processus de Validation
1. **Génération HTML** avec template approprié
2. **Test local** : ouvrir dans navigateur
3. **Test Google Docs** : copy-paste du contenu HTML
4. **Vérification formatage** : titres, code, structures
5. **Ajustements** si nécessaire

## 12. Checklist de Validation
### Avant Export
- [ ] Vérifier la hiérarchie des titres (H1: 32px, H2: 24px, H3: 18px)
- [ ] Contrôler la police du corps de texte (Inter/Roboto 15px)
- [ ] Valider les blocs de code (`<pre><code>` avec police monospace)
- [ ] Vérifier les espacements et marges
- [ ] S'assurer de l'utilisation des styles inline pour Google Docs

### Après Import Google Docs
- [ ] Contrôler la préservation des styles
- [ ] Vérifier la lisibilité des blocs de code
- [ ] Valider la structure hiérarchique
- [ ] Tester l'édition dans Google Docs
- [ ] Vérifier que les blocs de code conservent leur formatage
- [ ] Contrôler la numérotation des sections

### Tests de Compatibilité
- [ ] Test copy-paste depuis navigateur vers Google Docs
- [ ] Vérification des polices (Inter/Roboto, monospace)
- [ ] Contrôle des couleurs et contrastes
- [ ] Test d'édition dans Google Docs après import
- [ ] Validation de l'export PDF depuis Google Docs

**Document généré :** 2025-08-25  
**Version :** 2.0  
**Compatibilité :** Google Docs, Microsoft Word, LibreOffice
