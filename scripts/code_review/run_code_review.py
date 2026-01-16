#!/usr/bin/env python3
"""
Code Review Orchestrator
Exécute tous les agents de revue de code et génère un rapport consolidé.
"""

import os
import sys
from pathlib import Path
from datetime import datetime

# Add parent directory to path to import agents
sys.path.insert(0, str(Path(__file__).parent))

from agent_architecture import ArchitectureReviewAgent
from agent_duplication import DuplicationDetectorAgent
from agent_ai_bias import AIBiasDetectorAgent
from agent_ui_consistency import UIConsistencyAgent

class CodeReviewOrchestrator:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.reports_dir = self.project_root / "scripts" / "code_review" / "reports"
        self.reports_dir.mkdir(parents=True, exist_ok=True)
        
        self.agents = [
            ("Architecture Review", ArchitectureReviewAgent(project_root)),
            ("Code Duplication", DuplicationDetectorAgent(project_root)),
            ("AI Bias Detection", AIBiasDetectorAgent(project_root)),
            ("UI Consistency", UIConsistencyAgent(project_root)),
        ]
        
        self.individual_reports = []
        self.all_issues = {
            'ERROR': [],
            'WARNING': [],
            'INFO': []
        }
    
    def run_all_agents(self):
        """Execute all agents and collect reports"""
        print("\n" + "=" * 80)
        print("🔍 VST CODE REVIEW - ORCHESTRATOR")
        print("=" * 80)
        print(f"Project: {self.project_root.name}")
        print(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print("=" * 80)
        
        for agent_name, agent in self.agents:
            print(f"\n▶️  Running {agent_name}...")
            print("-" * 80)
            
            try:
                report = agent.run()
                self.individual_reports.append((agent_name, report))
                
                # Collect issues from this agent
                if hasattr(agent, 'issues'):
                    for issue in agent.issues:
                        severity = issue.get('severity', 'INFO')
                        self.all_issues[severity].append({
                            'agent': agent_name,
                            'issue': issue
                        })
                
                print(f"✅ {agent_name} completed")
            except Exception as e:
                print(f"❌ {agent_name} failed: {e}")
                import traceback
                traceback.print_exc()
        
        print("\n" + "=" * 80)
        print("✅ All agents completed!")
        print("=" * 80)
    
    def generate_consolidated_report(self) -> str:
        """Generate a consolidated report from all agents"""
        report = []
        
        report.append("=" * 80)
        report.append("📋 VST CODE REVIEW - CONSOLIDATED REPORT")
        report.append("=" * 80)
        report.append("")
        report.append(f"Project: Sp3ctra VST")
        report.append(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        report.append(f"Location: {self.project_root}")
        report.append("")
        report.append("=" * 80)
        report.append("")
        
        # Executive Summary
        report.append("📊 EXECUTIVE SUMMARY")
        report.append("=" * 80)
        report.append("")
        
        total_errors = len(self.all_issues['ERROR'])
        total_warnings = len(self.all_issues['WARNING'])
        total_info = len(self.all_issues['INFO'])
        total_issues = total_errors + total_warnings + total_info
        
        report.append(f"Total Issues Found: {total_issues}")
        report.append(f"  - ❌ Errors: {total_errors}")
        report.append(f"  - ⚠️  Warnings: {total_warnings}")
        report.append(f"  - ℹ️  Info: {total_info}")
        report.append("")
        
        # Critical issues (errors) by category
        if total_errors > 0:
            report.append("🚨 CRITICAL ISSUES (Errors):")
            report.append("-" * 80)
            for item in self.all_issues['ERROR']:
                issue = item['issue']
                report.append(f"  [{item['agent']}] {issue.get('category', 'Unknown')}")
                report.append(f"  Location: {issue.get('file', 'N/A')}:{issue.get('line', 0)}")
                report.append(f"  Message: {issue.get('message', 'No message')}")
                report.append("")
        else:
            report.append("✅ No critical errors found!")
            report.append("")
        
        # Priority recommendations
        report.append("💡 PRIORITY RECOMMENDATIONS")
        report.append("=" * 80)
        report.append("")
        
        recommendations = self._generate_recommendations()
        for i, rec in enumerate(recommendations, 1):
            report.append(f"{i}. {rec}")
        report.append("")
        
        # Detailed reports from each agent
        report.append("=" * 80)
        report.append("📑 DETAILED REPORTS BY AGENT")
        report.append("=" * 80)
        report.append("")
        
        for agent_name, agent_report in self.individual_reports:
            report.append(f"\n{'=' * 80}")
            report.append(f"Agent: {agent_name}")
            report.append(f"{'=' * 80}\n")
            report.append(agent_report)
            report.append("\n")
        
        # Footer
        report.append("=" * 80)
        report.append("📝 END OF CONSOLIDATED REPORT")
        report.append("=" * 80)
        report.append("")
        report.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        report.append(f"Report location: {self.reports_dir}")
        report.append("")
        
        return "\n".join(report)
    
    def _generate_recommendations(self) -> list:
        """Generate priority recommendations based on found issues"""
        recommendations = []
        
        # Analyze issues and generate smart recommendations
        error_categories = set(item['issue'].get('category', '') for item in self.all_issues['ERROR'])
        warning_categories = set(item['issue'].get('category', '') for item in self.all_issues['WARNING'])
        
        # Architecture recommendations
        if any('RT-Audio Safety' in cat for cat in error_categories):
            recommendations.append(
                "CRITICAL: Fix RT-audio safety violations in processBlock(). "
                "Remove any malloc/new/logging/mutex operations from the audio callback."
            )
        
        if any('Architecture' in cat for cat in warning_categories):
            recommendations.append(
                "Review architectural separation of concerns. Ensure PluginProcessor, "
                "Sp3ctraCore, and UI components have clear responsibilities."
            )
        
        # Duplication recommendations
        if any('Duplication' in cat for cat in warning_categories):
            recommendations.append(
                "Refactor duplicated code into shared libraries. This will improve "
                "maintainability and reduce the risk of divergent implementations."
            )
        
        # AI bias recommendations
        if any('AI Bias' in cat for cat in warning_categories):
            recommendations.append(
                "Clean up AI-generated code artifacts (excessive comments, placeholders, "
                "dead code). This will improve code readability and maintainability."
            )
        
        # UI recommendations
        if any('UI' in cat for cat in warning_categories):
            recommendations.append(
                "Standardize UI components (colors, fonts, layouts). Create a theme/style "
                "guide to ensure consistent user experience."
            )
        
        # Config recommendations
        if any('Config' in cat for cat in warning_categories):
            recommendations.append(
                "Ensure all VST configuration uses APVTS for DAW integration. "
                "Avoid .ini file dependencies in plugin code."
            )
        
        # Instance isolation
        if any('Instance Isolation' in cat for cat in warning_categories):
            recommendations.append(
                "Verify that multiple VST instances can coexist without conflicts. "
                "Eliminate global state and singleton patterns."
            )
        
        if not recommendations:
            recommendations.append("Code quality looks good! Continue maintaining best practices.")
        
        return recommendations
    
    def save_reports(self):
        """Save all reports to files"""
        print("\n📝 Saving reports...")
        
        # Save individual reports
        for agent_name, report in self.individual_reports:
            filename = agent_name.lower().replace(" ", "_") + "_report.txt"
            filepath = self.reports_dir / filename
            filepath.write_text(report)
            print(f"  ✓ {filepath}")
        
        # Save consolidated report
        consolidated = self.generate_consolidated_report()
        consolidated_path = self.reports_dir / "CONSOLIDATED_REPORT.txt"
        consolidated_path.write_text(consolidated)
        print(f"  ✓ {consolidated_path}")
        
        # Generate summary JSON for potential CI/CD integration
        import json
        summary = {
            'timestamp': datetime.now().isoformat(),
            'project': str(self.project_root),
            'summary': {
                'total_issues': len(self.all_issues['ERROR']) + len(self.all_issues['WARNING']) + len(self.all_issues['INFO']),
                'errors': len(self.all_issues['ERROR']),
                'warnings': len(self.all_issues['WARNING']),
                'info': len(self.all_issues['INFO'])
            },
            'agents_run': [name for name, _ in self.individual_reports]
        }
        
        summary_path = self.reports_dir / "summary.json"
        summary_path.write_text(json.dumps(summary, indent=2))
        print(f"  ✓ {summary_path}")
        
        print("\n✅ All reports saved!")
        
        return consolidated_path
    
    def print_summary(self):
        """Print a brief summary to console"""
        print("\n" + "=" * 80)
        print("📊 REVIEW SUMMARY")
        print("=" * 80)
        
        total_errors = len(self.all_issues['ERROR'])
        total_warnings = len(self.all_issues['WARNING'])
        total_info = len(self.all_issues['INFO'])
        
        print(f"❌ Errors: {total_errors}")
        print(f"⚠️  Warnings: {total_warnings}")
        print(f"ℹ️  Info: {total_info}")
        print("")
        
        if total_errors > 0:
            print("⚠️  CRITICAL: Fix errors before production!")
        elif total_warnings > 5:
            print("⚠️  Consider addressing warnings for better code quality")
        else:
            print("✅ Code quality looks good!")
        
        print("=" * 80)

def main():
    """Main entry point"""
    # Determine project root
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent
    
    print(f"\n🔍 Starting Code Review for: {project_root.name}")
    print(f"Location: {project_root}")
    
    # Create and run orchestrator
    orchestrator = CodeReviewOrchestrator(str(project_root))
    orchestrator.run_all_agents()
    
    # Save reports
    report_path = orchestrator.save_reports()
    
    # Print summary
    orchestrator.print_summary()
    
    print(f"\n📋 Full report available at: {report_path}")
    print("\n✅ Code review complete!")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
