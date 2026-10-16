"""Frequency-response evaluation of sampled impulse responses.

All functions take an impulse response h sampled at rate fs (Hz) and
evaluate its discrete-time Fourier transform exactly at arbitrary
frequencies, so extrema can be refined beyond any grid spacing.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy.optimize import minimize_scalar


def dtft(h: np.ndarray, fs: float, freqs: np.ndarray | float) -> np.ndarray:
    """Exact DTFT of h at the given frequencies (Hz)."""
    h = np.asarray(h, dtype=float)
    f = np.atleast_1d(np.asarray(freqs, dtype=float))
    n = np.arange(h.size)
    # Evaluate in chunks to bound memory for long responses and dense grids.
    out = np.empty(f.size, dtype=complex)
    chunk = max(1, 2_000_000 // max(h.size, 1))
    for start in range(0, f.size, chunk):
        w = 2.0 * np.pi * f[start:start + chunk, None] / fs
        out[start:start + chunk] = np.exp(-1j * w * n[None, :]) @ h
    return out


def magnitude_db(h: np.ndarray, fs: float, freqs: np.ndarray | float) -> np.ndarray:
    """Magnitude in dB; exact zeros map to -inf."""
    mag = np.abs(dtft(h, fs, freqs))
    with np.errstate(divide="ignore"):
        return 20.0 * np.log10(mag)


def relative_db(h: np.ndarray, fs: float, freqs: np.ndarray | float, ref_hz: float) -> np.ndarray:
    """Magnitude in dB relative to the magnitude at ref_hz."""
    return magnitude_db(h, fs, freqs) - magnitude_db(h, fs, ref_hz)[0]


@dataclass(frozen=True)
class Extremum:
    freq_hz: float
    value_db: float


def band_extrema(h: np.ndarray, fs: float, f_lo: float, f_hi: float, ref_hz: float,
                 grid_points: int = 8192) -> tuple[Extremum, Extremum]:
    """Minimum and maximum relative magnitude over [f_lo, f_hi].

    Evaluates a dense grid, then refines every interior local extremum and
    both band edges with a bounded scalar search on the exact DTFT.
    """
    if not f_hi > f_lo:
        raise ValueError("empty band")
    grid = np.linspace(f_lo, f_hi, grid_points)
    values = relative_db(h, fs, grid, ref_hz)
    ref = magnitude_db(h, fs, ref_hz)[0]
    step = grid[1] - grid[0]

    def rel(f: float) -> float:
        return float(magnitude_db(h, fs, f)[0] - ref)

    candidates = [Extremum(f_lo, float(values[0])), Extremum(f_hi, float(values[-1]))]
    interior = np.arange(1, grid_points - 1)
    is_max = (values[interior] >= values[interior - 1]) & (values[interior] >= values[interior + 1])
    is_min = (values[interior] <= values[interior - 1]) & (values[interior] <= values[interior + 1])
    for index, sign in [(i, -1.0) for i in interior[is_max]] + [(i, 1.0) for i in interior[is_min]]:
        lo = max(f_lo, grid[index] - step)
        hi = min(f_hi, grid[index] + step)
        result = minimize_scalar(lambda f: sign * rel(f), bounds=(lo, hi), method="bounded",
                                 options={"xatol": step * 1e-6})
        candidates.append(Extremum(float(result.x), rel(float(result.x))))

    finite = [c for c in candidates if np.isfinite(c.value_db)]
    lowest = min(candidates, key=lambda c: c.value_db)
    highest = max(finite, key=lambda c: c.value_db) if finite else candidates[0]
    return lowest, highest


@dataclass(frozen=True)
class DelayFit:
    delay_samples: float       # at the impulse-response rate
    max_deviation_samples: float  # largest phase residual, expressed as delay
    peak_index: int


def passband_delay(h: np.ndarray, fs: float, f_lo: float, f_hi: float,
                   points: int = 2048) -> DelayFit:
    """Delay from a least-squares fit of unwrapped phase over [f_lo, f_hi].

    Phase is modeled as -2*pi*f*tau + c. The residual is reported as the
    largest equivalent delay deviation, which measures dispersion.
    """
    f = np.linspace(f_lo, f_hi, points)
    phase = np.unwrap(np.angle(dtft(h, fs, f)))
    slope, intercept = np.polyfit(f, phase, 1)
    tau_seconds = -slope / (2.0 * np.pi)
    residual = phase - (slope * f + intercept)
    # Convert residual phase to an equivalent delay at each frequency.
    with np.errstate(divide="ignore", invalid="ignore"):
        deviation = np.where(f > 0, residual / (2.0 * np.pi * f), 0.0)
    return DelayFit(
        delay_samples=float(tau_seconds * fs),
        max_deviation_samples=float(np.max(np.abs(deviation)) * fs),
        peak_index=int(np.argmax(np.abs(h))),
    )
