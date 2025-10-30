#!/usr/bin/env python3
"""
Markdown to HTML Converter
Converts Markdown files to HTML following the HTML_EXPORT_CHARTER.md specifications.

Usage:
    python scripts/docs/md_to_html.py <input_path> [options]

Examples:
    # Convert single file
    python scripts/docs/md_to_html.py docs/IMAGE_SEQUENCER_SPECIFICATION.md
    
    # Convert all .md files in a directory
    python scripts/docs/md_to_html.py docs/ --recursive
    
    # Generate only Google Docs version
    python scripts/docs/md_to_html.py docs/ --template gdocs
"""

import argparse
import html
import os
import re
import sys
from pathlib import Path
from typing import List, Tuple

try:
    import mistune
except ImportError:
    print("Error: mistune library not found.")
    print("Install it with: pip install mistune>=3.0.0")
    sys.exit(1)

try:
    from PIL import Image, ImageDraw, ImageFont
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False
    print("Warning: Pillow not found. ASCII art will not be converted to images.")
    print("Install it with: pip install Pillow>=10.0.0")


# Design tokens from HTML_EXPORT_CHARTER.md
CSS_VARIABLES = """
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
"""

# Standard template CSS
STANDARD_CSS = CSS_VARIABLES + """
html, body {
  background: var(--bg);
  color: var(--text);
  font-family: "Inter", "Roboto", "Segoe UI", Arial, Helvetica, sans-serif;
  font-size: 12px;
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
  font-size: 24px; 
  letter-spacing: -0.01em; 
}

h2 { 
  font-size: 18px; 
  font-weight: 700;
}

h3 { 
  font-size: 14px; 
  font-weight: 700;
  color: #161c2d; 
}

h4 {
  font-size: 12px;
  font-weight: 700;
}

p { 
  margin: 0.6em 0 1em; 
}

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

pre code {
  background: none;
  border: none;
  border-radius: 0;
  padding: 0;
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
  text-align: left;
}

thead th {
  background: var(--table-head);
  text-align: left;
}

ul, ol {
  margin: 0.6em 0 1em;
  padding-left: 2em;
}

li {
  margin: 0.3em 0;
}

blockquote {
  border-left: 4px solid var(--border);
  margin: 1em 0;
  padding-left: 1em;
  color: var(--muted);
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
"""

# Inline styles for Google Docs compatibility
INLINE_STYLES = {
    'body': 'background: #ffffff; color: #101114; font-family: "Inter", "Roboto", "Segoe UI", Arial, Helvetica, sans-serif; font-size: 12px; line-height: 1.55; margin: 0; padding: 0;',
    'main': 'max-width: 940px; margin: 48px auto 96px; padding: 0 24px;',
    'h1': 'color: #101114; margin: 1.6em 0 0.6em; line-height: 1.25; font-size: 24px; letter-spacing: -0.01em; font-weight: 700;',
    'h2': 'color: #101114; margin: 1.6em 0 0.6em; line-height: 1.25; font-size: 18px; font-weight: 700;',
    'h3': 'color: #161c2d; margin: 1.6em 0 0.6em; line-height: 1.25; font-size: 14px; font-weight: 700;',
    'h4': 'color: #101114; margin: 1.6em 0 0.6em; line-height: 1.25; font-size: 12px; font-weight: 700;',
    'p': 'margin: 0.6em 0 1em; font-size: 12px;',
    'code': 'font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace; background: #f6f8fa; border: 1px solid #DADCE0; border-radius: 6px; padding: 1px 5px; font-size: 11px;',
    'pre': 'background: #f6f8fa; color: #0b1021; border: 1px solid #DADCE0; border-radius: 8px; padding: 14px 16px; margin: 14px 0 18px; white-space: pre-wrap; word-break: break-word; overflow: auto; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace; font-size: 11px;',
    'table': 'width: 100%; border-collapse: collapse; margin: 14px 0 20px; font-variant-numeric: tabular-nums; font-size: 11px;',
    'th': 'border: 1px solid #DADCE0; padding: 8px 10px; vertical-align: top; background: #f3f4f6; text-align: left; font-size: 11px;',
    'td': 'border: 1px solid #DADCE0; padding: 8px 10px; vertical-align: top; text-align: left; font-size: 11px;',
    'ul': 'margin: 0.6em 0 1em; padding-left: 2em; font-size: 12px;',
    'ol': 'margin: 0.6em 0 1em; padding-left: 2em; font-size: 12px;',
    'li': 'margin: 0.3em 0; font-size: 12px;',
    'blockquote': 'border-left: 4px solid #DADCE0; margin: 1em 0; padding-left: 1em; color: #5f6673; font-size: 12px;',
}


def format_title(filename: str) -> str:
    """Convert filename to readable title."""
    # Remove extension
    title = Path(filename).stem
    # Replace underscores with spaces
    title = title.replace('_', ' ')
    # Capitalize each word
    title = ' '.join(word.capitalize() for word in title.split())
    return title


def apply_inline_styles(html_content: str) -> str:
    """Apply inline styles to HTML elements for Google Docs compatibility."""
    
    # Apply styles to common tags
    for tag, style in INLINE_STYLES.items():
        if tag in ['body', 'main']:
            continue
        # Match opening tags without existing style attribute
        pattern = f'<{tag}(?!\\s+style=)([^>]*)>'
        replacement = f'<{tag} style="{style}"\\1>'
        html_content = re.sub(pattern, replacement, html_content)
    
    # Special handling for code inside pre (remove inline styles from code within pre)
    html_content = re.sub(
        r'<pre style="([^"]*)"[^>]*>\s*<code style="[^"]*"([^>]*)>',
        r'<pre style="\1"><code>',
        html_content
    )
    
    return html_content


def create_html_document(title: str, content: str, template: str = 'standard') -> str:
    """Create complete HTML document."""
    
    if template == 'gdocs':
        # Google Docs optimized template with inline styles
        body_style = INLINE_STYLES['body']
        main_style = INLINE_STYLES['main']
        
        # Apply inline styles to content
        content = apply_inline_styles(content)
        
        return f"""<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{html.escape(title)}</title>
</head>
<body style="{body_style}">
  <main style="{main_style}">
{content}
  </main>
</body>
</html>
"""
    else:
        # Standard template with CSS
        return f"""<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{html.escape(title)}</title>
  <style>
{STANDARD_CSS}
  </style>
</head>
<body>
  <main class="doc">
{content}
  </main>
</body>
</html>
"""


def convert_markdown_to_html(md_content: str) -> str:
    """Convert Markdown content to HTML using mistune."""
    
    # Create markdown parser with all extensions
    markdown = mistune.create_markdown(
        escape=False,  # We'll handle escaping ourselves
        plugins=['strikethrough', 'footnotes', 'table', 'url', 'task_lists']
    )
    
    # Convert to HTML
    html_content = markdown(md_content)
    
    # Remove horizontal rules (<hr> tags) that come from --- in Markdown
    # These are often used as section separators in Markdown but not wanted in final HTML
    html_content = re.sub(r'<hr\s*/?>\s*', '', html_content)
    
    return html_content


def process_markdown_file(input_path: Path, template: str = 'both') -> List[Tuple[Path, bool]]:
    """Process a single Markdown file and generate HTML output(s)."""
    
    results = []
    
    try:
        # Read Markdown content
        with open(input_path, 'r', encoding='utf-8') as f:
            md_content = f.read()
        
        # Convert to HTML
        html_body = convert_markdown_to_html(md_content)
        
        # Generate title from filename
        title = format_title(input_path.name)
        
        # Determine output paths
        output_base = input_path.parent / input_path.stem
        
        # Generate standard version
        if template in ['standard', 'both']:
            output_path = output_base.with_suffix('.html')
            html_doc = create_html_document(title, html_body, 'standard')
            
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(html_doc)
            
            results.append((output_path, True))
            print(f"✓ Generated: {output_path}")
        
        # Generate Google Docs version
        if template in ['gdocs', 'both']:
            output_path = Path(str(output_base) + '_gdocs.html')
            html_doc = create_html_document(title, html_body, 'gdocs')
            
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(html_doc)
            
            results.append((output_path, True))
            print(f"✓ Generated: {output_path}")
        
    except Exception as e:
        print(f"✗ Error processing {input_path}: {e}")
        results.append((input_path, False))
    
    return results


def find_markdown_files(directory: Path, recursive: bool = False, exclude: List[str] = None) -> List[Path]:
    """Find all Markdown files in a directory."""
    
    exclude = exclude or []
    md_files = []
    
    if recursive:
        pattern = '**/*.md'
    else:
        pattern = '*.md'
    
    for md_file in directory.glob(pattern):
        if md_file.name not in exclude:
            md_files.append(md_file)
    
    return sorted(md_files)


def main():
    """Main entry point."""
    
    parser = argparse.ArgumentParser(
        description='Convert Markdown files to HTML following HTML_EXPORT_CHARTER.md specifications.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Convert single file (generates both versions)
  %(prog)s docs/IMAGE_SEQUENCER_SPECIFICATION.md
  
  # Convert all .md files in directory
  %(prog)s docs/ --recursive
  
  # Generate only Google Docs version
  %(prog)s docs/ --template gdocs
  
  # Exclude specific files
  %(prog)s docs/ --recursive --exclude README.md CHANGELOG.md
        """
    )
    
    parser.add_argument(
        'input',
        type=str,
        help='Input Markdown file or directory'
    )
    
    parser.add_argument(
        '--template',
        choices=['standard', 'gdocs', 'both'],
        default='standard',
        help='HTML template to generate (default: standard)'
    )
    
    parser.add_argument(
        '--recursive',
        action='store_true',
        help='Process directories recursively'
    )
    
    parser.add_argument(
        '--exclude',
        nargs='+',
        default=[],
        help='Filenames to exclude from conversion'
    )
    
    args = parser.parse_args()
    
    # Validate input path
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Path not found: {input_path}")
        sys.exit(1)
    
    # Process files
    all_results = []
    
    if input_path.is_file():
        if input_path.suffix.lower() != '.md':
            print(f"Error: Input file must have .md extension")
            sys.exit(1)
        
        print(f"Converting: {input_path}")
        results = process_markdown_file(input_path, args.template)
        all_results.extend(results)
        
    elif input_path.is_dir():
        md_files = find_markdown_files(input_path, args.recursive, args.exclude)
        
        if not md_files:
            print(f"No Markdown files found in: {input_path}")
            sys.exit(0)
        
        print(f"Found {len(md_files)} Markdown file(s)")
        print()
        
        for md_file in md_files:
            print(f"Converting: {md_file}")
            results = process_markdown_file(md_file, args.template)
            all_results.extend(results)
            print()
    
    # Summary
    successful = sum(1 for _, success in all_results if success)
    failed = len(all_results) - successful
    
    print("=" * 60)
    print(f"Conversion complete: {successful} successful, {failed} failed")
    
    if failed > 0:
        sys.exit(1)


if __name__ == '__main__':
    main()
