"""Generated coefficient files must agree with their design reports and with
the constants compiled into the C++ code, and still meet the response gates."""

import hashlib
import json
import re
import unittest
from pathlib import Path

import numpy as np
from scipy import signal

from ak4522_tools import adc_gates, dac_gates

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
        rate = dac_gates.DATASHEET_RATE  # the table's conditions [DS p.6]
        for check in dac_gates.digital_filter_checks(self.taps, 8 * rate, rate, check_delay=False):
            with self.subTest(check=check.name):
                self.assertTrue(check.passed, check)


class ReconstructionFilesTest(unittest.TestCase):
    def setUp(self):
        self.values = read_inc(ROOT / "src/dsp/reconstruction_coefficients.inc")
        self.report = json.loads((ROOT / "src/dsp/reconstruction_design.json").read_text())
        self.header = (ROOT / "src/dsp/reconstruction.hpp").read_text(encoding="utf-8")

    def test_counts_match_report_and_cpp(self):
        sections = int(re.search(r"sections = (\d+);", self.header).group(1))
        pipeline = int(re.search(r"samples = (\d+);", self.header).group(1))
        self.assertEqual(self.values.size, 5 * sections)
        self.assertEqual(pipeline, self.report["pipeline_samples_8x"])

    def test_checksum_matches_report(self):
        digest = hashlib.sha256(np.ascontiguousarray(self.values, dtype="<f8").tobytes()).hexdigest()
        self.assertEqual(digest, self.report["sha256_le_f8"])

    def test_sections_are_stable(self):
        for s in self.values.reshape(-1, 5):
            radius = np.max(np.abs(np.roots([1.0, s[3], s[4]])))
            self.assertLess(radius, 1.0)

    def test_cpp_latency_constant_matches_design(self):
        source = (ROOT / "src/core/dac_core.cpp").read_text(encoding="utf-8")
        constant = float(re.search(r"coreDelayFrames = ([0-9.]+);", source).group(1))
        self.assertEqual(constant, self.report["target_delay_frames"])
        self.assertAlmostEqual(self.report["measured_delay_frames"], constant, places=6)

    def test_committed_chain_meets_core_gates(self):
        interpolator = read_inc(ROOT / "src/dsp/interpolator_coefficients.inc")
        sos = np.array([[s[0], s[1], s[2], 1.0, s[3], s[4]] for s in self.values.reshape(-1, 5)])
        x = np.concatenate([np.zeros(self.report["pipeline_samples_8x"]), interpolator, np.zeros(6000)])
        h = signal.sosfilt(sos, x)
        rate = dac_gates.DATASHEET_RATE
        for check in dac_gates.core_checks(h, 8 * rate, rate):
            with self.subTest(check=check.name):
                self.assertTrue(check.passed, check)


class DeemphasisFilesTest(unittest.TestCase):
    RATES = (32000.0, 44100.0, 48000.0)

    def setUp(self):
        self.values = read_inc(ROOT / "src/dsp/deemphasis_coefficients.inc").reshape(-1, 3)
        self.report = json.loads((ROOT / "src/dsp/deemphasis_design.json").read_text())

    def test_three_sets_with_matching_checksum(self):
        self.assertEqual(self.values.shape, (3, 3))
        digest = hashlib.sha256(np.ascontiguousarray(self.values.ravel(), dtype="<f8").tobytes()).hexdigest()
        self.assertEqual(digest, self.report["sha256_le_f8"])

    def test_fit_against_analog_reference(self):
        # Independent evaluation: scipy freqz against the analog shelf (P-61).
        for (b0, b1, a1), fs in zip(self.values, self.RATES):
            with self.subTest(rate=fs):
                f = np.geomspace(20.0, min(20000.0, 0.45 * fs), 20000)
                _, h = signal.freqz([b0, b1], [1.0, a1], worN=f, fs=fs)
                s = 2j * np.pi * f
                ref = (1 + s * 15e-6) / (1 + s * 50e-6)
                error = np.max(np.abs(20 * np.log10(np.abs(h / ref))))
                self.assertLessEqual(error, 0.1)
                self.assertAlmostEqual((b0 + b1) / (1 + a1), 1.0, places=12)
                self.assertLess(abs(a1), 1.0)


if __name__ == "__main__":
    unittest.main()


class DecimatorFilesTest(unittest.TestCase):
    def setUp(self):
        self.taps = read_inc(ROOT / "src/dsp/decimator_coefficients.inc")
        self.report = json.loads((ROOT / "src/dsp/decimator_design.json").read_text())
        self.header = (ROOT / "src/dsp/decimator.hpp").read_text(encoding="utf-8")

    def test_length_and_pipeline_match_report_and_cpp(self):
        cpp_taps = int(re.search(r"taps = (\d+);", self.header).group(1))
        cpp_pipeline = int(re.search(r"pipelineSamples = (\d+);", self.header).group(1))
        self.assertEqual(self.taps.size, self.report["length"])
        self.assertEqual(self.taps.size, cpp_taps)
        self.assertEqual(cpp_pipeline, self.report["pipeline_samples_8x"])
        self.assertEqual(self.report["total_delay_samples_8x"], 234.5)

    def test_checksum_matches_report_and_header_comment(self):
        digest = hashlib.sha256(np.ascontiguousarray(self.taps, dtype="<f8").tobytes()).hexdigest()
        self.assertEqual(digest, self.report["sha256_le_f8"])
        inc = (ROOT / "src/dsp/decimator_coefficients.inc").read_text(encoding="utf-8")
        self.assertIn(digest, inc)

    def test_committed_coefficients_meet_the_gates(self):
        rate = adc_gates.DATASHEET_RATE
        pipeline = self.report["pipeline_samples_8x"] / 8
        for check in adc_gates.decimation_checks(self.taps, 8 * rate, rate, delay_frames=pipeline):
            with self.subTest(check=check.name):
                self.assertTrue(check.passed, check)


class AdcHighPassGatesTest(unittest.TestCase):
    def test_first_order_design_meets_the_gates_at_every_rate(self):
        source = (ROOT / "src/dsp/adc_highpass.hpp").read_text(encoding="utf-8")
        corner = float(re.search(r"cornerAt44100 = ([0-9.]+);", source).group(1))
        for rate in (16000.0, 32000.0, 44100.0, 48000.0):
            for check in adc_gates.highpass_checks(corner * rate / 44100.0, rate):
                with self.subTest(rate=rate, check=check.name):
                    self.assertTrue(check.passed, check)
