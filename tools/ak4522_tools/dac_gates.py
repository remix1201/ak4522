"""DAC response gates from AGENTS.md section 13.2 and the evidence ledger.

Every limit carries its source: D (datasheet), I (inferred), or P (project
tolerance). Band edges scale with the DAC rate (D-F08, I-F01 to I-F03).
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from . import response

REFERENCE_HZ = 1000.0  # D-F09, at 44.1 kHz


def reference_hz(dac_rate: float) -> float:
    """Response reference frequency. [DS] states 1 kHz at 44.1 kHz; the
    digital filter has one coefficient set, so its response is fixed in
    normalized frequency and the reference scales with fs like the band
    edges (I-F07)."""
    return REFERENCE_HZ * dac_rate / 44100.0


@dataclass(frozen=True)
class BandEdges:
    passband_hz: float   # -0.06 dB edge, 20/44.1 x fs (I-F01)
    half_hz: float       # -6 dB point, 0.5 x fs (I-F02)
    stopband_hz: float   # 24.1/44.1 x fs (I-F03)


def band_edges(dac_rate: float) -> BandEdges:
    return BandEdges(
        passband_hz=20.0 / 44.1 * dac_rate,
        half_hz=0.5 * dac_rate,
        stopband_hz=24.1 / 44.1 * dac_rate,
    )


@dataclass(frozen=True)
class Check:
    name: str
    value: float
    limit: str
    source: str
    passed: bool


def _check(name: str, value: float, ok: bool, limit: str, source: str) -> Check:
    return Check(name, float(value), limit, source, bool(ok))


def digital_filter_checks(h: np.ndarray, fs_high: float, dac_rate: float,
                          nominal_delay_frames: float = 14.7,
                          delay_tolerance_frames: float = 0.1,
                          check_delay: bool = True) -> list[Check]:
    """Gate 1 (digital response) and gate 3 (delay) of AGENTS.md 13.2.

    h is the impulse response at fs_high for a unit impulse at the DAC rate.
    The stopband is checked from the stopband edge to the high-rate Nyquist
    frequency, which is the full implemented interval.
    """
    edges = band_edges(dac_rate)
    ref = reference_hz(dac_rate)
    checks = []

    low, high = response.band_extrema(h, fs_high, 0.0, edges.passband_hz, ref)
    ripple = max(abs(low.value_db), abs(high.value_db))
    checks.append(_check("passband_ripple_db", ripple, ripple <= 0.06,
                         "<= 0.06 dB from 0 to 20/44.1 fs, relative to 1/44.1 fs", "D-F04, I-F07"))

    edge_db = response.relative_db(h, fs_high, edges.passband_hz, ref)[0]
    checks.append(_check("passband_edge_db", edge_db, abs(edge_db + 0.06) <= 0.01,
                         "-0.06 +/- 0.01 dB at 20/44.1 fs", "D-F01, P-80"))

    half_db = response.relative_db(h, fs_high, edges.half_hz, ref)[0]
    checks.append(_check("half_rate_db", half_db, abs(half_db + 6.0) <= 0.1,
                         "-6 +/- 0.1 dB at 0.5 fs", "D-F02, P-80"))

    _, stop_peak = response.band_extrema(h, fs_high, edges.stopband_hz, fs_high / 2.0,
                                         ref, grid_points=32768)
    attenuation = -stop_peak.value_db
    checks.append(_check("stopband_attenuation_db", attenuation, attenuation >= 43.0,
                         ">= 43 dB from 24.1/44.1 fs to the high-rate Nyquist frequency",
                         "D-F05, I-F04"))

    if check_delay:
        fit = response.passband_delay(h, fs_high, ref, edges.passband_hz)
        oversampling = fs_high / dac_rate
        frames = fit.delay_samples / oversampling
        checks.append(_check("delay_dac_frames", frames,
                             abs(frames - nominal_delay_frames) <= delay_tolerance_frames,
                             f"{nominal_delay_frames} +/- {delay_tolerance_frames} DAC frames, "
                             "passband phase fit", "D-F06, P-83"))
    return checks


def combined_response_check(h: np.ndarray, fs_high: float, dac_rate: float) -> Check:
    """Gate 2: digital plus analog response within +/-0.2 dB over 0 to 20 kHz
    at 44.1 kHz (D-F07). At other rates the band scales with fs (P-81)."""
    edges = band_edges(dac_rate)
    low, high = response.band_extrema(h, fs_high, 0.0, edges.passband_hz, reference_hz(dac_rate))
    deviation = max(abs(low.value_db), abs(high.value_db))
    return _check("combined_response_db", deviation, deviation <= 0.2,
                  "<= 0.2 dB from 0 to 20/44.1 fs, relative to 1/44.1 fs", "D-F07, I-F07, P-81, P-82")


def core_checks(h: np.ndarray, fs_high: float, dac_rate: float,
                nominal_delay_frames: float = 14.7,
                delay_tolerance_frames: float = 0.1) -> list[Check]:
    """Gates for the whole linear chip model, input register to analog
    output: combined response (gate 2) and delay (gate 3, D-F06, D-F10)."""
    edges = band_edges(dac_rate)
    fit = response.passband_delay(h, fs_high, reference_hz(dac_rate), edges.passband_hz)
    frames = fit.delay_samples / (fs_high / dac_rate)
    return [
        combined_response_check(h, fs_high, dac_rate),
        _check("delay_dac_frames", frames,
               abs(frames - nominal_delay_frames) <= delay_tolerance_frames,
               f"{nominal_delay_frames} +/- {delay_tolerance_frames} DAC frames, passband phase fit",
               "D-F06, D-F10, P-83"),
    ]


def report(checks: list[Check], **context) -> dict:
    """JSON-ready report with the context needed to interpret the results."""
    return {
        "context": context,
        "checks": [asdict(c) for c in checks],
        "passed": all(c.passed for c in checks),
    }
