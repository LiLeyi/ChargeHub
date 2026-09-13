#!/usr/bin/env python3
"""Validate the repository-local engineering-skill configuration."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]


class AgentSkillSetupTest(unittest.TestCase):
    def test_agent_docs_exist_and_are_linked(self) -> None:
        agents = (REPO_ROOT / "AGENTS.md").read_text(encoding="utf-8")
        self.assertEqual(agents.count("## Agent skills"), 1)

        for relative_path in (
            "docs/agents/issue-tracker.md",
            "docs/agents/triage-labels.md",
            "docs/agents/domain.md",
        ):
            self.assertTrue((REPO_ROOT / relative_path).is_file(), relative_path)
            self.assertIn(relative_path, agents)

    def test_github_issue_tracker_configuration(self) -> None:
        tracker = (REPO_ROOT / "docs/agents/issue-tracker.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("https://github.com/LiLeyi/ChargeHub", tracker)
        self.assertIn("外部 Pull Request 不作为需求或分诊入口", tracker)

    def test_default_triage_labels_are_mapped(self) -> None:
        labels = (REPO_ROOT / "docs/agents/triage-labels.md").read_text(
            encoding="utf-8"
        )
        for label in (
            "needs-triage",
            "needs-info",
            "ready-for-agent",
            "ready-for-human",
            "wontfix",
        ):
            self.assertIn(f"`{label}` | `{label}`", labels)

    def test_domain_layout_is_single_context(self) -> None:
        domain = (REPO_ROOT / "docs/agents/domain.md").read_text(encoding="utf-8")
        self.assertIn("single-context", domain)
        self.assertIn("`CONTEXT.md`", domain)
        self.assertIn("`docs/adr/`", domain)


if __name__ == "__main__":
    unittest.main()
