#!/usr/bin/env python3
"""
Agent 4: UI Consistency Checker
Vérifie l'homogénéité et la cohérence de l'interface utilisateur JUCE.
"""

import os
import re
from pathlib import Path
from typing import List, Dict, Tuple, Set
from collections import defaultdict

class UIConsistencyAgent:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.vst_dir = self.project_root / "vst" / "source"
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
    
    def check_color_consistency(self):
        """Check for consistent color usage across UI components"""
        print("🔍 Checking UI color consistency...")
        
        # Extract all color definitions
        colors = defaultdict(list)
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                # Look for juce::Colour definitions
                color_patterns = [
                    r'juce::Colour\s*\(\s*0x([0-9a-fA-F]{8})\s*\)',
                    r'juce::Colours::(\w+)',
                    r'\.withAlpha\s*\(\s*([\d.]+)f?\s*\)',
                ]
                
                for i, line in enumerate(lines, 1):
                    for pattern in color_patterns:
                        matches = re.finditer(pattern, line)
                        for match in matches:
                            color_value = match.group(1) if match.lastindex else match.group(0)
                            colors[color_value].append((
                                str(cpp_file.relative_to(self.project_root)),
                                i,
                                line.strip()
                            ))
        
        # Check for hardcoded colors used only once (should be constants)
        singleton_colors = {color: locs for color, locs in colors.items() if len(locs) == 1}
        
        if len(singleton_colors) > 5:  # Many singleton colors suggest inconsistency
            self.add_issue(
                'UI - Color Consistency',
                'Multiple files',
                0,
                f"Found {len(singleton_colors)} unique colors used only once. Consider creating a color palette/theme.",
                'WARNING'
            )
    
    def check_font_consistency(self):
        """Check for consistent font usage"""
        print("🔍 Checking font consistency...")
        
        fonts = defaultdict(list)
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                for i, line in enumerate(lines, 1):
                    # Look for font definitions
                    font_match = re.search(r'juce::Font\s*\(\s*([\d.]+)f?\s*\)', line)
                    if font_match:
                        font_size = font_match.group(1)
                        fonts[font_size].append((
                            str(cpp_file.relative_to(self.project_root)),
                            i,
                            line.strip()
                        ))
        
        # Report if too many different font sizes (suggests inconsistency)
        if len(fonts) > 5:
            self.add_issue(
                'UI - Font Consistency',
                'Multiple files',
                0,
                f"Found {len(fonts)} different font sizes. Consider using a limited set of predefined sizes.",
                'INFO'
            )
    
    def check_layout_consistency(self):
        """Check for consistent layout patterns"""
        print("🔍 Checking layout consistency...")
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                # Check for resized() implementations
                resized_pattern = r'void\s+\w+::resized\s*\(\s*\)'
                
                for i, line in enumerate(lines, 1):
                    if re.search(resized_pattern, line):
                        # Check if using proper layout patterns (FlexBox, Grid, etc.)
                        resized_start = i
                        resized_code = []
                        brace_count = 0
                        
                        for j in range(i-1, min(i+50, len(lines))):
                            resized_code.append(lines[j])
                            brace_count += lines[j].count('{') - lines[j].count('}')
                            if brace_count == 0 and j > i:
                                break
                        
                        resized_text = '\n'.join(resized_code)
                        
                        # Check for hardcoded pixel values (anti-pattern)
                        pixel_coords = re.findall(r'setBounds\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)', resized_text)
                        
                        if len(pixel_coords) > 3:
                            self.add_issue(
                                'UI - Layout Hardcoding',
                                str(cpp_file.relative_to(self.project_root)),
                                resized_start,
                                f"Found {len(pixel_coords)} hardcoded setBounds() calls. Consider using FlexBox or proportional layouts.",
                                'WARNING'
                            )
                        
                        # Check if using getWidth()/getHeight() for responsive design
                        if 'getWidth()' not in resized_text and 'getHeight()' not in resized_text:
                            if len(pixel_coords) > 0:
                                self.add_issue(
                                    'UI - Non-responsive Layout',
                                    str(cpp_file.relative_to(self.project_root)),
                                    resized_start,
                                    "Layout uses fixed coordinates without referencing component size. Consider responsive design.",
                                    'WARNING'
                                )
    
    def check_component_naming(self):
        """Check for consistent component naming"""
        print("🔍 Checking component naming consistency...")
        
        component_names = defaultdict(list)
        
        for header_file in self.vst_dir.glob("*.h"):
            with open(header_file, 'r', encoding='utf-8') as f:
                content = f.read()
                
                # Find member variables (likely UI components)
                member_patterns = [
                    r'juce::(\w+)\s+(\w+);',
                    r'std::unique_ptr<juce::(\w+)>\s+(\w+);',
                ]
                
                for pattern in member_patterns:
                    matches = re.finditer(pattern, content)
                    for match in matches:
                        component_type = match.group(1)
                        component_name = match.group(2)
                        component_names[component_type].append((
                            str(header_file.relative_to(self.project_root)),
                            component_name
                        ))
        
        # Check for inconsistent naming conventions
        for comp_type, instances in component_names.items():
            if len(instances) > 1:
                # Check naming patterns
                suffixed = [name for _, name in instances if name.endswith(comp_type)]
                prefixed = [name for _, name in instances if name.startswith(comp_type.lower())]
                plain = [name for _, name in instances if not (name.endswith(comp_type) or name.startswith(comp_type.lower()))]
                
                # If mixed naming styles
                if suffixed and plain:
                    self.add_issue(
                        'UI - Naming Inconsistency',
                        'Multiple files',
                        0,
                        f"Inconsistent naming for {comp_type}: some use suffix '{comp_type}', others don't. Choose one convention.",
                        'INFO'
                    )
    
    def check_event_handling(self):
        """Check for consistent event handling patterns"""
        print("🔍 Checking event handling consistency...")
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                # Check for listener implementations
                listener_methods = [
                    'buttonClicked',
                    'sliderValueChanged',
                    'comboBoxChanged',
                    'timerCallback',
                ]
                
                for method in listener_methods:
                    pattern = rf'void\s+\w+::{method}\s*\('
                    
                    for i, line in enumerate(lines, 1):
                        if re.search(pattern, line):
                            # Check if method has proper error handling
                            method_start = i
                            method_code = []
                            brace_count = 0
                            
                            for j in range(i-1, min(i+30, len(lines))):
                                method_code.append(lines[j])
                                brace_count += lines[j].count('{') - lines[j].count('}')
                                if brace_count == 0 and j > i:
                                    break
                            
                            method_text = '\n'.join(method_code)
                            
                            # Check for nullptr checks when accessing pointers
                            if '->' in method_text and 'if' not in method_text[:100]:
                                self.add_issue(
                                    'UI - Event Safety',
                                    str(cpp_file.relative_to(self.project_root)),
                                    method_start,
                                    f"Event handler '{method}' accesses pointers without nullptr checks.",
                                    'WARNING'
                                )
    
    def check_parameter_bindings(self):
        """Check for proper APVTS parameter bindings in UI"""
        print("🔍 Checking parameter bindings...")
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                
                # Check if UI components are bound to APVTS
                has_apvts = 'getAPVTS()' in content or 'apvts' in content
                has_sliders = 'Slider' in content or 'Button' in content
                has_attachments = 'SliderAttachment' in content or 'ButtonAttachment' in content
                
                if has_sliders and has_apvts and not has_attachments:
                    self.add_issue(
                        'UI - Parameter Binding',
                        str(cpp_file.relative_to(self.project_root)),
                        0,
                        "UI has controls and APVTS access but no Attachments. Controls may not be properly bound to parameters.",
                        'WARNING'
                    )
    
    def check_thread_safety(self):
        """Check for thread-safe UI updates"""
        print("🔍 Checking UI thread safety...")
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                # Look for UI updates in non-UI contexts
                for i, line in enumerate(lines, 1):
                    # Check for repaint() or component updates in processBlock
                    if 'processBlock' in line:
                        # Check subsequent lines for UI calls
                        for j in range(i, min(i+50, len(lines))):
                            if any(ui_call in lines[j] for ui_call in ['repaint()', 'setVisible(', 'setBounds(']):
                                self.add_issue(
                                    'UI - Thread Safety',
                                    str(cpp_file.relative_to(self.project_root)),
                                    j+1,
                                    "UI update in processBlock (audio thread). Use MessageManager::callAsync() instead.",
                                    'ERROR'
                                )
    
    def generate_report(self) -> str:
        """Generate formatted report"""
        report = []
        report.append("=" * 80)
        report.append("🎨 UI CONSISTENCY REPORT")
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
                    report.append(f"  🎨 {issue['message']}")
                    report.append("")
        
        if not errors and not warnings and not info:
            report.append("✅ No UI consistency issues found!")
        
        report.append("=" * 80)
        return "\n".join(report)
    
    def run(self) -> str:
        """Run all UI consistency checks"""
        print("\n🎨 Starting UI Consistency Checker Agent...")
        print("=" * 80)
        
        self.check_color_consistency()
        self.check_font_consistency()
        self.check_layout_consistency()
        self.check_component_naming()
        self.check_event_handling()
        self.check_parameter_bindings()
        self.check_thread_safety()
        
        return self.generate_report()

if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    agent = UIConsistencyAgent(project_root)
    report = agent.run()
    print(report)
    
    # Save report
    output_file = Path(project_root) / "scripts" / "code_review" / "reports" / "ui_consistency_report.txt"
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(report)
    print(f"\n📝 Report saved to: {output_file}")
