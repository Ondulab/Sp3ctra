#!/usr/bin/env python3
"""
Agent 6: Code Metrics Analyzer
Calcule des métriques quantitatives de qualité de code.
"""

import os
import re
import subprocess
from pathlib import Path
from typing import List, Dict, Tuple
from collections import defaultdict
import json

class CodeMetricsAgent:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.vst_dir = self.project_root / "vst" / "source"
        self.metrics = {}
        self.issues = []
        self.lizard_available = False
        
        # Check tool availability
        self.check_tools()
        
    def check_tools(self):
        """Check if metrics tools are available"""
        try:
            subprocess.run(['lizard', '--version'], 
                         capture_output=True, check=True, timeout=5)
            self.lizard_available = True
            print("✓ lizard detected")
        except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired):
            print("⚠️  lizard not found. Install with: pip3 install lizard")
    
    def add_issue(self, category: str, file: str, line: int, message: str, severity: str = "WARNING"):
        """Add an issue to the report"""
        self.issues.append({
            'severity': severity,
            'category': category,
            'file': file,
            'line': line,
            'message': message
        })
    
    def run_lizard_analysis(self):
        """Run lizard for cyclomatic complexity and other metrics"""
        if not self.lizard_available:
            print("⏭️  Skipping lizard (not installed)")
            return
        
        print("🔍 Calculating code metrics with lizard...")
        
        try:
            cmd = [
                'lizard',
                str(self.vst_dir),
                '-l', 'cpp',
                '--json'
            ]
            
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30
            )
            
            if result.returncode == 0:
                self.parse_lizard_output(result.stdout)
            else:
                print(f"  ⚠️  Lizard returned non-zero: {result.returncode}")
                
        except subprocess.TimeoutExpired:
            print("  ⚠️  Lizard timeout")
        except Exception as e:
            print(f"  ⚠️  Lizard error: {e}")
    
    def parse_lizard_output(self, json_output: str):
        """Parse lizard JSON output"""
        try:
            data = json.loads(json_output)
            
            self.metrics['total_functions'] = len(data.get('function_list', []))
            self.metrics['total_nloc'] = data.get('nloc', 0)
            self.metrics['avg_nloc'] = data.get('average_nloc', 0)
            self.metrics['avg_ccn'] = data.get('average_CCN', 0)
            self.metrics['avg_token'] = data.get('average_token', 0)
            
            # Analyze individual functions
            complex_functions = []
            long_functions = []
            high_param_functions = []
            
            for func in data.get('function_list', []):
                ccn = func.get('cyclomatic_complexity', 0)
                nloc = func.get('nloc', 0)
                params = func.get('parameter_count', 0)
                name = func.get('name', 'unknown')
                file = func.get('filename', '')
                line = func.get('start_line', 0)
                
                # Check thresholds
                if ccn > 15:  # High complexity
                    complex_functions.append({
                        'name': name,
                        'file': file,
                        'line': line,
                        'ccn': ccn
                    })
                    
                if nloc > 100:  # Long function
                    long_functions.append({
                        'name': name,
                        'file': file,
                        'line': line,
                        'nloc': nloc
                    })
                    
                if params > 6:  # Too many parameters
                    high_param_functions.append({
                        'name': name,
                        'file': file,
                        'line': line,
                        'params': params
                    })
            
            self.metrics['complex_functions'] = complex_functions
            self.metrics['long_functions'] = long_functions
            self.metrics['high_param_functions'] = high_param_functions
            
            # Generate issues
            for func in complex_functions:
                try:
                    rel_path = Path(func['file']).relative_to(self.project_root)
                except ValueError:
                    rel_path = func['file']
                    
                self.add_issue(
                    'Metrics - Complexity',
                    str(rel_path),
                    func['line'],
                    f"Function '{func['name']}' has cyclomatic complexity {func['ccn']} (threshold: 15). Consider refactoring.",
                    'WARNING'
                )
            
            for func in long_functions:
                try:
                    rel_path = Path(func['file']).relative_to(self.project_root)
                except ValueError:
                    rel_path = func['file']
                    
                self.add_issue(
                    'Metrics - Size',
                    str(rel_path),
                    func['line'],
                    f"Function '{func['name']}' has {func['nloc']} lines (threshold: 100). Consider splitting.",
                    'INFO'
                )
            
            for func in high_param_functions:
                try:
                    rel_path = Path(func['file']).relative_to(self.project_root)
                except ValueError:
                    rel_path = func['file']
                    
                self.add_issue(
                    'Metrics - Parameters',
                    str(rel_path),
                    func['line'],
                    f"Function '{func['name']}' has {func['params']} parameters (threshold: 6). Consider using a struct.",
                    'INFO'
                )
                
        except json.JSONDecodeError as e:
            print(f"  ⚠️  Failed to parse lizard JSON: {e}")
    
    def calculate_file_metrics(self):
        """Calculate basic file-level metrics manually"""
        print("🔍 Calculating file metrics...")
        
        file_metrics = []
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8', errors='ignore') as f:
                lines = f.readlines()
                
                total_lines = len(lines)
                blank_lines = sum(1 for line in lines if not line.strip())
                comment_lines = sum(1 for line in lines if line.strip().startswith('//') or line.strip().startswith('/*') or line.strip().startswith('*'))
                code_lines = total_lines - blank_lines - comment_lines
                
                # Count functions (simple heuristic)
                function_count = len(re.findall(r'^\w+.*\s+\w+\s*\([^)]*\)\s*{', ''.join(lines), re.MULTILINE))
                
                file_metrics.append({
                    'file': cpp_file.name,
                    'total_lines': total_lines,
                    'code_lines': code_lines,
                    'comment_lines': comment_lines,
                    'blank_lines': blank_lines,
                    'functions': function_count,
                    'comment_ratio': comment_lines / total_lines if total_lines > 0 else 0
                })
        
        self.metrics['file_metrics'] = file_metrics
        
        # Summary
        self.metrics['total_files'] = len(file_metrics)
        self.metrics['total_code_lines'] = sum(f['code_lines'] for f in file_metrics)
        self.metrics['total_comment_lines'] = sum(f['comment_lines'] for f in file_metrics)
        self.metrics['avg_comment_ratio'] = sum(f['comment_ratio'] for f in file_metrics) / len(file_metrics) if file_metrics else 0
    
    def generate_report(self) -> str:
        """Generate formatted metrics report"""
        report = []
        report.append("=" * 80)
        report.append("📊 CODE METRICS REPORT")
        report.append("=" * 80)
        report.append("")
        
        if not self.lizard_available:
            report.append("⚠️  Limited metrics (install lizard for full analysis)")
            report.append("")
        
        # Overview
        report.append("📈 OVERVIEW")
        report.append("-" * 80)
        report.append(f"Total Files: {self.metrics.get('total_files', 0)}")
        report.append(f"Total Code Lines: {self.metrics.get('total_code_lines', 0)}")
        report.append(f"Total Comment Lines: {self.metrics.get('total_comment_lines', 0)}")
        report.append(f"Average Comment Ratio: {self.metrics.get('avg_comment_ratio', 0):.1%}")
        
        if self.lizard_available:
            report.append(f"Total Functions: {self.metrics.get('total_functions', 0)}")
            report.append(f"Average Cyclomatic Complexity: {self.metrics.get('avg_ccn', 0):.1f}")
            report.append(f"Average Function Length: {self.metrics.get('avg_nloc', 0):.0f} lines")
        report.append("")
        
        # Complexity Issues
        if self.metrics.get('complex_functions'):
            report.append("⚠️  HIGH COMPLEXITY FUNCTIONS")
            report.append("-" * 80)
            for func in self.metrics['complex_functions'][:10]:
                report.append(f"  {func['name']} (CCN: {func['ccn']}) - {Path(func['file']).name}:{func['line']}")
            if len(self.metrics['complex_functions']) > 10:
                report.append(f"  ... and {len(self.metrics['complex_functions']) - 10} more")
            report.append("")
        
        # Long Functions
        if self.metrics.get('long_functions'):
            report.append("📏 LONG FUNCTIONS")
            report.append("-" * 80)
            for func in self.metrics['long_functions'][:5]:
                report.append(f"  {func['name']} ({func['nloc']} lines) - {Path(func['file']).name}:{func['line']}")
            if len(self.metrics['long_functions']) > 5:
                report.append(f"  ... and {len(self.metrics['long_functions']) - 5} more")
            report.append("")
        
        # File Breakdown
        if self.metrics.get('file_metrics'):
            report.append("📁 FILE BREAKDOWN")
            report.append("-" * 80)
            file_metrics = sorted(self.metrics['file_metrics'], key=lambda x: x['code_lines'], reverse=True)
            for fm in file_metrics[:10]:
                report.append(f"  {fm['file']:30s} {fm['code_lines']:4d} code, {fm['comment_lines']:3d} comments, {fm['functions']:2d} functions")
            report.append("")
        
        # Issues Summary
        errors = [i for i in self.issues if i['severity'] == 'ERROR']
        warnings = [i for i in self.issues if i['severity'] == 'WARNING']
        info = [i for i in self.issues if i['severity'] == 'INFO']
        
        report.append(f"📊 Issues: {len(errors)} errors, {len(warnings)} warnings, {len(info)} info")
        report.append("")
        
        # Quality Assessment
        report.append("✨ QUALITY ASSESSMENT")
        report.append("-" * 80)
        
        # Scoring
        score = 100
        if self.metrics.get('avg_ccn', 0) > 10:
            score -= 20
            report.append("  ⚠️  Average complexity above recommended threshold")
        if len(self.metrics.get('complex_functions', [])) > 5:
            score -= 15
            report.append(f"  ⚠️  {len(self.metrics['complex_functions'])} functions with high complexity")
        if self.metrics.get('avg_comment_ratio', 0) < 0.1:
            score -= 10
            report.append("  ⚠️  Low comment ratio (< 10%)")
        elif self.metrics.get('avg_comment_ratio', 0) > 0.5:
            score -= 5
            report.append("  ℹ️  High comment ratio (> 50%), may indicate over-documentation")
        
        if score >= 80:
            report.append(f"\n  ✅ Quality Score: {score}/100 - Excellent!")
        elif score >= 60:
            report.append(f"\n  👍 Quality Score: {score}/100 - Good")
        elif score >= 40:
            report.append(f"\n  ⚠️  Quality Score: {score}/100 - Needs Improvement")
        else:
            report.append(f"\n  ❌ Quality Score: {score}/100 - Poor")
        
        report.append("")
        report.append("=" * 80)
        return "\n".join(report)
    
    def run(self) -> str:
        """Run all metrics analysis"""
        print("\n📊 Starting Code Metrics Analyzer Agent...")
        print("=" * 80)
        
        self.calculate_file_metrics()
        self.run_lizard_analysis()
        
        return self.generate_report()

if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    agent = CodeMetricsAgent(project_root)
    report = agent.run()
    print(report)
    
    # Save report
    output_file = Path(project_root) / "scripts" / "code_review" / "reports" / "metrics_report.txt"
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(report)
    print(f"\n📝 Report saved to: {output_file}")
