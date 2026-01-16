#!/usr/bin/env python3
"""
Agent 7: LLM Semantic Analyzer
Utilise Claude (Anthropic) pour une analyse sémantique approfondie du code VST.
Détecte des patterns complexes impossibles avec regex/AST seuls.
"""

import os
import json
from pathlib import Path
from typing import List, Dict, Optional
import anthropic

class LLMSemanticAgent:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.vst_dir = self.project_root / "vst" / "source"
        self.issues = []
        self.recommendations = []
        self.api_key = os.getenv('ANTHROPIC_API_KEY')
        self.client = None
        
        # Check API availability
        if self.api_key:
            try:
                self.client = anthropic.Anthropic(api_key=self.api_key)
                print("✓ Claude API available")
            except Exception as e:
                print(f"⚠️  Claude API error: {e}")
        else:
            print("⚠️  ANTHROPIC_API_KEY not set. Set with: export ANTHROPIC_API_KEY='your-key'")
    
    def add_issue(self, category: str, file: str, message: str, severity: str = "WARNING"):
        """Add an issue to the report"""
        self.issues.append({
            'severity': severity,
            'category': category,
            'file': file,
            'message': message
        })
    
    def analyze_file_with_llm(self, file_path: Path) -> Optional[Dict]:
        """Analyze a single file with Claude"""
        if not self.client:
            return None
        
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                code = f.read()
            
            # Create prompt for Claude
            prompt = f"""You are a senior C++ audio developer specialized in real-time audio (RT-audio) and VST plugin development using JUCE framework.

Analyze this VST plugin code file for:
1. **RT-Audio Safety**: Identify any operations that are unsafe in audio callback (malloc/new, mutex locks, syscalls, logging, std::vector::push_back, std::string operations)
2. **Architecture Issues**: Separation of concerns, dependency injection, global state
3. **JUCE Best Practices**: Proper use of AudioProcessor, APVTS, UI thread safety
4. **Subtle Bugs**: Logic errors, edge cases, resource leaks
5. **Code Smells**: God objects, feature envy, primitive obsession

File: {file_path.name}
```cpp
{code}
```

Provide analysis in JSON format:
{{
  "rt_safety_issues": [
    {{"line": <number or null>, "issue": "<description>", "severity": "ERROR|WARNING"}}
  ],
  "architecture_issues": [
    {{"issue": "<description>", "severity": "WARNING|INFO"}}
  ],
  "bugs": [
    {{"line": <number or null>, "issue": "<description>", "severity": "ERROR|WARNING"}}
  ],
  "improvements": [
    {{"suggestion": "<description>"}}
  ]
}}

Be concise but specific. Only report actual issues, not theoretical concerns."""

            # Call Claude API
            message = self.client.messages.create(
                model="claude-3-sonnet-20240229",
                max_tokens=2000,
                temperature=0,
                messages=[{
                    "role": "user",
                    "content": prompt
                }]
            )
            
            # Parse response
            response_text = message.content[0].text
            
            # Try to extract JSON from response
            json_start = response_text.find('{')
            json_end = response_text.rfind('}') + 1
            
            if json_start >= 0 and json_end > json_start:
                json_str = response_text[json_start:json_end]
                analysis = json.loads(json_str)
                return analysis
            else:
                print(f"  ⚠️  Could not parse JSON from Claude response for {file_path.name}")
                return None
                
        except json.JSONDecodeError as e:
            print(f"  ⚠️  JSON parse error for {file_path.name}: {e}")
            return None
        except Exception as e:
            print(f"  ⚠️  Analysis error for {file_path.name}: {e}")
            return None
    
    def analyze_all_files(self):
        """Analyze all VST source files"""
        if not self.client:
            print("⏭️  Skipping LLM analysis (API key not configured)")
            return
        
        print("🔍 Analyzing files with Claude AI...")
        
        cpp_files = list(self.vst_dir.glob("*.cpp"))
        
        for i, cpp_file in enumerate(cpp_files, 1):
            print(f"  [{i}/{len(cpp_files)}] Analyzing {cpp_file.name}...")
            
            analysis = self.analyze_file_with_llm(cpp_file)
            
            if analysis:
                # Process RT safety issues
                for issue in analysis.get('rt_safety_issues', []):
                    self.add_issue(
                        'LLM - RT-Audio Safety',
                        str(cpp_file.relative_to(self.project_root)),
                        f"Line {issue.get('line', '?')}: {issue['issue']}",
                        issue.get('severity', 'WARNING')
                    )
                
                # Process architecture issues
                for issue in analysis.get('architecture_issues', []):
                    self.add_issue(
                        'LLM - Architecture',
                        str(cpp_file.relative_to(self.project_root)),
                        issue['issue'],
                        issue.get('severity', 'WARNING')
                    )
                
                # Process bugs
                for issue in analysis.get('bugs', []):
                    self.add_issue(
                        'LLM - Potential Bug',
                        str(cpp_file.relative_to(self.project_root)),
                        f"Line {issue.get('line', '?')}: {issue['issue']}",
                        issue.get('severity', 'WARNING')
                    )
                
                # Collect improvements
                for improvement in analysis.get('improvements', []):
                    self.recommendations.append({
                        'file': cpp_file.name,
                        'suggestion': improvement['suggestion']
                    })
    
    def generate_report(self) -> str:
        """Generate formatted report"""
        report = []
        report.append("=" * 80)
        report.append("🤖 LLM SEMANTIC ANALYSIS REPORT (Claude)")
        report.append("=" * 80)
        report.append("")
        
        if not self.client:
            report.append("❌ Claude API not configured!")
            report.append("")
            report.append("To enable LLM analysis:")
            report.append("1. Get API key from: https://console.anthropic.com/")
            report.append("2. Set environment variable:")
            report.append("   export ANTHROPIC_API_KEY='your-api-key-here'")
            report.append("3. Re-run the analysis")
            report.append("")
            report.append("=" * 80)
            return "\n".join(report)
        
        # Count by severity
        errors = [i for i in self.issues if i['severity'] == 'ERROR']
        warnings = [i for i in self.issues if i['severity'] == 'WARNING']
        info = [i for i in self.issues if i['severity'] == 'INFO']
        
        report.append(f"📊 Summary: {len(errors)} errors, {len(warnings)} warnings, {len(info)} info")
        report.append(f"🎯 Recommendations: {len(self.recommendations)}")
        report.append("")
        
        # Issues by category
        from collections import defaultdict
        by_category = defaultdict(list)
        for issue in self.issues:
            by_category[issue['category']].append(issue)
        
        for category, cat_issues in sorted(by_category.items()):
            report.append(f"▶️  {category} ({len(cat_issues)} issues)")
            report.append("-" * 80)
            
            for issue in cat_issues[:10]:  # Limit to 10 per category
                report.append(f"  {issue['severity']}: {issue['file']}")
                report.append(f"  {issue['message']}")
                report.append("")
            
            if len(cat_issues) > 10:
                report.append(f"  ... and {len(cat_issues) - 10} more")
                report.append("")
        
        # Recommendations
        if self.recommendations:
            report.append("💡 IMPROVEMENT RECOMMENDATIONS")
            report.append("=" * 80)
            
            by_file = defaultdict(list)
            for rec in self.recommendations:
                by_file[rec['file']].append(rec['suggestion'])
            
            for file, suggestions in by_file.items():
                report.append(f"\n{file}:")
                for i, suggestion in enumerate(suggestions, 1):
                    report.append(f"  {i}. {suggestion}")
            report.append("")
        
        if not errors and not warnings and not info:
            report.append("✅ No issues found by LLM analysis!")
        
        report.append("=" * 80)
        report.append("Note: LLM analysis provides semantic understanding beyond pattern matching.")
        report.append("Always review recommendations in context of your specific requirements.")
        report.append("=" * 80)
        return "\n".join(report)
    
    def run(self) -> str:
        """Run LLM semantic analysis"""
        print("\n🤖 Starting LLM Semantic Analyzer Agent (Claude)...")
        print("=" * 80)
        
        self.analyze_all_files()
        
        return self.generate_report()

if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    agent = LLMSemanticAgent(project_root)
    report = agent.run()
    print(report)
    
    # Save report
    output_file = Path(project_root) / "scripts" / "code_review" / "reports" / "llm_semantic_report.txt"
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(report)
    print(f"\n📝 Report saved to: {output_file}")
