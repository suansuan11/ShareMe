#!/usr/bin/env python3
"""Validate the repository-owned ShareMe skill without third-party packages."""

import ast
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SKILL_DIR = ROOT / ".agents/skills/shareme-sol-luna"
EXPECTED_INTERFACE = {
    "display_name": "ShareMe Sol-Luna",
    "short_description": "Run staged, evidence-led ShareMe development",
    "default_prompt": (
        "Use $shareme-sol-luna to continue ShareMe through the next verified stage."
    ),
}


def fail(message):
    print(f"Skill validation failed: {message}", file=sys.stderr)
    return 1


def parse_frontmatter(skill_file):
    try:
        content = skill_file.read_text(encoding="utf-8")
    except OSError as error:
        raise ValueError(f"cannot read {skill_file.name}: {error}") from error

    match = re.match(r"\A---\n(.*?)\n---(?:\n|\Z)", content, re.DOTALL)
    if not match:
        raise ValueError("SKILL.md has invalid YAML frontmatter delimiters")

    fields = {}
    for line in match.group(1).splitlines():
        field = re.fullmatch(r"([A-Za-z][A-Za-z0-9-]*):\s*(.+)", line)
        if not field:
            raise ValueError(f"unsupported SKILL.md frontmatter line: {line!r}")
        key, value = field.groups()
        if key in fields:
            raise ValueError(f"duplicate SKILL.md frontmatter key: {key}")
        fields[key] = value.strip()
    return fields


def validate_frontmatter(skill_file):
    fields = parse_frontmatter(skill_file)
    if set(fields) != {"name", "description"}:
        raise ValueError("SKILL.md frontmatter must contain only name and description")

    name = fields["name"]
    if name != "shareme-sol-luna":
        raise ValueError("SKILL.md name must be shareme-sol-luna")
    if len(name) > 64 or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", name):
        raise ValueError("SKILL.md name must be valid hyphen-case")

    description = fields["description"]
    if not description or len(description) > 1024:
        raise ValueError("SKILL.md description must contain 1 to 1024 characters")
    if "<" in description or ">" in description:
        raise ValueError("SKILL.md description cannot contain angle brackets")


def parse_interface(openai_file):
    try:
        lines = openai_file.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ValueError(f"cannot read agents/openai.yaml: {error}") from error

    if not lines or lines[0] != "interface:":
        raise ValueError("agents/openai.yaml must begin with interface:")

    interface = {}
    for line in lines[1:]:
        if line and not line.startswith(" "):
            break
        field = re.fullmatch(r"  ([a-z_]+):\s*(.+)", line)
        if not field:
            raise ValueError(f"unsupported agents/openai.yaml interface line: {line!r}")
        key, raw_value = field.groups()
        if key in interface:
            raise ValueError(f"duplicate agents/openai.yaml interface key: {key}")
        try:
            value = ast.literal_eval(raw_value)
        except (SyntaxError, ValueError) as error:
            raise ValueError(f"interface.{key} must be a quoted string") from error
        if not isinstance(value, str):
            raise ValueError(f"interface.{key} must be a quoted string")
        interface[key] = value
    return interface


def validate_interface(openai_file):
    interface = parse_interface(openai_file)
    for key, expected in EXPECTED_INTERFACE.items():
        if interface.get(key) != expected:
            raise ValueError(f"agents/openai.yaml interface.{key} is missing or stale")


def main(argv):
    if len(argv) > 2:
        return fail("usage: python3 scripts/validate_shareme_skill.py [skill-directory]")

    skill_dir = Path(argv[1]) if len(argv) == 2 else DEFAULT_SKILL_DIR
    try:
        validate_frontmatter(skill_dir / "SKILL.md")
        validate_interface(skill_dir / "agents/openai.yaml")
    except ValueError as error:
        return fail(str(error))

    print("Skill is valid!")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
