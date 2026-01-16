#!/usr/bin/env python3
"""
Agent 3: AI Vibe Coding Bias Detector
Détecte les patterns typiques du code généré par IA qui peuvent indiquer
des problèmes de cohérence ou de qualité.
"""

import os
import re
from pathlib import Path
from typing import List, Dict, Tuple
from collections import defaultdict

class AIBiasDetectorAgent:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.vst_dir = self.project_root / "vst" / "source"
        self.src_dir = self.project_root / "src"
        self.issues = []
        
    def add_issue(self, category: str, file: str, line: int, message: str, severity: str = "WARNING"):
        """Add an issue to the report"""
        self.issues.append({
            'severity': severity,
            'category': category,
            'file': file,
            'line': line,
            'message': message
        })
    
    def check_excessive_comments(self):
        """Check for over-commented code (typical AI pattern)"""
        print("🔍 Checking for excessive comments (AI bias)...")
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                lines = f.readlines()
                
                comment_count = 0
                code_count = 0
                
                for i, line in enumerate(lines, 1):
                    stripped = line.strip()
                    if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                        comment_count += 1
                        
                        # Check for obvious AI-generated comments
                        ai_patterns = [
                            r'(?i)this (function|method) (is|does)',
                            r'(?i)TODO:?\s*(add|implement|fix)',
                            r'(?i)FIXME',
                            r'(?i)Note:',
                            r'(?i)Important:',
                            r'(?i)Example:',
                        ]
                        
                        for pattern in ai_patterns:
                            if re.search(pattern, line):
                                self.add_issue(
                                    'AI Bias - Generic Comments',
                                    str(cpp_file.relative_to(self.project_root)),
                                    i,
                                    f"Generic AI-style comment: {stripped[:80]}",
                                    'INFO'
                                )
                                break
                    elif stripped and not stripped.startswith('#'):
                        code_count += 1
                
                # Check comment-to-code ratio
                if code_count > 0:
                    ratio = comment_count / code_count
                    if ratio > 0.5:  # More than 50% comments
                        self.add_issue(
                            'AI Bias - Over-commented',
                            str(cpp_file.relative_to(self.project_root)),
                            0,
                            f"Excessive comments detected (ratio: {ratio:.2f}). Consider cleaning up obvious/redundant comments.",
                            'INFO'
                        )
    
    def check_placeholder_code(self):
        """Check for placeholder implementations (incomplete AI code)"""
        print("🔍 Checking for placeholder/incomplete code...")
        
        placeholder_patterns = [
            (r'// TODO', 'TODO comment'),
            (r'// FIXME', 'FIXME comment'),
            (r'// XXX', 'XXX comment'),
            (r'throw std::runtime_error\("Not implemented"\)', 'Not implemented exception'),
            (r'return\s+0\s*;\s*//.*not\s+implemented', 'Unimplemented return'),
            (r'juce::ignoreUnused', 'Unused parameter (may indicate incomplete implementation)'),
        ]
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                for i, line in enumerate(lines, 1):
                    for pattern, desc in placeholder_patterns:
                        if re.search(pattern, line, re.IGNORECASE):
                            self.add_issue(
                                'AI Bias - Placeholder Code',
                                str(cpp_file.relative_to(self.project_root)),
                                i,
                                f"{desc}: {line.strip()[:80]}",
                                'WARNING'
                            )
    
    def check_inconsistent_naming(self):
        """Check for inconsistent naming conventions (AI mixing styles)"""
        print("🔍 Checking for inconsistent naming conventions...")
        
        naming_styles = {
            'camelCase': defaultdict(int),
            'PascalCase': defaultdict(int),
            'snake_case': defaultdict(int),
            'UPPER_CASE': defaultdict(int),
        }
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                
                # Extract variable/function names (simplified)
                identifiers = re.findall(r'\b([a-z_][a-zA-Z0-9_]*)\s*[=\(]', content)
                
                for identifier in identifiers:
                    if re.match(r'^[a-z]+[A-Z]', identifier):
                        naming_styles['camelCase'][str(cpp_file.relative_to(self.project_root))] += 1
                    elif re.match(r'^[A-Z][a-z]+[A-Z]', identifier):
                        naming_styles['PascalCase'][str(cpp_file.relative_to(self.project_root))] += 1
                    elif '_' in identifier and identifier.lower() == identifier:
                        naming_styles['snake_case'][str(cpp_file.relative_to(self.project_root))] += 1
                    elif identifier.upper() == identifier and len(identifier) > 2:
                        naming_styles['UPPER_CASE'][str(cpp_file.relative_to(self.project_root))] += 1
        
        # Check for files mixing styles excessively
        for file in set().union(*[set(files.keys()) for files in naming_styles.values()]):
            styles_used = [style for style, files in naming_styles.items() if file in files and files[file] > 5]
            
            if len(styles_used) > 2:  # More than 2 naming styles
                self.add_issue(
                    'AI Bias - Inconsistent Naming',
                    file,
                    0,
                    f"File uses {len(styles_used)} different naming styles: {', '.join(styles_used)}",
                    'WARNING'
                )
    
    def check_dead_code(self):
        """Check for commented-out code blocks (AI leaving alternatives)"""
        print("🔍 Checking for commented-out code blocks...")
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                lines = f.readlines()
                
                commented_code_start = None
                commented_lines = 0
                
                for i, line in enumerate(lines, 1):
                    stripped = line.strip()
                    
                    # Check if line is commented code (has code-like patterns)
                    if stripped.startswith('//'):
                        uncommented = stripped[2:].strip()
                        # Look for code patterns
                        if any(pattern in uncommented for pattern in ['{', '}', '(', ')', ';', '=']):
                            if commented_code_start is None:
                                commented_code_start = i
                            commented_lines += 1
                        else:
                            if commented_lines >= 3:  # At least 3 lines of commented code
                                self.add_issue(
                                    'AI Bias - Dead Code',
                                    str(cpp_file.relative_to(self.project_root)),
                                    commented_code_start,
                                    f"Block of {commented_lines} lines of commented-out code. Remove or uncomment.",
                                    'WARNING'
                                )
                            commented_code_start = None
                            commented_lines = 0
                    else:
                        if commented_lines >= 3:
                            self.add_issue(
                                'AI Bias - Dead Code',
                                str(cpp_file.relative_to(self.project_root)),
                                commented_code_start,
                                f"Block of {commented_lines} lines of commented-out code. Remove or uncomment.",
                                'WARNING'
                            )
                        commented_code_start = None
                        commented_lines = 0
    
    def check_magic_numbers(self):
        """Check for magic numbers (AI sometimes hardcodes values)"""
        print("🔍 Checking for magic numbers...")
        
        # Common magic numbers to ignore
        acceptable_numbers = {0, 1, 2, -1, 100, 1000}
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                lines = f.readlines()
                
                for i, line in enumerate(lines, 1):
                    # Skip comments and string literals
                    if '//' in line:
                        line = line[:line.index('//')]
                    
                    # Find numeric literals
                    numbers = re.findall(r'\b(\d+\.?\d*)\b', line)
                    
                    for num_str in numbers:
                        try:
                            num = float(num_str)
                            # Check if it's a magic number (not in acceptable list, not obvious)
                            if num not in acceptable_numbers and num > 10 and '=' in line:
                                # Don't report constants (often uppercase variable names before =)
                                if not re.search(r'[A-Z_]{2,}\s*=\s*' + re.escape(num_str), line):
                                    if not re.search(r'const\s+\w+\s+\w+\s*=\s*' + re.escape(num_str), line):
                                        self.add_issue(
                                            'AI Bias - Magic Number',
                                            str(cpp_file.relative_to(self.project_root)),
                                            i,
                                            f"Magic number {num_str} found. Consider using named constant: {line.strip()[:60]}",
                                            'INFO'
                                        )
                        except ValueError:
                            pass
    
    def check_copy_paste_patterns(self):
        """Check for repeated similar code blocks (copy-paste AI generation)"""
        print("🔍 Checking for copy-paste patterns...")
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                
                # Look for repeated structural patterns
                patterns = [
                    (r'if\s*\([^)]+\)\s*{\s*[^}]+\s*}\s*if\s*\([^)]+\)\s*{\s*[^}]+\s*}\s*if\s*\([^)]+\)\s*{', 
                     'Multiple similar if blocks (consider loop or switch)'),
                    (r'(\w+)\s*=\s*[^;]+;\s*(\1)\s*=\s*[^;]+;\s*(\1)\s*=', 
                     'Multiple assignments to same variable (check logic)'),
                ]
                
                for pattern, desc in patterns:
                    matches = list(re.finditer(pattern, content))
                    if len(matches) > 2:
                        for match in matches[:3]:  # Report first 3
                            line_num = content[:match.start()].count('\n') + 1
                            self.add_issue(
                                'AI Bias - Copy-Paste Pattern',
                                str(cpp_file.relative_to(self.project_root)),
                                line_num,
                                desc,
                                'WARNING'
                            )
    
    def generate_report(self) -> str:
        """Generate formatted report"""
        report = []
        report.append("=" * 80)
        report.append("🤖 AI VIBE CODING BIAS REPORT")
        report.append("=" * 80)
        report.append("")
        
        # Count by severity
        errors = [i for i in self.issues if i['severity'] == 'ERROR']
        warnings = [i for i in self.issues if i['severity'] == 'WARNING']
        info = [i for i in self.issues if i['severity'] == 'INFO']
        
        report.append(f"📊 Summary: {len(errors)} errors, {len(warnings)} warnings, {len(info)} info")
        report.append("")
        
        for severity, issues_list, icon in [
            ('ERROR', errors, '❌'),
            ('WARNING', warnings, '⚠️'),
            ('INFO', info, 'ℹ️')
        ]:
            if issues_list:
                report.append(f"{icon} {severity}S:")
                report.append("-" * 80)
                for issue in issues_list:
                    report.append(f"  [{issue['category']}]")
                    report.append(f"  File: {issue['file']}:{issue['line']}")
                    report.append(f"  💡 {issue['message']}")
                    report.append("")
        
        if not errors and not warnings and not info:
            report.append("✅ No AI bias issues found!")
        
        report.append("=" * 80)
        return "\n".join(report)
    
    def run(self) -> str:
        """Run all AI bias checks"""
        print("\n🤖 Starting AI Vibe Coding Bias Detector Agent...")
        print("=" * 80)
        
        self.check_excessive_comments()
        self.check_placeholder_code()
        self.check_inconsistent_naming()
        self.check_dead_code()
        self.check_magic_numbers()
        self.check_copy_paste_patterns()
        
        return self.generate_report()

if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    agent = AIBiasDetectorAgent(project_root)
    report = agent.run()
    print(report)
    
    # Save report
    output_file = Path(project_root) / "scripts" / "code_review" / "reports" / "ai_bias_report.txt"
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(report)
    print(f"\n📝 Report saved to: {output_file}")
