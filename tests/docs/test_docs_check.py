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
    sys.modules[spec.name] = module  # dataclasses look their module up there
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


# Spelled in two parts so the citation scanner does not read the synthetic
# citations below as citations of this file.
VERIFIES = "Verifies" + ":"


class TraceTest(unittest.TestCase):
    """The traceability gate (B-15, ADR-0010) on a synthetic repository.

    Verifies: b15-requirement-test-traceability-v2 (criterion 4: each
    failure case of criterion 2), nfr012-traceability-v2 (criterion 2).
    """

    REQUIREMENTS = """\
        #### <a id="fr001-one-v1"></a>**[FR-001 v1]** One
        #### <a id="fr002-two-v2"></a>**[FR-002 v2]** Two
        #### <a id="fr003-three-v1"></a>**[FR-003 v1]** Three
        #### <a id="nfr001-quality-v1"></a>**[NFR-001 v1]** Quality
        """

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self._tmp.name)
        self.write("docs/specs/requirements.md", self.REQUIREMENTS)
        self.roadmap(m1_done=False)
        self.results: dict[str, str] = {}

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def write(self, name: str, text: str) -> None:
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(text), encoding="utf-8")

    def roadmap(self, m1_done: bool) -> None:
        status = "Done (2026-10-04)" if m1_done else "open"
        self.write("docs/plan/roadmap.md", f"""\
            ## <a id="m0-start-v1"></a>**[M0 v1]** Start

            **Status:** done.

            **Requirements:** [NFR-001](../specs/requirements.md#nfr001-quality-v1).

            ## <a id="m1-core-v1"></a>**[M1 v1]** Core

            **Status:** {status}

            **Requirements:** [FR-001](../specs/requirements.md#fr001-one-v1)–[FR-003](../specs/requirements.md#fr003-three-v1).

            ## Publication
            """)

    def trace(self) -> tuple[dict[str, dict[str, str]], list[str]]:
        cases = "".join(
            f'<testcase name="{name}">{"<failure/>" if result == "failed" else "<skipped/>" if result == "skipped" else ""}</testcase>'
            for name, result in self.results.items()
        )
        junit = self.repo / "junit.xml"
        junit.write_text(f"<testsuite>{cases}</testsuite>", encoding="utf-8")
        matrix, problems = checker.trace(self.repo / "docs", self.repo, junit)
        return {r.label: r.tests for r in matrix}, problems

    def test_header_citation_covers_every_test_and_a_block_covers_the_next(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            // {VERIFIES} fr001-one-v1 (both tests)
            #include <gtest/gtest.h>

            TEST(A, First) {{}}

            // {VERIFIES} fr002-two-v2 (this test only)
            TEST(A, Second) {{}}
            """)
        self.results = {"A.First": "passed", "A.Second": "passed"}
        matrix, problems = self.trace()
        self.assertEqual(problems, [])
        self.assertEqual(sorted(matrix["FR-001"]), ["A.First", "A.Second"])
        self.assertEqual(sorted(matrix["FR-002"]), ["A.Second"])

    def test_milestone_range_assigns_every_requirement_in_it(self) -> None:
        milestones = checker.milestones(self.repo / "docs")
        self.assertEqual(milestones["M1"], (1, False, ["FR-001", "FR-002", "FR-003"]))
        self.assertEqual(milestones["M0"], (0, True, ["NFR-001"]))

    def test_citation_of_an_old_version_fails(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            // {VERIFIES} fr002-two-v1
            TEST(A, First) {{}}
            """)
        self.results = {"A.First": "passed"}
        _, problems = self.trace()
        self.assertEqual(len(problems), 1)
        self.assertIn("tests/a_test.cpp:1: cites 'fr002-two-v1', which does not exist", problems[0])

    def test_old_version_cited_outside_tests_fails_too(self) -> None:
        self.write("scripts/x.sh", f"""\
            # {VERIFIES} fr001-one-v7
            """)
        _, problems = self.trace()
        self.assertEqual(len(problems), 1)
        self.assertIn("scripts/x.sh:1: cites 'fr001-one-v7'", problems[0])

    def test_failed_citing_test_fails(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            // {VERIFIES} fr001-one-v1
            TEST(A, First) {{}}
            """)
        self.results = {"A.First": "failed"}
        _, problems = self.trace()
        self.assertEqual(problems, ["tests/a_test.cpp:1: A.First, citing fr001-one-v1, failed"])

    def test_citing_test_missing_from_the_results_fails_and_skipped_does_not(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            // {VERIFIES} fr001-one-v1
            TEST(A, First) {{}}
            TEST(A, Second) {{}}
            """)
        self.results = {"A.First": "skipped"}
        matrix, problems = self.trace()
        self.assertEqual(problems, ["tests/a_test.cpp:1: A.Second, citing fr001-one-v1, not run"])
        self.assertEqual(matrix["FR-001"]["A.First"], "skipped")

    def test_requirement_of_a_done_milestone_without_test_fails(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            // {VERIFIES} fr001-one-v1 fr002-two-v2
            TEST(A, First) {{}}
            """)
        self.results = {"A.First": "passed"}
        self.assertEqual(self.trace()[1], [])  # M1 not done yet
        self.roadmap(m1_done=True)
        _, problems = self.trace()
        self.assertEqual(len(problems), 1)
        self.assertIn("FR-003 v1 belongs to done milestone M1 and no test that ran cites it", problems[0])

    def test_requirement_cited_only_by_skipped_tests_is_skipped_and_fails_when_done(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            // {VERIFIES} fr001-one-v1 fr002-two-v2 fr003-three-v1
            TEST(A, First) {{}}
            TEST(A, Second) {{}}
            """)
        self.results = {"A.First": "skipped", "A.Second": "skipped"}
        self.roadmap(m1_done=True)
        matrix_rows, problems = checker.trace(self.repo / "docs", self.repo, self._junit())
        self.assertEqual({checker.requirement_result(r) for r in matrix_rows if r.label.startswith("FR")}, {"skipped"})
        self.assertEqual(len(problems), 3)
        # One passing test is enough; a skipped one beside it changes nothing.
        self.results = {"A.First": "skipped", "A.Second": "passed"}
        matrix_rows, problems = checker.trace(self.repo / "docs", self.repo, self._junit())
        self.assertEqual(problems, [])
        self.assertIn("| [FR-001](../specs/requirements.md#fr001-one-v1) One | v1 | M1 | `A.First`<br>`A.Second` | passed |",
                      checker.matrix_markdown(matrix_rows))

    def test_verifies_without_a_versioned_anchor_fails(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            // {VERIFIES} FR-001 v1
            TEST(A, First) {{}}
            """)
        self.results = {"A.First": "passed"}
        self.assertEqual(self.trace()[1], [f"tests/a_test.cpp:1: {VERIFIES} names no versioned anchor (<doc>-<slug>-v<N>)"])

    def test_unknown_milestone_status_is_an_error(self) -> None:
        roadmap = self.repo / "docs/plan/roadmap.md"
        roadmap.write_text(roadmap.read_text(encoding="utf-8").replace("**Status:** open", "**Status:** ✅ finished"),
                           encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "M1 status must start with 'done' or 'open'"):
            checker.milestones(self.repo / "docs")

    def test_python_citations_are_not_shown_as_passed(self) -> None:
        self.write("tests/docs/test_x.py", f'''\
            """{VERIFIES} fr001-one-v1"""
            ''')
        matrix_rows, problems = checker.trace(self.repo / "docs", self.repo, self._junit())
        self.assertEqual(problems, [])
        self.assertEqual(checker.requirement_result(next(r for r in matrix_rows if r.label == "FR-001")), "not in CTest")

    def test_done_m0_is_not_gated(self) -> None:
        # NFR-001 belongs to M0 (done) and has no test: M0 is checked by CI jobs.
        self.assertEqual(self.trace()[1], [])

    def test_citation_not_attached_to_a_test_fails(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            #include <gtest/gtest.h>

            // {VERIFIES} fr001-one-v1

            TEST(A, First) {{}}
            """)
        self.results = {"A.First": "passed"}
        _, problems = self.trace()
        self.assertEqual(problems, [f"tests/a_test.cpp:3: {VERIFIES} is neither in the file header nor right above a TEST"])

    def test_cmake_citation_covers_the_add_tests_of_its_block(self) -> None:
        self.write("tests/CMakeLists.txt", f"""\
            # {VERIFIES} fr003-three-v1
            add_test(NAME Arch.One COMMAND x)
            add_test(NAME Arch.Two COMMAND y)

            add_test(NAME Other COMMAND z)
            """)
        self.results = {"Arch.One": "passed", "Arch.Two": "passed", "Other": "passed"}
        matrix, problems = self.trace()
        self.assertEqual(problems, [])
        self.assertEqual(sorted(matrix["FR-003"]), ["Arch.One", "Arch.Two"])

    def test_matrix_page_and_csv(self) -> None:
        self.write("tests/a_test.cpp", f"""\
            // {VERIFIES} fr001-one-v1
            TEST(A, First) {{}}
            """)
        self.results = {"A.First": "passed"}
        matrix_rows, _ = checker.trace(self.repo / "docs", self.repo, self._junit())
        page = checker.matrix_markdown(matrix_rows)
        self.assertIn("do not edit", page)
        self.assertIn("| [FR-001](../specs/requirements.md#fr001-one-v1) One | v1 | M1 | `A.First` | passed |", page)
        self.assertIn("| [FR-002](../specs/requirements.md#fr002-two-v2) Two | v2 | M1 | – | no test |", page)
        csv_path = self.repo / "out.csv"
        checker.write_csv(matrix_rows, csv_path)
        self.assertEqual(csv_path.read_text(encoding="utf-8").splitlines()[:2],
                         ["requirement,version,milestones,test,result", "FR-001,1,M1,A.First,passed"])

    def _junit(self) -> Path:
        self.trace()
        return self.repo / "junit.xml"


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
