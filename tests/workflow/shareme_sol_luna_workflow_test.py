from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SKILL = ROOT / ".agents/skills/shareme-sol-luna/SKILL.md"


class ShareMeSolLunaWorkflowTest(unittest.TestCase):
    def test_required_files_exist(self):
        required = (
            ROOT / "AGENTS.md",
            SKILL,
            ROOT / ".agents/skills/shareme-sol-luna/agents/openai.yaml",
            ROOT / ".agents/skills/shareme-sol-luna/references/project-contract.md",
            ROOT / ".agents/skills/shareme-sol-luna/references/role-contracts.md",
            ROOT / "docs/development/current-stage.md",
        )
        self.assertEqual([], [str(path.relative_to(ROOT)) for path in required if not path.is_file()])

    def test_skill_frontmatter_and_trigger(self):
        text = SKILL.read_text(encoding="utf-8")
        self.assertRegex(text, r"(?s)\A---\nname: shareme-sol-luna\ndescription: Use when[^\n]+\n---")
        for trigger in ("ShareMe", "继续开发", "implementation", "diagnosis", "review"):
            self.assertIn(trigger, text)

    def test_agents_points_to_skill_and_stage(self):
        text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
        self.assertIn(".agents/skills/shareme-sol-luna/SKILL.md", text)
        self.assertIn("docs/development/current-stage.md", text)
        self.assertIn("Do not use concurrent writers", text)

    def test_stage_contract_has_required_sections(self):
        text = (ROOT / "docs/development/current-stage.md").read_text(encoding="utf-8")
        for heading in (
            "# ShareMe Current Development Stage",
            "## Delivered baseline",
            "## Verification status",
            "## Active stage",
            "## Next recommended stage",
            "## Git handoff",
        ):
            self.assertIn(heading, text)

    def test_workflow_is_portable_and_complete(self):
        files = [ROOT / "AGENTS.md", SKILL, *SKILL.parent.joinpath("references").glob("*.md")]
        combined = "\n".join(path.read_text(encoding="utf-8") for path in files)
        self.assertNotRegex(combined, r"/Users/[^/]+/")
        self.assertNotRegex(combined, r"[A-Za-z]:\\Users\\")
        self.assertNotRegex(combined, r"\b(?:TODO|TBD|FIXME)\b")


if __name__ == "__main__":
    unittest.main()
