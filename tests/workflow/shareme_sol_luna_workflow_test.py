from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SKILL = ROOT / ".agents/skills/shareme-sol-luna/SKILL.md"
VALIDATOR = ROOT / "scripts/validate_shareme_skill.py"


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
                "name: shareme-sol-luna",
                "name: ShareMe-Sol-Luna",
            ),
            "invalid skill description": (
                "SKILL.md",
                "description: Use when working in the ShareMe repository",
                "description: Use <ShareMe> when working in the repository",
            ),
            "stale display name": (
                "agents/openai.yaml",
                'display_name: "ShareMe Sol-Luna"',
                'display_name: "Stale ShareMe Skill"',
            ),
            "stale short description": (
                "agents/openai.yaml",
                'short_description: "Run staged, evidence-led ShareMe development"',
                'short_description: "Stale description"',
            ),
            "stale default prompt": (
                "agents/openai.yaml",
                "Use $shareme-sol-luna to continue ShareMe through the next verified stage.",
                "Use ShareMe without the skill trigger.",
            ),
        }
        for label, (relative_path, old, new) in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temp_dir:
                candidate = Path(temp_dir) / "shareme-sol-luna"
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
        self.assertRegex(
            text,
            r"dynamic-routing stage is delivered on `main` through merge commit\s+`ac118c4`",
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
        dynamic_documents = (
            ROOT / "docs/superpowers/specs/2026-08-03-shareme-dynamic-model-routing-design.md",
            ROOT / "docs/superpowers/plans/2026-08-03-shareme-dynamic-model-routing.md",
        )
        dynamic_text = "\n".join(path.read_text(encoding="utf-8") for path in dynamic_documents)
        self.assertNotRegex(dynamic_text, r"/Users/[^/]+/")
        self.assertNotRegex(dynamic_text, r"[A-Za-z]:\\Users\\")
        self.assertIn("Cost-tier basis:", dynamic_text)
        self.assertIn("Actual model/fallback:", dynamic_text)
        self.assertNotIn("without mutations", dynamic_text)
        self.assertNotIn("zero-write behavior", dynamic_text)

        for required in (
            "Default active Luna count: <= 2",
            "Hard maximum: 3",
            "Concurrent writers in overlapping scope: 0",
            'A user-level filesystem-read-only or "Do not edit" request authorizes zero filesystem mutations',
            "A request to free disk space is not deletion authorization",
            "Never bypass a rejected destructive command",
            "Compute numerical Git claims from exact current commands",
            "Every role named for dispatch needs its own fully instantiated contract",
            "Source-read-only test execution may write only explicitly allowed ignored build output",
            '"Do not edit" overrides source-read-only test execution',
            "Independent review | Filesystem-read-only Tester/Reviewer; no writes",
            "Independent test execution | Source-read-only Tester/Reviewer; builds only with explicit ignored build-output scope",
            "Inspect the model choices exposed by the current runtime",
            "Target capability tier:",
            "Requested model:",
            "Requested reasoning effort:",
            "Selection reason:",
            "Cost-tier basis:",
            "Fallback or difference:",
            "Actual model/fallback:",
            "Never claim measured credit savings without per-agent usage telemetry",
            "Never create duplicate agents merely to save credits",
            "Without a cost-tier basis, select by capability only and make no expected or realized cost-saving claim",
            "verified",
            "environment-dependent",
            "libwebrtc",
        ):
            self.assertIn(required, combined)
        self.assertNotRegex(combined, r"/Users/[^/]+/")
        self.assertNotRegex(combined, r"[A-Za-z]:\\Users\\")
        self.assertNotRegex(combined, re.compile(r"gpt-[0-9]", re.IGNORECASE))
        self.assertFalse((ROOT / ".codex/config.toml").exists())
        self.assertNotRegex(combined, r"\b(?:TODO|TBD|FIXME)\b")
        self.assertLessEqual(len((ROOT / "AGENTS.md").read_text(encoding="utf-8").splitlines()), 120)
        self.assertLessEqual(len(re.findall(r"\S+", SKILL.read_text(encoding="utf-8"))), 500)

    def test_dispatch_and_return_templates_are_exact(self):
        role_contract = (SKILL.parent / "references/role-contracts.md").read_text(
            encoding="utf-8"
        )

        def normalized_template(heading):
            match = re.search(
                rf"## {re.escape(heading)}.*?```text\n(.*?)\n```",
                role_contract,
                re.DOTALL,
            )
            self.assertIsNotNone(match)
            return [line.strip() for line in match.group(1).splitlines() if line.strip()]

        expected_dispatch = [
            "Role:",
            "Target capability tier:",
            "Requested model:",
            "Requested reasoning effort:",
            "Selection reason:",
            "Cost-tier basis:",
            "Fallback or difference:",
            "Goal:",
            "Allowed scope:",
            "Forbidden scope:",
            "Context and evidence:",
            "Acceptance:",
            "Commands/tests:",
            "Rollback:",
            "Return format:",
        ]
        expected_return = [
            "Investigation:",
            "Changes:",
            "Commands:",
            "Tests:",
            "Risks:",
            "Open issues:",
            "Actual model/fallback:",
        ]
        self.assertEqual(expected_dispatch, normalized_template("Exact dispatch contract"))
        self.assertEqual(expected_return, normalized_template("Exact return contract"))


if __name__ == "__main__":
    unittest.main()
