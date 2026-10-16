"""Tests for the response analysis. Each result is compared with an
independent calculation: analytic responses, exact delays, or an FFT on a
very dense grid instead of the direct DTFT used by the code under test."""

import unittest

import numpy as np
from scipy import signal

from ak4522_tools import dac_gates, response

FS_HIGH = 352800.0  # 8 x 44.1 kHz
DAC_RATE = 44100.0


def impulse(length: int, at: int) -> np.ndarray:
    h = np.zeros(length)
    h[at] = 1.0
    return h


class DtftTest(unittest.TestCase):
    def test_matches_fft_at_bin_frequencies(self):
        rng = np.random.default_rng(1)
        h = rng.standard_normal(97)
        n_fft = 1024
        bins = np.arange(n_fft // 2 + 1)
        expected = np.fft.rfft(h, n_fft)
        actual = response.dtft(h, FS_HIGH, bins * FS_HIGH / n_fft)
        np.testing.assert_allclose(actual, expected, rtol=1e-10, atol=1e-10)

    def test_first_order_iir_matches_analytic(self):
        a = 0.9
        h = a ** np.arange(4000)  # truncated far below double precision
        f = np.linspace(0, FS_HIGH / 2, 50)
        w = 2 * np.pi * f / FS_HIGH
        expected = 1.0 / (1.0 - a * np.exp(-1j * w))
        np.testing.assert_allclose(response.dtft(h, FS_HIGH, f), expected, rtol=1e-9)


class ExtremaTest(unittest.TestCase):
    def setUp(self):
        # Equiripple lowpass with a visible passband ripple.
        self.h = signal.remez(101, [0, 0.18, 0.25, 0.5], [1, 0], fs=1.0)
        self.fs = 1.0

    def dense_fft_extrema(self, f_lo, f_hi, ref):
        n_fft = 1 << 22
        spectrum = np.abs(np.fft.rfft(self.h, n_fft))
        f = np.arange(spectrum.size) * self.fs / n_fft
        ref_db = 20 * np.log10(abs(response.dtft(self.h, self.fs, ref)[0]))
        band = (f >= f_lo) & (f <= f_hi)
        db = 20 * np.log10(spectrum[band]) - ref_db
        return db.min(), db.max()

    def test_passband_extrema_match_dense_fft(self):
        low, high = response.band_extrema(self.h, self.fs, 0.0, 0.18, 0.01)
        fft_low, fft_high = self.dense_fft_extrema(0.0, 0.18, 0.01)
        self.assertAlmostEqual(low.value_db, fft_low, places=6)
        self.assertAlmostEqual(high.value_db, fft_high, places=6)

    def test_refinement_beats_coarse_grid(self):
        # A coarse grid misses the true peak; refinement must recover it.
        _, refined = response.band_extrema(self.h, self.fs, 0.0, 0.18, 0.01, grid_points=64)
        _, fft_high = self.dense_fft_extrema(0.0, 0.18, 0.01)
        self.assertAlmostEqual(refined.value_db, fft_high, places=6)

    def test_stopband_peak_matches_dense_fft(self):
        _, high = response.band_extrema(self.h, self.fs, 0.25, 0.5, 0.01)
        _, fft_high = self.dense_fft_extrema(0.25, 0.5, 0.01)
        self.assertAlmostEqual(high.value_db, fft_high, places=5)


class DelayTest(unittest.TestCase):
    def test_pure_delay_is_exact(self):
        for at in (0, 1, 37, 118):
            fit = response.passband_delay(impulse(256, at), FS_HIGH, 1000.0, 20000.0)
            self.assertAlmostEqual(fit.delay_samples, at, places=9)
            self.assertLess(fit.max_deviation_samples, 1e-9)
            self.assertEqual(fit.peak_index, at)

    def test_symmetric_fir_delay_is_half_length(self):
        for taps in (31, 64, 235):
            h = signal.firwin(taps, 30000.0, fs=FS_HIGH)
            fit = response.passband_delay(h, FS_HIGH, 1000.0, 20000.0)
            self.assertAlmostEqual(fit.delay_samples, (taps - 1) / 2, places=6)

    def test_dispersion_reported_for_iir(self):
        b, a = signal.butter(2, 30000.0, fs=FS_HIGH)
        h = signal.lfilter(b, a, impulse(4000, 0))
        fit = response.passband_delay(h, FS_HIGH, 1000.0, 20000.0)
        self.assertGreater(fit.max_deviation_samples, 0.01)
        # Low-frequency group delay of a 2nd-order Butterworth: sqrt(2)/wc,
        # with the prewarped analog cutoff of the bilinear design.
        wc = 2 * FS_HIGH * np.tan(np.pi * 30000.0 / FS_HIGH)
        self.assertAlmostEqual(fit.delay_samples / FS_HIGH, np.sqrt(2) / wc, delta=2e-6)


class GateTest(unittest.TestCase):
    def test_reference_scales_with_rate(self):
        self.assertEqual(dac_gates.reference_hz(44100.0), 1000.0)
        self.assertAlmostEqual(dac_gates.reference_hz(48000.0), 1088.4354, places=3)
        self.assertAlmostEqual(dac_gates.reference_hz(32000.0), 725.6236, places=3)

    def test_band_edges_scale_with_rate(self):
        edges = dac_gates.band_edges(48000.0)
        self.assertAlmostEqual(edges.passband_hz, 21768.7075, places=3)
        self.assertAlmostEqual(edges.half_hz, 24000.0)
        self.assertAlmostEqual(edges.stopband_hz, 26231.2925, places=3)
        self.assertAlmostEqual(dac_gates.band_edges(44100.0).passband_hz, 20000.0)

    def test_pure_delay_fails_filter_gates(self):
        checks = {c.name: c for c in dac_gates.digital_filter_checks(
            impulse(300, 118), FS_HIGH, DAC_RATE)}
        self.assertTrue(checks["passband_ripple_db"].passed)
        self.assertFalse(checks["passband_edge_db"].passed)   # 0 dB, not -0.06
        self.assertFalse(checks["half_rate_db"].passed)       # 0 dB, not -6
        self.assertFalse(checks["stopband_attenuation_db"].passed)
        self.assertAlmostEqual(checks["delay_dac_frames"].value, 118 / 8)
        self.assertTrue(checks["delay_dac_frames"].passed)    # 14.75 is within 0.1

    def test_delay_outside_tolerance_fails(self):
        checks = {c.name: c for c in dac_gates.digital_filter_checks(
            impulse(300, 112), FS_HIGH, DAC_RATE)}
        self.assertAlmostEqual(checks["delay_dac_frames"].value, 14.0)
        self.assertFalse(checks["delay_dac_frames"].passed)

    @staticmethod
    def fft_relative_db(h, f_lo, f_hi):
        # Independent of the code under test: dense FFT, nearest bin to 1 kHz.
        n_fft = 1 << 22
        spectrum = 20 * np.log10(np.abs(np.fft.rfft(h, n_fft)))
        f = np.arange(spectrum.size) * FS_HIGH / n_fft
        ref = spectrum[int(round(1000.0 * n_fft / FS_HIGH))]
        band = (f >= f_lo) & (f <= f_hi)
        return spectrum[band] - ref

    def test_ripple_between_limits_fails(self):
        # An echo gives passband ripple of a few tenths of a dB: above the
        # 0.06 dB limit, below anything a loosened limit might allow.
        h = impulse(64, 0) + 0.02 * impulse(64, 40)
        check = {c.name: c for c in dac_gates.digital_filter_checks(
            h, FS_HIGH, DAC_RATE, check_delay=False)}["passband_ripple_db"]
        expected = np.max(np.abs(self.fft_relative_db(h, 0.0, 20000.0)))
        self.assertAlmostEqual(check.value, expected, places=3)
        self.assertGreater(check.value, 0.06)
        self.assertLess(check.value, 0.6)
        self.assertFalse(check.passed)

    def test_ripple_within_limit_passes(self):
        h = impulse(64, 0) + 0.002 * impulse(64, 40)
        check = {c.name: c for c in dac_gates.digital_filter_checks(
            h, FS_HIGH, DAC_RATE, check_delay=False)}["passband_ripple_db"]
        self.assertLess(check.value, 0.06)
        self.assertTrue(check.passed)

    def test_partial_stopband_attenuation_fails(self):
        # A short lowpass attenuates the stopband by tens of dB, not 43.
        h = signal.firwin(15, 22050.0, fs=FS_HIGH)
        check = {c.name: c for c in dac_gates.digital_filter_checks(
            h, FS_HIGH, DAC_RATE, check_delay=False)}["stopband_attenuation_db"]
        expected = -np.max(self.fft_relative_db(h, 24100.0 / 44100.0 * DAC_RATE, FS_HIGH / 2))
        self.assertAlmostEqual(check.value, expected, places=2)
        self.assertGreater(check.value, 1.0)
        self.assertLess(check.value, 43.0)
        self.assertFalse(check.passed)

    def test_strong_stopband_attenuation_passes(self):
        h = signal.remez(301, [0, 20000, 24100, FS_HIGH / 2], [1, 0], weight=[1, 10], fs=FS_HIGH)
        check = {c.name: c for c in dac_gates.digital_filter_checks(
            h, FS_HIGH, DAC_RATE, check_delay=False)}["stopband_attenuation_db"]
        self.assertGreater(check.value, 43.0)
        self.assertTrue(check.passed)

    def test_combined_check(self):
        self.assertTrue(dac_gates.combined_response_check(impulse(64, 3), FS_HIGH, DAC_RATE).passed)
        droop = np.array([0.25, 0.5, 0.25])  # about -1.5 dB at 20 kHz at this rate
        result = dac_gates.combined_response_check(droop, FS_HIGH, DAC_RATE)
        self.assertFalse(result.passed)
        expected = -20 * np.log10(np.cos(np.pi * 20000 / FS_HIGH) ** 2 / np.cos(np.pi * 1000 / FS_HIGH) ** 2)
        self.assertAlmostEqual(result.value, expected, places=6)


if __name__ == "__main__":
    unittest.main()
