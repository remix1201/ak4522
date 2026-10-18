# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

"""Check which noise spectra could meet both weighted S/N figures (DISC-08).

Each candidate spectrum is scaled to 100 dB A-weighted S/N over 10 Hz to
20 kHz (D-A03), then its CCIR-ARM S/N is computed with the average-responding
meter (D-A13, 96 dB typical). Candidates: white noise, a rising f^2 tilt as a
delta-sigma modulator would give, and a second-order peaking shape whose gain
is solved for 96 dB. Also prints the loopback figure two equal independent
converters at 96 dB would give, against the 94 dB typical (AD-A12).

    cd tools && uv run python noise_shape_study.py

Analysis only; the model keeps white noise (P-94).
"""

from __future__ import annotations

import sys

import numpy as np
from scipy.optimize import brentq

from ak4522_tools import metrics, weighting

F = np.linspace(metrics.BAND[0], metrics.BAND[1], 400_001)
A = 10.0 ** (weighting.a_weighting_db(F) / 10.0)
C = 10.0 ** (weighting.ccir_arm_db(F) / 10.0)
AVERAGE_READING_DB = 20.0 * np.log10(np.sqrt(np.pi) / 2.0)  # average-responding meter on Gaussian noise


def ccir_snr(shape: np.ndarray) -> float:
    """CCIR-ARM S/N of a spectrum shape scaled to 100 dB A-weighted."""
    density = metrics.FULL_SCALE_RMS**2 * 1e-10 / np.trapezoid(A * shape, F)
    power = np.trapezoid(C * shape, F) * density
    return float(10.0 * np.log10(metrics.FULL_SCALE_RMS**2 / power) - AVERAGE_READING_DB)


def peaking(f0: float, q: float, gain_db: float) -> np.ndarray:
    """Power response of an analog second-order peaking section."""
    a = 10.0 ** (gain_db / 40.0)
    w = F / f0
    return np.abs(((1 - w * w) + 1j * w * a / q) / ((1 - w * w) + 1j * w / (a * q))) ** 2


def main() -> int:
    print(f"white: CCIR-ARM {ccir_snr(np.ones_like(F)):.2f} dB")
    for fc in (3000, 10000, 30000):
        print(f"rising tilt 1 + (f/{fc})^2: CCIR-ARM {ccir_snr(1 + (F / fc) ** 2):.2f} dB")
    for f in (1000, 3000, 6300, 8000, 10000, 12500, 20000):
        print(f"CCIR-ARM minus A-weighting at {f} Hz: "
              f"{float(weighting.ccir_arm_db(f) - weighting.a_weighting_db(f)):+.2f} dB")
    for q in (0.5, 0.7, 1.0):
        for f0 in (5000, 7000, 9000):
            gain = brentq(lambda g: ccir_snr(peaking(f0, q, g)) - 96.0, 0.1, 40.0)
            print(f"peak at {f0} Hz, Q {q}: {gain:.1f} dB needed for 96 dB CCIR-ARM")
    print(f"loopback of two independent 96 dB converters: {96.0 - 10.0 * np.log10(2.0):.2f} dB (94 typical)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
