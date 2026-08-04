from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SKILL = ROOT / ".agents/skills/shareme-sol-terra/SKILL.md"
VALIDATOR = ROOT / "scripts/validate_shareme_skill.py"
CONFIG = ROOT / ".codex/config.toml"
TERRA_EXPLORER = ROOT / ".codex/agents/terra_explorer.toml"
TERRA_IMPLEMENTER = ROOT / ".codex/agents/terra_implementer.toml"


class ShareMeSolTerraWorkflowTest(unittest.TestCase):
    def test_runtime_configuration_is_deterministic_and_bounded(self):
        self.assertTrue(CONFIG.is_file(), "project Codex configuration is missing")
        self.assertTrue(TERRA_EXPLORER.is_file(), "Terra explorer configuration is missing")
        self.assertTrue(TERRA_IMPLEMENTER.is_file(), "Terra implementer configuration is missing")
        config = CONFIG.read_text(encoding="utf-8")
        self.assertNotIn('model = "gpt-5.6-sol"', config)
        self.assertNotIn('model_reasoning_effort = "medium"', config)
        for required in (
            "[agents]",
            "enabled = true",
            "max_concurrent_threads_per_session = 2",
            'default_subagent_model = "gpt-5.6-terra"',
            'default_subagent_reasoning_effort = "medium"',
        ):
            self.assertIn(required, config)

        explorer = TERRA_EXPLORER.read_text(encoding="utf-8")
        implementer = TERRA_IMPLEMENTER.read_text(encoding="utf-8")
        for name, agent in (("terra_explorer", explorer), ("terra_implementer", implementer)):
            with self.subTest(agent=name):
                self.assertIn(f'name = "{name}"', agent)
                self.assertRegex(agent, r'description = "[^"\n]+"')
                self.assertIn('developer_instructions = """', agent)
                self.assertIn('model = "gpt-5.6-terra"', agent)
                self.assertIn('model_reasoning_effort = "medium"', agent)
        self.assertIn('sandbox_mode = "read-only"', explorer)
        self.assertIn("do not commit", implementer.lower())
        self.assertRegex(implementer.lower(), r"do not\s+expand scope")

    def test_required_files_exist(self):
        required = (
            ROOT / "AGENTS.md",
            SKILL,
            ROOT / ".agents/skills/shareme-sol-terra/agents/openai.yaml",
            ROOT / ".agents/skills/shareme-sol-terra/references/project-contract.md",
            ROOT / ".agents/skills/shareme-sol-terra/references/role-contracts.md",
            CONFIG,
            TERRA_EXPLORER,
            TERRA_IMPLEMENTER,
            ROOT / "docs/development/current-stage.md",
        )
        self.assertEqual([], [str(path.relative_to(ROOT)) for path in required if not path.is_file()])

    def test_skill_frontmatter_and_trigger(self):
        text = SKILL.read_text(encoding="utf-8")
        self.assertRegex(text, r"(?s)\A---\nname: shareme-sol-terra\ndescription: Use when[^\n]+\n---")
        for trigger in ("ShareMe", "继续开发", "implementation", "diagnosis", "review"):
            self.assertIn(trigger, text)

    def test_repository_skill_validator(self):
        self.assertTrue(VALIDATOR.is_file(), "repository skill validator is missing")

        result = subprocess.run(
            [sys.executable, str(VALIDATOR)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("Skill is valid!\n", result.stdout)

        mutations = {
            "invalid skill name": (
                "SKILL.md",
                "name: shareme-sol-terra",
                "name: ShareMe-Sol-Terra",
            ),
            "invalid skill description": (
                "SKILL.md",
                "description: Use when working in the ShareMe repository",
                "description: Use <ShareMe> when working in the repository",
            ),
            "stale display name": (
                "agents/openai.yaml",
                'display_name: "ShareMe Sol-Terra"',
                'display_name: "Stale ShareMe Skill"',
            ),
            "stale short description": (
                "agents/openai.yaml",
                'short_description: "Run staged, evidence-led ShareMe development"',
                'short_description: "Stale description"',
            ),
            "stale default prompt": (
                "agents/openai.yaml",
                "Use $shareme-sol-terra to continue ShareMe through the next verified stage.",
                "Use ShareMe without the skill trigger.",
            ),
        }
        for label, (relative_path, old, new) in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temp_dir:
                candidate = Path(temp_dir) / "shareme-sol-terra"
                shutil.copytree(SKILL.parent, candidate)
                target = candidate / relative_path
                text = target.read_text(encoding="utf-8")
                self.assertIn(old, text)
                target.write_text(text.replace(old, new, 1), encoding="utf-8")

                rejected = subprocess.run(
                    [sys.executable, str(VALIDATOR), str(candidate)],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertNotEqual(0, rejected.returncode)

    def test_agents_points_to_skill_and_stage(self):
        text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
        self.assertIn(".agents/skills/shareme-sol-terra/SKILL.md", text)
        self.assertIn("docs/development/current-stage.md", text)
        self.assertIn("one writer owns", text)

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
        self.assertRegex(
            text,
            r"deterministic ShareMe workflow stage is delivered on\s+`codex/shareme-workflow-simplify`",
        )
        self.assertNotIn("handoff-only", text)

    def test_workflow_is_portable_and_complete(self):
        files = [
            ROOT / "AGENTS.md",
            SKILL,
            ROOT / "docs/development/current-stage.md",
            *SKILL.parent.joinpath("references").glob("*.md"),
        ]
        combined = "\n".join(path.read_text(encoding="utf-8") for path in files)
        for required in (
            "Sol works directly by default",
            "at most two independent Terra",
            "terra_explorer",
            "terra_implementer",
            "one writer",
            "Unmeasured",
            'A user-level filesystem-read-only or "Do not edit" request authorizes zero filesystem mutations',
            "A request to free disk space is not deletion authorization",
            "Never bypass a rejected destructive command",
            "Compute numerical Git claims from exact current commands",
            "Source-read-only test execution may write only explicitly allowed ignored build output",
            '"Do not edit" overrides source-read-only test execution',
            "verified",
            "environment-dependent",
            "libwebrtc",
        ):
            self.assertIn(required, combined)
        self.assertNotRegex(combined, r"/Users/[^/]+/")
        self.assertNotRegex(combined, r"[A-Za-z]:\\Users\\")
        self.assertNotRegex(combined, r"\b(?:TODO|TBD|FIXME)\b")
        self.assertLessEqual(len((ROOT / "AGENTS.md").read_text(encoding="utf-8").splitlines()), 120)
        self.assertLessEqual(len(re.findall(r"\S+", SKILL.read_text(encoding="utf-8"))), 300)

    def test_direct_first_role_contract_is_concise(self):
        role_contract = (SKILL.parent / "references/role-contracts.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("Sol works directly by default", role_contract)
        self.assertIn("at most two independent Terra", role_contract)
        self.assertIn("terra_explorer", role_contract)
        self.assertIn("terra_implementer", role_contract)
        self.assertIn("independent read-only", role_contract)
        self.assertIn("one writer", role_contract)
        self.assertIn("Target:", role_contract)
        self.assertIn("Return summary:", role_contract)
        self.assertNotIn("Target capability tier:", role_contract)
        self.assertNotIn("Actual model/fallback:", role_contract)
        self.assertLessEqual(len(re.findall(r"\S+", role_contract)), 350)


if __name__ == "__main__":
    unittest.main()
