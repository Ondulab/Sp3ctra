#!/usr/bin/env python3
"""
Agent 1: Architecture Review Agent
Vérifie la cohérence architecturale du VST, la séparation des responsabilités,
et le respect du plan de migration.
"""

import os
import re
from pathlib import Path
from typing import List, Dict, Tuple

class ArchitectureReviewAgent:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.vst_dir = self.project_root / "vst" / "source"
        self.src_dir = self.project_root / "src"
        self.issues = []
        self.warnings = []
        self.info = []
        
    def add_issue(self, category: str, file: str, line: int, message: str, severity: str = "ERROR"):
        """Add an issue to the report"""
        self.issues.append({
            'severity': severity,
            'category': category,
            'file': file,
            'line': line,
            'message': message
        })
    
    def check_global_variables_in_vst(self):
        """Check for problematic global variable usage in VST code"""
        print("🔍 Checking for global variables in VST code...")
        
        global_patterns = [
            (r'\bextern\s+\w+\s+g_\w+', 'Global variable access'),
            (r'\bstatic\s+\w+\s+\w+\s*=', 'Static variable (potential state leak)'),
        ]
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                for i, line in enumerate(lines, 1):
                    for pattern, desc in global_patterns:
                        if re.search(pattern, line) and not line.strip().startswith('//'):
                            # Exception: testTonePhase member variable is OK
                            if 'testTonePhase' in line:
                                continue
                            self.add_issue(
                                'Architecture - Global State',
                                str(cpp_file.relative_to(self.project_root)),
                                i,
                                f"{desc}: {line.strip()}",
                                'WARNING'
                            )
    
    def check_rtaudio_usage_in_vst(self):
        """Verify that VST code does NOT use RtAudio (only JUCE)"""
        print("🔍 Checking for improper RtAudio usage in VST...")
        
        rtaudio_patterns = [
            r'#include.*rtaudio',
            r'\bRtAudio\b',
            r'audio_rtaudio',
        ]
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                for i, line in enumerate(lines, 1):
                    for pattern in rtaudio_patterns:
                        if re.search(pattern, line, re.IGNORECASE) and not line.strip().startswith('//'):
                            self.add_issue(
                                'Architecture - Audio Layer',
                                str(cpp_file.relative_to(self.project_root)),
                                i,
                                f"RtAudio should NOT be used in VST (DAW handles audio): {line.strip()}",
                                'ERROR'
                            )
    
    def check_responsibility_separation(self):
        """Check that PluginProcessor, Sp3ctraCore, and UI are properly separated"""
        print("🔍 Checking responsibility separation...")
        
        processor_file = self.vst_dir / "PluginProcessor.cpp"
        core_file = self.vst_dir / "Sp3ctraCore.cpp"
        editor_file = self.vst_dir / "PluginEditor.cpp"
        
        # PluginProcessor should NOT handle low-level UDP/socket operations
        with open(processor_file, 'r', encoding='utf-8') as f:
            content = f.read()
            lines = content.split('\n')
            
            bad_patterns = [
                (r'\bsocket\s*\(', 'Direct socket() call'),
                (r'\bbind\s*\(', 'Direct bind() call'),
                (r'\brecvfrom\s*\(', 'Direct recvfrom() call'),
            ]
            
            for i, line in enumerate(lines, 1):
                for pattern, desc in bad_patterns:
                    if re.search(pattern, line) and not line.strip().startswith('//'):
                        self.add_issue(
                            'Architecture - Separation of Concerns',
                            str(processor_file.relative_to(self.project_root)),
                            i,
                            f"PluginProcessor should delegate to Sp3ctraCore: {desc} found",
                            'ERROR'
                        )
        
        # Sp3ctraCore should NOT have JUCE GUI code
        with open(core_file, 'r', encoding='utf-8') as f:
            content = f.read()
            lines = content.split('\n')
            
            if 'juce::Component' in content or 'juce::Graphics' in content:
                self.add_issue(
                    'Architecture - Separation of Concerns',
                    str(core_file.relative_to(self.project_root)),
                    0,
                    "Sp3ctraCore should not contain GUI code (use juce::Logger for logging only)",
                    'ERROR'
                )
    
    def check_rt_audio_safety(self):
        """Check for RT-unsafe operations in audio callback"""
        print("🔍 Checking RT-audio safety in processBlock...")
        
        processor_file = self.vst_dir / "PluginProcessor.cpp"
        
        with open(processor_file, 'r', encoding='utf-8') as f:
            content = f.read()
            
            # Find processBlock function
            processblock_match = re.search(
                r'void\s+Sp3ctraAudioProcessor::processBlock\s*\([^)]+\)\s*{(.*?)\n}',
                content,
                re.DOTALL
            )
            
            if processblock_match:
                processblock_code = processblock_match.group(1)
                lines = processblock_code.split('\n')
                
                # RT-unsafe patterns
                unsafe_patterns = [
                    (r'\bmalloc\s*\(', 'malloc() call (RT-unsafe)'),
                    (r'\bnew\s+\w+', 'new operator (RT-unsafe)'),
                    (r'\bdelete\s+', 'delete operator (RT-unsafe)'),
                    (r'\bstd::mutex', 'Mutex lock (RT-unsafe if blocking)'),
                    (r'\bprintf\s*\(', 'printf() call (RT-unsafe)'),
                    (r'\bjuce::Logger', 'Logger call (RT-unsafe, use in non-RT thread only)'),
                    (r'\bstd::cout', 'std::cout (RT-unsafe)'),
                ]
                
                for i, line in enumerate(lines, 1):
                    for pattern, desc in unsafe_patterns:
                        if re.search(pattern, line) and not line.strip().startswith('//'):
                            self.add_issue(
                                'RT-Audio Safety',
                                str(processor_file.relative_to(self.project_root)),
                                i,
                                f"{desc} in processBlock: {line.strip()}",
                                'ERROR'
                            )
    
    def check_instance_isolation(self):
        """Check that multiple VST instances won't interfere with each other"""
        print("🔍 Checking instance isolation (multi-instance support)...")
        
        # Check for singleton patterns or global state managers
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                
                # Look for singleton pattern
                if re.search(r'static\s+\w+\*\s+instance\s*=', content):
                    self.add_issue(
                        'Architecture - Instance Isolation',
                        str(cpp_file.relative_to(self.project_root)),
                        0,
                        "Singleton pattern detected - may cause issues with multiple VST instances",
                        'WARNING'
                    )
    
    def check_config_management(self):
        """Check that configuration is managed through APVTS (not .ini files)"""
        print("🔍 Checking configuration management...")
        
        processor_file = self.vst_dir / "PluginProcessor.cpp"
        
        with open(processor_file, 'r', encoding='utf-8') as f:
            content = f.read()
            lines = content.split('\n')
            
            # Check for .ini file loading
            ini_patterns = [
                r'\.ini',
                r'config_load',
                r'parseConfigFile',
            ]
            
            for i, line in enumerate(lines, 1):
                for pattern in ini_patterns:
                    if re.search(pattern, line, re.IGNORECASE) and not line.strip().startswith('//'):
                        if 'NO .ini loading' not in line:
                            self.add_issue(
                                'Architecture - Configuration',
                                str(processor_file.relative_to(self.project_root)),
                                i,
                                f"VST should use APVTS for config, not .ini files: {line.strip()}",
                                'WARNING'
                            )
    
    def generate_report(self) -> str:
        """Generate a formatted report"""
        report = []
        report.append("=" * 80)
        report.append("🏗️  ARCHITECTURE REVIEW REPORT")
        report.append("=" * 80)
        report.append("")
        
        # Count by severity
        errors = [i for i in self.issues if i['severity'] == 'ERROR']
        warnings = [i for i in self.issues if i['severity'] == 'WARNING']
        
        report.append(f"📊 Summary: {len(errors)} errors, {len(warnings)} warnings")
        report.append("")
        
        if errors:
            report.append("❌ ERRORS:")
            report.append("-" * 80)
            for issue in errors:
                report.append(f"  [{issue['category']}]")
                report.append(f"  File: {issue['file']}:{issue['line']}")
                report.append(f"  ⚠️  {issue['message']}")
                report.append("")
        
        if warnings:
            report.append("⚠️  WARNINGS:")
            report.append("-" * 80)
            for issue in warnings:
                report.append(f"  [{issue['category']}]")
                report.append(f"  File: {issue['file']}:{issue['line']}")
                report.append(f"  ⚠️  {issue['message']}")
                report.append("")
        
        if not errors and not warnings:
            report.append("✅ No architectural issues found!")
        
        report.append("=" * 80)
        return "\n".join(report)
    
    def run(self) -> str:
        """Run all architecture checks"""
        print("\n🏗️  Starting Architecture Review Agent...")
        print("=" * 80)
        
        self.check_global_variables_in_vst()
        self.check_rtaudio_usage_in_vst()
        self.check_responsibility_separation()
        self.check_rt_audio_safety()
        self.check_instance_isolation()
        self.check_config_management()
        
        return self.generate_report()

if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    agent = ArchitectureReviewAgent(project_root)
    report = agent.run()
    print(report)
    
    # Save report
    output_file = Path(project_root) / "scripts" / "code_review" / "reports" / "architecture_report.txt"
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(report)
    print(f"\n📝 Report saved to: {output_file}")
