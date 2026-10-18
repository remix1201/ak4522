# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

"""Converter performance metrics with the conditions of [DS] p.5:
measurement band 10 Hz to 20 kHz, 1 kHz signal, levels relative to a
full-scale sine (D-A01 to D-A03, D-A11 to D-A13; AGENTS.md 13.2 gates 4 to 6).

Signals are normalized: full scale is 1.0, so a full-scale sine has RMS
1/sqrt(2).
"""

from __future__ import annotations

import numpy as np

from . import weighting

FULL_SCALE_RMS = 1.0 / np.sqrt(2.0)
BAND = (10.0, 20000.0)


def remove_tone(x: np.ndarray, freq: float, rate: float) -> tuple[np.ndarray, float]:
    """Least-squares removal of a sine at `freq` and DC. Returns the residual
    and the tone's RMS. This is the documented notch for DR and SINAD."""
    n = np.arange(x.size)
    w = 2.0 * np.pi * freq / rate
    basis = np.column_stack([np.ones(x.size), np.cos(w * n), np.sin(w * n)])
    coef, *_ = np.linalg.lstsq(basis, x, rcond=None)
    residual = x - basis @ coef
    return residual, float(np.hypot(coef[1], coef[2]) / np.sqrt(2.0))


def snr_a(zero_input_output: np.ndarray, rate: float) -> float:
    """S/N, A-weighted: full-scale sine RMS over weighted noise RMS (gate 4)."""
    noise = weighting.apply(zero_input_output, rate, weighting.a_weighting_db, BAND)
    return 20.0 * np.log10(FULL_SCALE_RMS / weighting.rms(noise))


def snr_ccir_arm(zero_input_output: np.ndarray, rate: float) -> float:
    """S/N with CCIR-ARM weighting and an average-responding meter (D-A13)."""
    noise = weighting.apply(zero_input_output, rate, weighting.ccir_arm_db, BAND)
    return 20.0 * np.log10(FULL_SCALE_RMS / weighting.average_responding(noise))


def dynamic_range(output: np.ndarray, rate: float, freq: float = 1000.0) -> float:
    """DR: -60 dBFS tone removed, A-weighted residual, re full scale (gate 5)."""
    residual, _ = remove_tone(output, freq, rate)
    weighted = weighting.apply(residual, rate, weighting.a_weighting_db, BAND)
    return 20.0 * np.log10(FULL_SCALE_RMS / weighting.rms(weighted))


def sinad(output: np.ndarray, rate: float, freq: float = 1000.0) -> float:
    """S/(N+D): tone RMS over unweighted residual in 10 Hz to 20 kHz (gate 6)."""
    residual, tone = remove_tone(output, freq, rate)
    band_limited = weighting.apply(residual, rate, None, BAND)
    return 20.0 * np.log10(tone / weighting.rms(band_limited))
