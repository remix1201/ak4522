"""Generated coefficient files must agree with their design reports and with
the constants compiled into the C++ code, and still meet the response gates."""

import hashlib
import json
import re
import unittest
from pathlib import Path

import numpy as np

from ak4522_tools import dac_gates

ROOT = Path(__file__).resolve().parents[2]


def read_inc(path: Path) -> np.ndarray:
    values = []
    for line in path.read_text(encoding="utf-8").splitlines():
        literal = line.split("//")[0].strip().rstrip(",")
        if literal:
            values.append(float.fromhex(literal))
    return np.array(values)


class InterpolatorFilesTest(unittest.TestCase):
    def setUp(self):
        self.taps = read_inc(ROOT / "src/dsp/interpolator_coefficients.inc")
        self.report = json.loads((ROOT / "src/dsp/interpolator_design.json").read_text())

    def test_length_matches_report_and_cpp(self):
        header = (ROOT / "src/dsp/interpolator.hpp").read_text(encoding="utf-8")
        cpp_taps = int(re.search(r"taps = (\d+);", header).group(1))
        self.assertEqual(self.taps.size, self.report["length"])
        self.assertEqual(self.taps.size, cpp_taps)

    def test_checksum_matches_report_and_header_comment(self):
        digest = hashlib.sha256(np.ascontiguousarray(self.taps, dtype="<f8").tobytes()).hexdigest()
        self.assertEqual(digest, self.report["sha256_le_f8"])
        inc = (ROOT / "src/dsp/interpolator_coefficients.inc").read_text(encoding="utf-8")
        self.assertIn(digest, inc)

    def test_committed_coefficients_meet_the_gates(self):
        for dac_rate in (32000.0, 44100.0, 48000.0):
            checks = dac_gates.digital_filter_checks(self.taps, 8 * dac_rate, dac_rate,
                                                     check_delay=False)
            for check in checks:
                with self.subTest(rate=dac_rate, check=check.name):
                    self.assertTrue(check.passed, check)


if __name__ == "__main__":
    unittest.main()
