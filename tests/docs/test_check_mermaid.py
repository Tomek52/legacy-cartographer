"""Tests for the Mermaid check of the committed module maps (backlog B-22).

Verifies: fr007-module-map-without-llm-v1 (criterion 1: the committed maps
parse with the Mermaid that MkDocs Material loads).

scripts/check-mermaid.sh runs the pinned Mermaid of tools/mermaid-check on
the committed libmodbus map and the module-map golden files, and on the
fixtures in tests/docs/mermaid. Skipped when Node.js or the pinned packages
(`npm ci --prefix tools/mermaid-check`) are missing, unless
LCART_REQUIRE_MERMAID=1 (set in CI, where a skip would hide the check).

Run: python3 -m unittest discover -s tests/docs -v
"""

from __future__ import annotations

import os
import shutil
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts" / "check-mermaid.sh"
FIXTURES = Path(__file__).resolve().parent / "mermaid"
INSTALLED = (REPO / "tools" / "mermaid-check" / "node_modules" / "mermaid").is_dir()
AVAILABLE = shutil.which("node") is not None and INSTALLED
REQUIRED = os.environ.get("LCART_REQUIRE_MERMAID") == "1"


def check_mermaid(*files: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(SCRIPT), *map(str, files)], cwd=REPO, capture_output=True, text=True, timeout=300, check=False
    )


@unittest.skipUnless(AVAILABLE or REQUIRED, "Node.js or tools/mermaid-check/node_modules missing")
class CheckMermaidTest(unittest.TestCase):
    def test_committed_maps_parse(self) -> None:
        result = check_mermaid()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("check-mermaid: OK", result.stdout)
        # The libmodbus map and at least one golden file were checked.
        self.assertRegex(result.stdout, r"\(\d+ block\(s\) in ([2-9]|\d{2,}) file\(s\)")

    def test_good_diagram_passes(self) -> None:
        result = check_mermaid(FIXTURES / "good.md")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_broken_diagram_fails_naming_the_file_and_mermaids_error(self) -> None:
        broken = FIXTURES / "broken.md"
        result = check_mermaid(FIXTURES / "good.md", broken)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        # The second block of broken.md opens on line 11; the first parses.
        self.assertIn(f"{broken}:11: Mermaid ", result.stderr)
        self.assertNotIn(f"{broken}:6:", result.stderr)
        self.assertIn("Parse error", result.stderr)  # Mermaid's own message
        self.assertNotIn("check-mermaid: OK", result.stdout)

    def test_file_without_a_mermaid_block_fails(self) -> None:
        result = check_mermaid(REPO / "README.md")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("README.md: no mermaid block", result.stderr)


if __name__ == "__main__":
    unittest.main()
