# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

"""Noise weighting curves and their application to sampled signals.

A-weighting follows IEC 61672-1; the ITU-R 468 curve follows ITU-R BS.468-4.
CCIR-ARM (Dolby) is the 468 curve renormalized to 0 dB at 2 kHz and read
with an average-responding meter calibrated for sine RMS. These are the
weightings named in [DS] p.5 notes 5, 6, and 9 (D-A02, D-A03, D-A13).
"""

from __future__ import annotations

import numpy as np


def a_weighting_db(f: np.ndarray | float) -> np.ndarray:
    """IEC 61672-1 A-weighting in dB (0 dB at 1 kHz)."""
    f = np.asarray(f, dtype=float)
    f2 = f * f
    ra = (12194.0**2 * f2 * f2) / (
        (f2 + 20.6**2) * np.sqrt((f2 + 107.7**2) * (f2 + 737.9**2)) * (f2 + 12194.0**2))
    with np.errstate(divide="ignore"):
        return 20.0 * np.log10(ra) + 2.0


def itu468_db(f: np.ndarray | float) -> np.ndarray:
    """ITU-R BS.468-4 weighting in dB (0 dB at 1 kHz, +12.2 dB peak near 6.3 kHz)."""
    f = np.asarray(f, dtype=float)
    h1 = (-4.737338981378384e-24 * f**6 + 2.043828333606125e-15 * f**4
          - 1.363894795463638e-7 * f**2 + 1.0)
    h2 = 1.306612257412824e-19 * f**5 - 2.118150887518656e-11 * f**3 + 5.559488023498642e-4 * f
    r = 1.246332637532143e-4 * f / np.sqrt(h1 * h1 + h2 * h2)
    with np.errstate(divide="ignore"):
        return 18.2 + 20.0 * np.log10(r)


def ccir_arm_db(f: np.ndarray | float) -> np.ndarray:
    """CCIR-ARM: the 468 curve shifted to 0 dB at 2 kHz (-5.6 dB at 1 kHz)."""
    return itu468_db(f) - float(itu468_db(2000.0))


def apply(signal: np.ndarray, rate: float, curve_db, band: tuple[float, float] = (10.0, 20000.0)) -> np.ndarray:
    """Weights a signal in the frequency domain and limits it to `band`.

    The signal is treated as one period (no window), which is exact for the
    steady noise and coherent tones these measurements use.
    """
    x = np.asarray(signal, dtype=float)
    spectrum = np.fft.rfft(x)
    f = np.fft.rfftfreq(x.size, 1.0 / rate)
    gain = np.zeros_like(f)
    inside = (f >= band[0]) & (f <= band[1])
    if curve_db is None:
        gain[inside] = 1.0
    else:
        gain[inside] = 10.0 ** (curve_db(f[inside]) / 20.0)
    return np.fft.irfft(spectrum * gain, n=x.size)


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(x))))


def average_responding(x: np.ndarray) -> float:
    """Average-responding meter reading, calibrated to read RMS for a sine."""
    return float(np.mean(np.abs(x)) * np.pi / (2.0 * np.sqrt(2.0)))
