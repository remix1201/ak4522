"""ADC response gates from the evidence ledger (AD-03 to AD-07) and the
M7 policies.

Every limit carries its source: D (datasheet), I (inferred), or P (project
tolerance). Frequencies scale with the ADC rate (AD-07); the response
reference is 1 kHz. The datasheet gives these values at fs = 44.1 kHz
[DS p.6], so they are gates at that rate.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from . import response
from .dac_gates import Check, _check

REFERENCE_HZ = 1000.0  # AD-07
DATASHEET_RATE = 44100.0
NOMINAL_DELAY_FRAMES = 29.3  # AD-05


@dataclass(frozen=True)
class DecimationEdges:
    ripple_hz: float      # -0.005 dB, 19.76 kHz at 44.1 kHz (AD-03)
    point_02_hz: float    # -0.02 dB, 20.02 kHz
    point_06_hz: float    # -0.06 dB, 20.20 kHz
    half_hz: float        # -6.0 dB, 22.05 kHz
    stopband_hz: float    # 24.34 kHz (AD-04)


def decimation_edges(adc_rate: float) -> DecimationEdges:
    scale = adc_rate / DATASHEET_RATE
    return DecimationEdges(19760.0 * scale, 20020.0 * scale, 20200.0 * scale,
                           22050.0 * scale, 24340.0 * scale)


def decimation_checks(h: np.ndarray, fs_high: float, adc_rate: float,
                      delay_frames: float | None = None) -> list[Check]:
    """Gates for the decimation filter: h is its impulse response at fs_high.

    The stopband is checked from the stopband edge to the fs_high Nyquist
    frequency: everything there aliases into the output band on decimation.
    """
    e = decimation_edges(adc_rate)
    ref = REFERENCE_HZ
    checks = []

    low, high = response.band_extrema(h, fs_high, 0.0, e.ripple_hz, ref)
    ripple = max(abs(low.value_db), abs(high.value_db))
    checks.append(_check("passband_ripple_db", ripple, ripple <= 0.005,
                         "<= 0.005 dB from 0 to 19.76/44.1 fs, relative to 1 kHz", "AD-03, AD-04"))

    for name, freq, target, tolerance in (("point_0p02_db", e.point_02_hz, -0.02, 0.01),
                                          ("point_0p06_db", e.point_06_hz, -0.06, 0.01),
                                          ("half_rate_db", e.half_hz, -6.0, 0.1)):
        value = response.relative_db(h, fs_high, freq, ref)[0]
        checks.append(_check(name, value, abs(value - target) <= tolerance,
                             f"{target} +/- {tolerance} dB at {freq / adc_rate:.4f} fs", "AD-03, P-106"))

    _, stop_peak = response.band_extrema(h, fs_high, e.stopband_hz, fs_high / 2.0, ref,
                                         grid_points=65536)
    attenuation = -stop_peak.value_db
    checks.append(_check("stopband_attenuation_db", attenuation, attenuation >= 80.0,
                         ">= 80 dB from 24.34/44.1 fs to the input Nyquist frequency", "AD-04, I-AD02"))

    if delay_frames is not None:
        fit = response.passband_delay(h, fs_high, ref, e.ripple_hz)
        frames = fit.delay_samples / (fs_high / adc_rate) + delay_frames
        checks.append(_check("delay_adc_frames", frames, abs(frames - NOMINAL_DELAY_FRAMES) <= 0.1,
                             "29.3 +/- 0.1 frames including the pipeline, passband phase fit",
                             "AD-05, P-106"))
    return checks


def highpass_checks(cutoff_hz: float, adc_rate: float) -> list[Check]:
    """Gates for the first-order HPF (AD-06, I-AD03, P-107), evaluated on the
    bilinear design the C++ code uses."""
    k = np.tan(np.pi * cutoff_hz / adc_rate)
    b0 = 1.0 / (1.0 + k)
    a1 = (k - 1.0) / (k + 1.0)

    def db(f: float) -> float:
        z = np.exp(-2j * np.pi * f / adc_rate)
        hz = b0 * (1.0 - z) / (1.0 + a1 * z)
        ref = b0 * (1.0 - np.exp(-2j * np.pi * REFERENCE_HZ / adc_rate)) \
            / (1.0 + a1 * np.exp(-2j * np.pi * REFERENCE_HZ / adc_rate))
        return float(20.0 * np.log10(abs(hz) / abs(ref)))

    scale = adc_rate / DATASHEET_RATE
    return [
        _check("hpf_3db", db(0.9 * scale), abs(db(0.9 * scale) + 3.0) <= 0.02,
               "-3 +/- 0.02 dB at 0.9 Hz x fs/44.1 kHz", "AD-06, P-107"),
        _check("hpf_0p5db", db(2.7 * scale), abs(db(2.7 * scale) + 0.5) <= 0.05,
               "-0.5 +/- 0.05 dB at 2.7 Hz x fs/44.1 kHz (first order gives -0.46)", "AD-06, I-AD03, P-107"),
        _check("hpf_0p1db", db(6.0 * scale), abs(db(6.0 * scale) + 0.1) <= 0.02,
               "-0.1 +/- 0.02 dB at 6.0 Hz x fs/44.1 kHz", "AD-06, I-AD03, P-107"),
    ]
