#!/usr/bin/env python3
"""
Agent 2: Code Duplication Detector
Détecte les duplications de code entre standalone et VST, 
et les fonctionnalités non-connectées ou redondantes.
"""

import os
import re
import hashlib
from pathlib import Path
from typing import List, Dict, Set, Tuple
from collections import defaultdict

class DuplicationDetectorAgent:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.vst_dir = self.project_root / "vst" / "source"
        self.src_dir = self.project_root / "src"
        self.issues = []
        
    def add_issue(self, category: str, locations: List[Tuple[str, int]], message: str, severity: str = "WARNING"):
        """Add a duplication issue"""
        self.issues.append({
            'severity': severity,
            'category': category,
            'locations': locations,
            'message': message
        })
    
    def normalize_code(self, code: str) -> str:
        """Normalize code for comparison (remove whitespace, comments)"""
        # Remove C++ comments
        code = re.sub(r'//.*$', '', code, flags=re.MULTILINE)
        code = re.sub(r'/\*.*?\*/', '', code, flags=re.DOTALL)
        
        # Remove whitespace
        code = re.sub(r'\s+', ' ', code)
        
        return code.strip()
    
    def get_code_hash(self, code: str) -> str:
        """Get hash of normalized code"""
        normalized = self.normalize_code(code)
        return hashlib.md5(normalized.encode()).hexdigest()
    
    def extract_functions(self, file_path: Path) -> Dict[str, Tuple[str, int]]:
        """Extract functions from C/C++ file"""
        functions = {}
        
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                # Simple function pattern (not perfect but sufficient)
                func_pattern = r'^(\w+[\w\s\*]+)\s+(\w+)\s*\([^)]*\)\s*{'
                
                current_func = None
                current_func_start = 0
                brace_count = 0
                func_lines = []
                
                for i, line in enumerate(lines, 1):
                    # Check for function start
                    match = re.match(func_pattern, line)
                    if match and not current_func:
                        current_func = match.group(2)
                        current_func_start = i
                        func_lines = [line]
                        brace_count = line.count('{') - line.count('}')
                    elif current_func:
                        func_lines.append(line)
                        brace_count += line.count('{') - line.count('}')
                        
                        if brace_count == 0:
                            # Function complete
                            func_code = '\n'.join(func_lines)
                            if len(func_code) > 100:  # Ignore tiny functions
                                functions[current_func] = (func_code, current_func_start)
                            current_func = None
                            func_lines = []
        except Exception as e:
            print(f"  Warning: Could not parse {file_path}: {e}")
        
        return functions
    
    def find_duplicate_functions(self):
        """Find functions that are duplicated between standalone and VST"""
        print("🔍 Checking for duplicate functions between standalone and VST...")
        
        # Build hash -> (file, function_name, line) mapping
        hash_map = defaultdict(list)
        
        # Scan VST source files
        for cpp_file in self.vst_dir.glob("*.cpp"):
            if cpp_file.name == "global_stubs.c":
                continue  # Stubs are expected
            
            functions = self.extract_functions(cpp_file)
            for func_name, (code, line_num) in functions.items():
                code_hash = self.get_code_hash(code)
                hash_map[code_hash].append((str(cpp_file.relative_to(self.project_root)), func_name, line_num))
        
        # Scan standalone source files (recursive)
        for cpp_file in self.src_dir.rglob("*.c"):
            functions = self.extract_functions(cpp_file)
            for func_name, (code, line_num) in functions.items():
                code_hash = self.get_code_hash(code)
                hash_map[code_hash].append((str(cpp_file.relative_to(self.project_root)), func_name, line_num))
        
        for cpp_file in self.src_dir.rglob("*.cpp"):
            functions = self.extract_functions(cpp_file)
            for func_name, (code, line_num) in functions.items():
                code_hash = self.get_code_hash(code)
                hash_map[code_hash].append((str(cpp_file.relative_to(self.project_root)), func_name, line_num))
        
        # Find duplicates (same hash, different files)
        for code_hash, locations in hash_map.items():
            if len(locations) > 1:
                # Check if it's cross-boundary duplication (VST vs standalone)
                vst_locs = [loc for loc in locations if 'vst/' in loc[0]]
                src_locs = [loc for loc in locations if 'src/' in loc[0]]
                
                if vst_locs and src_locs:
                    self.add_issue(
                        'Code Duplication - Cross-boundary',
                        [(loc[0], loc[2]) for loc in locations],
                        f"Function '{locations[0][1]}' duplicated between VST and standalone. Consider refactoring into shared library.",
                        'WARNING'
                    )
    
    def find_unused_stubs(self):
        """Find stub functions that are never called"""
        print("🔍 Checking for unused stub functions...")
        
        stub_file = self.vst_dir / "global_stubs.c"
        if not stub_file.exists():
            return
        
        # Extract stub function names
        with open(stub_file, 'r', encoding='utf-8') as f:
            content = f.read()
            stub_functions = re.findall(r'^\w+[\w\s\*]+\s+(\w+)\s*\([^)]*\)\s*{', content, re.MULTILINE)
        
        # Check if each stub is called in VST code
        vst_code = ""
        for cpp_file in self.vst_dir.glob("*.cpp"):
            if cpp_file.name != "global_stubs.c":
                with open(cpp_file, 'r', encoding='utf-8') as f:
                    vst_code += f.read()
        
        for stub_func in stub_functions:
            # Look for function calls
            if not re.search(rf'\b{stub_func}\s*\(', vst_code):
                self.add_issue(
                    'Unused Code - Stubs',
                    [(str(stub_file.relative_to(self.project_root)), 0)],
                    f"Stub function '{stub_func}' is never called in VST code. Consider removing.",
                    'INFO'
                )
    
    def find_duplicate_config_handling(self):
        """Find duplicate configuration handling code"""
        print("🔍 Checking for duplicate configuration handling...")
        
        config_patterns = [
            'udp_port',
            'udp_address',
            'sensor_dpi',
            'log_level',
        ]
        
        # Map config key -> files that handle it
        config_handlers = defaultdict(list)
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                for pattern in config_patterns:
                    matches = [(i+1, line) for i, line in enumerate(lines) if pattern in line.lower()]
                    if matches:
                        config_handlers[pattern].extend([
                            (str(cpp_file.relative_to(self.project_root)), line_num, line.strip())
                            for line_num, line in matches
                        ])
        
        # Check for excessive handling (same config in multiple files)
        for config_key, handlers in config_handlers.items():
            unique_files = set(h[0] for h in handlers)
            if len(unique_files) > 2:  # Config should be in max 2 places (definition + usage)
                self.add_issue(
                    'Code Duplication - Config Handling',
                    [(h[0], h[1]) for h in handlers],
                    f"Configuration '{config_key}' is handled in {len(unique_files)} different files. Consider centralizing.",
                    'WARNING'
                )
    
    def find_duplicate_buffer_initialization(self):
        """Find duplicate buffer initialization patterns"""
        print("🔍 Checking for duplicate buffer initialization...")
        
        buffer_init_patterns = [
            r'audio_image_buffers_init',
            r'initDoubleBuffer',
            r'DoubleBuffer.*init',
        ]
        
        init_locations = []
        
        for cpp_file in self.vst_dir.glob("*.cpp"):
            with open(cpp_file, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = content.split('\n')
                
                for i, line in enumerate(lines, 1):
                    for pattern in buffer_init_patterns:
                        if re.search(pattern, line) and not line.strip().startswith('//'):
                            init_locations.append((str(cpp_file.relative_to(self.project_root)), i, line.strip()))
        
        # Group by pattern
        if len(init_locations) > 3:  # Reasonable threshold
            self.add_issue(
                'Code Duplication - Buffer Init',
                [(loc[0], loc[1]) for loc in init_locations],
                f"Buffer initialization code appears in {len(init_locations)} places. Consider using a single initialization function.",
                'INFO'
            )
    
    def generate_report(self) -> str:
        """Generate formatted report"""
        report = []
        report.append("=" * 80)
        report.append("🔁 CODE DUPLICATION REPORT")
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
                    report.append(f"  {issue['message']}")
                    report.append(f"  Locations:")
                    for file, line in issue['locations']:
                        report.append(f"    - {file}:{line}")
                    report.append("")
        
        if not errors and not warnings and not info:
            report.append("✅ No code duplication issues found!")
        
        report.append("=" * 80)
        return "\n".join(report)
    
    def run(self) -> str:
        """Run all duplication checks"""
        print("\n🔁 Starting Code Duplication Detector Agent...")
        print("=" * 80)
        
        self.find_duplicate_functions()
        self.find_unused_stubs()
        self.find_duplicate_config_handling()
        self.find_duplicate_buffer_initialization()
        
        return self.generate_report()

if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    agent = DuplicationDetectorAgent(project_root)
    report = agent.run()
    print(report)
    
    # Save report
    output_file = Path(project_root) / "scripts" / "code_review" / "reports" / "duplication_report.txt"
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(report)
    print(f"\n📝 Report saved to: {output_file}")
