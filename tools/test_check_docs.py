"""Tests for check_docs.py: each check must fail on a deliberately broken copy
of the real documentation, and pass on an unmodified copy.

    python3 tools/test_check_docs.py
"""

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_docs  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
COPIED = ["README.md", "AGENTS.md", "LICENSE", "docs", "include", "tests", "src",
          "reference/README.md", "tools/README.md"]


class CheckDocsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        for item in COPIED:
            source = ROOT / item
            dest = self.root / item
            dest.parent.mkdir(parents=True, exist_ok=True)
            if source.is_dir():
                shutil.copytree(source, dest)
            elif source.is_file():
                shutil.copy2(source, dest)

    def tearDown(self):
        self._tmp.cleanup()

    def edit(self, rel, old, new):
        path = self.root / rel
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text, f"fixture text missing from {rel}")
        path.write_text(text.replace(old, new, 1), encoding="utf-8")

    def append(self, rel, text):
        with open(self.root / rel, "a", encoding="utf-8") as handle:
            handle.write(text)

    def assertFlags(self, fragment):
        errors = check_docs.run(self.root)
        self.assertTrue(any(fragment in e for e in errors),
                        f"expected an error containing {fragment!r}, got {errors}")

    def test_unmodified_copy_passes(self):
        self.assertEqual(check_docs.run(self.root), [])

    def test_documented_fact_without_page(self):
        self.edit("docs/evidence.md", "| D     | p.1 Features            |", "| D     | Features                |")
        self.assertFlags("D-01: documented fact has no page reference")

    def test_invalid_class(self):
        self.edit("docs/evidence.md", "| D-02 | 8x interpolator precedes each DAC delta-sigma modulator                           | D ",
                  "| D-02 | 8x interpolator precedes each DAC delta-sigma modulator                           | Q ")
        self.assertFlags("D-02: class 'Q'")

    def test_undefined_policy_citation(self):
        self.append("docs/features.md", "\nSee P-99.\n")
        self.assertFlags("cites P-99")

    def test_undefined_requirement_citation(self):
        self.append("docs/api.md", "\nSee REQ-FAKE-01.\n")
        self.assertFlags("cites REQ-FAKE-01")

    def test_citation_in_header_checked(self):
        self.append("include/ak4522/config.hpp", "\n// See D-Z99.\n")
        self.assertFlags("cites D-Z99")

    def test_untraced_requirement(self):
        agents = self.root / "AGENTS.md"
        agents.write_text(agents.read_text(encoding="utf-8") + "\n**REQ-NEW-01:** Something new.\n",
                          encoding="utf-8")
        self.assertFlags("REQ-NEW-01: defined in AGENTS.md but not traced")

    def test_checksum_disagreement(self):
        self.edit("reference/README.md", "0db2cd2f", "1db2cd2f")
        self.assertFlags("datasheet checksum differs")

    def test_local_datasheet_mismatch(self):
        (self.root / "reference/AK4522.PDF").write_bytes(b"not the datasheet")
        self.assertFlags("does not match the recorded checksum")

    def test_em_dash(self):
        self.append("docs/api.md", "\nA — B\n")
        self.assertFlags("em or en dash")

    def test_broken_link(self):
        self.append("README.md", "\n[missing](docs/nope.md)\n")
        self.assertFlags("link target 'docs/nope.md' does not exist")

    def test_broken_anchor(self):
        self.append("docs/evidence.md", "\n[x](#no-such-heading)\n")
        self.assertFlags("anchor '#no-such-heading'")

    def test_range_expansion(self):
        ids = check_docs.expand_ranges("D-T01 to D-T03 and P-10", r"D-[A-Z]\d{2}")
        self.assertEqual(ids, {"D-T01", "D-T02", "D-T03"})


if __name__ == "__main__":
    unittest.main()
