# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

"""Weighting curves against their standards' published tables, and metrics
against synthetic signals with known answers."""

import unittest

import numpy as np

from ak4522_tools import metrics, weighting

# IEC 61672-1:2013 Table 3, A-weighting in dB, keyed by band index n: the
# table lists nominal labels (31.5 Hz, 16 kHz, ...) for the exact base-ten
# frequencies 1000 * 10^(n / 10), at which the values are computed.
A_TABLE = {-20: -70.4, -17: -50.5, -15: -39.4, -12: -26.2, -9: -16.1, -6: -8.6, -3: -3.2,
           0: 0.0, 3: 1.2, 6: 1.0, 9: -1.1, 12: -6.6, 13: -9.3}

# ITU-R BS.468-4 Table 1 (dB).
ITU468_TABLE = {31.5: -29.9, 63: -23.9, 100: -19.8, 200: -13.8, 400: -7.8, 800: -1.9, 1000: 0.0,
                2000: 5.6, 3150: 9.0, 4000: 10.5, 5000: 11.7, 6300: 12.2, 7100: 12.0, 8000: 11.4,
                9000: 10.1, 10000: 8.1, 12500: 0.0, 14000: -5.3, 16000: -11.7, 20000: -22.2}


class CurveTest(unittest.TestCase):
    def test_a_weighting_matches_iec_61672(self):
        for n, expected in A_TABLE.items():
            f = 1000.0 * 10.0 ** (n / 10.0)
            with self.subTest(f=f):
                # Table values are rounded to 0.1 dB.
                self.assertAlmostEqual(float(weighting.a_weighting_db(f)), expected, delta=0.05)

    def test_itu468_matches_bs468(self):
        for f, expected in ITU468_TABLE.items():
            with self.subTest(f=f):
                self.assertAlmostEqual(float(weighting.itu468_db(f)), expected, delta=0.15)

    def test_ccir_arm_is_zero_at_2_khz(self):
        self.assertAlmostEqual(float(weighting.ccir_arm_db(2000.0)), 0.0, places=12)
        self.assertAlmostEqual(float(weighting.ccir_arm_db(1000.0)), -5.6, delta=0.1)


class ApplyTest(unittest.TestCase):
    RATE = 44100.0

    def coherent_sine(self, cycles, n=1 << 16, amplitude=1.0):
        t = np.arange(n)
        return amplitude * np.sin(2 * np.pi * cycles * t / n), cycles * self.RATE / n

    def test_weighting_scales_a_sine_by_the_curve(self):
        for cycles in (300, 1486, 6000, 20000):
            x, f = self.coherent_sine(cycles)
            y = weighting.apply(x, self.RATE, weighting.a_weighting_db)
            expected = 10 ** (float(weighting.a_weighting_db(f)) / 20) / np.sqrt(2)
            self.assertAlmostEqual(weighting.rms(y), expected, places=9)

    def test_band_limits_outside_content(self):
        x, f = self.coherent_sine(32000)  # about 21.5 kHz, outside 10 Hz to 20 kHz
        self.assertGreater(f, 20000)
        # Only FFT rounding remains, more than 200 dB down.
        self.assertLess(weighting.rms(weighting.apply(x, self.RATE, None)), 1e-10)

    def test_average_responding_reads_rms_for_a_sine(self):
        x, _ = self.coherent_sine(1486)
        self.assertAlmostEqual(weighting.average_responding(x), 1 / np.sqrt(2), places=4)

    def test_average_responding_reads_low_for_gaussian_noise(self):
        # For Gaussian noise an average-responding meter reads sqrt(pi)/2 of RMS.
        # The estimate's standard deviation with 2^20 samples is about 7e-4.
        x = np.random.default_rng(1).standard_normal(1 << 20)
        self.assertAlmostEqual(weighting.average_responding(x) / weighting.rms(x),
                               np.sqrt(np.pi) / 2, delta=3e-3)


class MetricsTest(unittest.TestCase):
    RATE = 44100.0
    N = 1 << 18

    def test_snr_a_of_white_noise_matches_the_integral(self):
        sigma = 1e-5
        noise = sigma * np.random.default_rng(2).standard_normal(self.N)
        # White noise: weighted power = sigma^2 * mean of |A|^2 over the band / Nyquist share.
        f = np.linspace(10, 20000, 200001)
        share = np.trapezoid(10 ** (weighting.a_weighting_db(f) / 10), f) / (self.RATE / 2)
        # Reference: a full-scale sine (amplitude 1) has RMS 1/sqrt(2), stated
        # here independently of the module's constant.
        expected = 20 * np.log10(np.sqrt(0.5) / (sigma * np.sqrt(share)))
        self.assertAlmostEqual(metrics.snr_a(noise, self.RATE), expected, delta=0.05)

    def test_sinad_of_tone_with_known_harmonics(self):
        n = np.arange(self.N)
        f = 1000.0
        tone = 0.9 * np.sin(2 * np.pi * f * n / self.RATE)
        h2 = 0.9e-4 * np.sin(2 * np.pi * 2 * f * n / self.RATE)
        h3 = 0.9e-4 * np.sin(2 * np.pi * 3 * f * n / self.RATE)
        expected = 20 * np.log10(0.9 / np.sqrt(2 * 0.9e-4 ** 2))  # 76.99 dB
        self.assertAlmostEqual(metrics.sinad(tone + h2 + h3, self.RATE, f), expected, delta=0.02)

    def test_dynamic_range_removes_the_tone(self):
        n = np.arange(self.N)
        tone = 1e-3 * np.sin(2 * np.pi * 1000.0 * n / self.RATE)  # -60 dBFS
        sigma = 1e-5
        noise = sigma * np.random.default_rng(3).standard_normal(self.N)
        self.assertAlmostEqual(metrics.dynamic_range(tone + noise, self.RATE),
                               metrics.snr_a(noise, self.RATE), delta=0.05)


if __name__ == "__main__":
    unittest.main()
