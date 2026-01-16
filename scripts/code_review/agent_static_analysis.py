#!/usr/bin/env python3
"""
Agent 5: Static Analysis Integration
Utilise clang-tidy et cppcheck pour une analyse professionnelle AST-based.
"""

import os
import re
import subprocess
import json
from pathlib import Path
from typing import List, Dict, Tuple
from collections import defaultdict

class StaticAnalysisAgent:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.vst_dir = self.project_root / "vst" / "source"
        self.issues = []
        self.clang_tidy_available = False
        self.cppcheck_available = False
        
        # Check tool availability
        self.check_tools()
        
    def check_tools(self):
        """Check if static analysis tools are available"""
        try:
            subprocess.run(['clang-tidy', '--version'], 
                         capture_output=True, check=True, timeout=5)
            self.clang_tidy_available = True
            print("✓ clang-tidy detected")
        except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired):
            print("⚠️  clang-tidy not found. Install with: brew install llvm")
            
        try:
            subprocess.run(['cppcheck', '--version'], 
                         capture_output=True, check=True, timeout=5)
            self.cppcheck_available = True
            print("✓ cppcheck detected")
        except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired):
            print("⚠️  cppcheck not found. Install with: brew install cppcheck")
    
    def add_issue(self, category: str, file: str, line: int, message: str, 
                  severity: str = "WARNING", checker: str = ""):
        """Add an issue to the report"""
        self.issues.append({
            'severity': severity,
            'category': category,
            'file': file,
            'line': line,
            'message': message,
            'checker': checker
        })
    
    def run_clang_tidy(self):
        """Run clang-tidy on VST source files"""
        if not self.clang_tidy_available:
            print("⏭️  Skipping clang-tidy (not installed)")
            return
        
        print("🔍 Running clang-tidy analysis...")
        
        cpp_files = list(self.vst_dir.glob("*.cpp"))
        
        for cpp_file in cpp_files:
            try:
                # Run clang-tidy with JSON export
                cmd = [
                    'clang-tidy',
                    str(cpp_file),
                    '--config-file=' + str(self.project_root / 'scripts' / 'code_review' / '.clang-tidy'),
                    '--',
                    '-std=c++17',
                    '-I' + str(self.project_root / 'vst' / 'source'),
                    '-I' + str(self.project_root / 'src'),
                ]
                
                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=30,
                    cwd=str(self.project_root)
                )
                
                # Parse clang-tidy output
                self.parse_clang_tidy_output(result.stdout, cpp_file)
                
            except subprocess.TimeoutExpired:
                print(f"  ⚠️  Timeout analyzing {cpp_file.name}")
            except Exception as e:
                print(f"  ⚠️  Error analyzing {cpp_file.name}: {e}")
    
    def parse_clang_tidy_output(self, output: str, source_file: Path):
        """Parse clang-tidy text output"""
        # Pattern: filename:line:col: warning: message [checker-name]
        pattern = r'([^:]+):(\d+):(\d+):\s+(warning|error):\s+(.+?)\s+\[([^\]]+)\]'
        
        for match in re.finditer(pattern, output):
            file_path, line, col, severity, message, checker = match.groups()
            
            # Categorize by checker type
            category = self.categorize_clang_check(checker)
            
            # Map severity
            severity_mapped = 'ERROR' if severity == 'error' else 'WARNING'
            
            self.add_issue(
                category,
                str(source_file.relative_to(self.project_root)),
                int(line),
                f"[{checker}] {message}",
                severity_mapped,
                'clang-tidy'
            )
    
    def categorize_clang_check(self, checker: str) -> str:
        """Categorize clang-tidy check"""
        if 'bugprone' in checker:
            return 'Static Analysis - Bugs'
        elif 'performance' in checker:
            return 'Static Analysis - Performance'
        elif 'concurrency' in checker:
            return 'Static Analysis - Threading'
        elif 'modernize' in checker:
            return 'Static Analysis - Modernization'
        elif 'readability' in checker:
            return 'Static Analysis - Readability'
        elif 'cppcoreguidelines' in checker:
            return 'Static Analysis - Core Guidelines'
        else:
            return 'Static Analysis - Other'
    
    def run_cppcheck(self):
        """Run cppcheck on VST source files"""
        if not self.cppcheck_available:
            print("⏭️  Skipping cppcheck (not installed)")
            return
        
        print("🔍 Running cppcheck analysis...")
        
        try:
            cmd = [
                'cppcheck',
                '--enable=all',
                '--suppress=missingIncludeSystem',
                '--suppress=unusedFunction',
                '--inline-suppr',
                '--template={file}:{line}:{severity}:{id}:{message}',
                '--quiet',
                str(self.vst_dir)
            ]
            
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=60
            )
            
            # Parse both stdout and stderr (cppcheck uses stderr for results)
            self.parse_cppcheck_output(result.stderr)
            
        except subprocess.TimeoutExpired:
            print("  ⚠️  Cppcheck timeout")
        except Exception as e:
            print(f"  ⚠️  Cppcheck error: {e}")
    
    def parse_cppcheck_output(self, output: str):
        """Parse cppcheck output"""
        # Pattern: filename:line:severity:id:message
        pattern = r'([^:]+):(\d+):([^:]+):([^:]+):(.+)'
        
        for line in output.split('\n'):
            match = re.match(pattern, line)
            if match:
                file_path, line_num, severity, check_id, message = match.groups()
                
                # Map severity
                severity_mapped = 'ERROR' if severity == 'error' else 'WARNING'
                if severity in ['style', 'performance', 'portability']:
                    severity_mapped = 'INFO'
                
                # Categorize
                category = self.categorize_cppcheck(check_id, severity)
                
                try:
                    rel_path = Path(file_path).relative_to(self.project_root)
                except ValueError:
                    rel_path = file_path
                
                self.add_issue(
                    category,
                    str(rel_path),
                    int(line_num),
                    f"[{check_id}] {message}",
                    severity_mapped,
                    'cppcheck'
                )
    
    def categorize_cppcheck(self, check_id: str, severity: str) -> str:
        """Categorize cppcheck issue"""
        if 'leak' in check_id.lower() or 'memory' in check_id.lower():
            return 'Static Analysis - Memory'
        elif 'null' in check_id.lower() or 'pointer' in check_id.lower():
            return 'Static Analysis - Null Pointer'
        elif 'uninit' in check_id.lower():
            return 'Static Analysis - Uninitialized'
        elif 'performance' in severity:
            return 'Static Analysis - Performance'
        elif 'concurrency' in check_id.lower() or 'thread' in check_id.lower():
            return 'Static Analysis - Threading'
        else:
            return 'Static Analysis - Other'
    
    def generate_report(self) -> str:
        """Generate formatted report"""
        report = []
        report.append("=" * 80)
        report.append("🔬 STATIC ANALYSIS REPORT (clang-tidy + cppcheck)")
        report.append("=" * 80)
        report.append("")
        
        if not self.clang_tidy_available and not self.cppcheck_available:
            report.append("❌ No static analysis tools available!")
            report.append("")
            report.append("To install:")
            report.append("  macOS: brew install llvm cppcheck")
            report.append("  Linux: apt-get install clang-tidy cppcheck")
            report.append("")
            report.append("=" * 80)
            return "\n".join(report)
        
        # Count by severity
        errors = [i for i in self.issues if i['severity'] == 'ERROR']
        warnings = [i for i in self.issues if i['severity'] == 'WARNING']
        info = [i for i in self.issues if i['severity'] == 'INFO']
        
        report.append(f"📊 Summary: {len(errors)} errors, {len(warnings)} warnings, {len(info)} info")
        report.append(f"Tools: {'clang-tidy' if self.clang_tidy_available else ''} "
                     f"{'cppcheck' if self.cppcheck_available else ''}")
        report.append("")
        
        # Group by tool
        by_tool = defaultdict(list)
        for issue in self.issues:
            by_tool[issue['checker']].append(issue)
        
        for tool_name, tool_issues in by_tool.items():
            report.append(f"▶️  {tool_name.upper()} ({len(tool_issues)} issues)")
            report.append("-" * 80)
            
            # Group by category
            by_category = defaultdict(list)
            for issue in tool_issues:
                by_category[issue['category']].append(issue)
            
            for category, cat_issues in sorted(by_category.items()):
                report.append(f"\n  [{category}] ({len(cat_issues)} issues)")
                for issue in cat_issues[:10]:  # Limit to 10 per category
                    report.append(f"    {issue['severity']}: {issue['file']}:{issue['line']}")
                    report.append(f"    {issue['message']}")
                if len(cat_issues) > 10:
                    report.append(f"    ... and {len(cat_issues) - 10} more")
            report.append("")
        
        if not errors and not warnings and not info:
            report.append("✅ No issues found by static analysis!")
        
        report.append("=" * 80)
        return "\n".join(report)
    
    def run(self) -> str:
        """Run all static analysis checks"""
        print("\n🔬 Starting Static Analysis Agent...")
        print("=" * 80)
        
        self.run_clang_tidy()
        self.run_cppcheck()
        
        return self.generate_report()

if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    agent = StaticAnalysisAgent(project_root)
    report = agent.run()
    print(report)
    
    # Save report
    output_file = Path(project_root) / "scripts" / "code_review" / "reports" / "static_analysis_report.txt"
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(report)
    print(f"\n📝 Report saved to: {output_file}")
