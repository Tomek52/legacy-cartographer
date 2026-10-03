"""Tests for the documentation gate (backlog B-03, NFR-012).

Verifies: nfr012-traceability-v2 (anchor format and uniqueness are checked).

Two layers are tested:
- scripts/check-doc-anchors.py on small synthetic doc trees;
- the real mkdocs.yml (strict build) on a temporary copy of docs/ with one
  defect injected per test. Skipped when MkDocs is not installed.

Run: python3 -m unittest discover -s tests/docs -v
"""

from __future__ import annotations

import importlib.util
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def load_checker():
    spec = importlib.util.spec_from_file_location("check_doc_anchors", REPO / "scripts" / "check-doc-anchors.py")
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


checker = load_checker()


class AnchorCheckerTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.docs = Path(self._tmp.name) / "docs"
        self.docs.mkdir()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def write(self, name: str, text: str) -> Path:
        path = self.docs / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(text), encoding="utf-8")
        return path

    def problems(self) -> list[str]:
        return checker.check(self.docs)

    def test_valid_requirement_and_rule_anchors_pass(self) -> None:
        self.write("req.md", """\
            #### <a id="fr001-ingest-compilation-database-v1"></a>**[FR-001 v1]** Ingest
            ## <a id="arch-principles-v2"></a>Principles
            """)
        self.assertEqual(self.problems(), [])

    def test_bad_format_is_reported(self) -> None:
        self.write("a.md", '<a id="FR-001"></a> text\n')
        self.assertIn("does not match", "\n".join(self.problems()))

    def test_positional_slug_without_version_is_reported(self) -> None:
        self.write("a.md", '<a id="fr001-criterion-5"></a> text\n')
        self.assertEqual(len(self.problems()), 1)

    def test_duplicate_anchor_is_reported_across_files(self) -> None:
        self.write("a.md", '<a id="arch-rule-one-v1"></a> one\n')
        self.write("sub/b.md", '<a id="arch-rule-one-v1"></a> two\n')
        problems = self.problems()
        self.assertEqual(len(problems), 1)
        self.assertIn("duplicate anchor", problems[0])

    def test_marker_with_wrong_version_is_reported(self) -> None:
        self.write("b.md", '### <a id="b07-clang-spike-v2"></a>**[B-07 v1]** Spike\n')
        self.assertIn("does not match anchor", "\n".join(self.problems()))

    def test_marker_with_wrong_id_is_reported(self) -> None:
        self.write("b.md", '#### <a id="nfr004-deterministic-output-v1"></a>**[NFR-005 v1]** Det\n')
        self.assertIn("does not match anchor", "\n".join(self.problems()))

    def test_requirement_heading_without_marker_is_reported(self) -> None:
        self.write("r.md", '#### <a id="fr004-record-call-edges-v1"></a>Record call edges\n')
        self.assertIn("needs a visible marker", "\n".join(self.problems()))

    def test_requirement_heading_without_anchor_is_reported(self) -> None:
        self.write("r.md", "#### FR-004 Record call edges\n")
        self.assertIn("without an anchor", "\n".join(self.problems()))

    def test_anchors_in_code_are_ignored(self) -> None:
        self.write("c.md", """\
            ```html
            <a id="NOT-AN-ANCHOR"></a>
            ```
            Inline `<a id="also-not"></a>` too.
            """)
        self.assertEqual(self.problems(), [])

    def test_adr_template_is_skipped(self) -> None:
        self.write("adr/template.md", '<a id="adrNNNN-short-slug-v1"></a>\n')
        self.assertEqual(self.problems(), [])

    def test_problem_lines_point_at_file_and_line(self) -> None:
        self.write("p.md", 'ok\n<a id="bad"></a>\n')
        self.assertTrue(self.problems()[0].startswith("docs/p.md:2:"))


@unittest.skipIf(shutil.which("mkdocs") is None and not (REPO / ".venv-docs" / "bin" / "mkdocs").exists(),
                 "MkDocs not installed")
class StrictBuildTest(unittest.TestCase):
    """The real mkdocs.yml must fail on each class of documentation defect."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        shutil.copytree(REPO / "docs", self.root / "docs")
        shutil.copy(REPO / "mkdocs.yml", self.root / "mkdocs.yml")
        venv = REPO / ".venv-docs" / "bin" / "mkdocs"
        self.mkdocs = str(venv) if venv.exists() else "mkdocs"

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def build(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [self.mkdocs, "build", "--strict", "--site-dir", str(self.root / "site")],
            cwd=self.root, capture_output=True, text=True, check=False)

    def append(self, page: str, text: str) -> None:
        with (self.root / "docs" / page).open("a", encoding="utf-8") as handle:
            handle.write(text)

    def test_unmodified_docs_build(self) -> None:
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_page_missing_from_nav_fails(self) -> None:
        (self.root / "docs" / "orphan.md").write_text("# Orphan\n", encoding="utf-8")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not included in the \"nav\"", result.stderr)

    def test_broken_link_fails(self) -> None:
        self.append("index.md", "\n[gone](no-such-page.md)\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no-such-page.md", result.stderr)

    def test_link_to_old_anchor_version_fails(self) -> None:
        self.append("index.md", "\n[old](specs/requirements.md#fr004-record-call-edges-v0)\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("fr004-record-call-edges-v0", result.stderr)


if __name__ == "__main__":
    sys.exit(unittest.main())
