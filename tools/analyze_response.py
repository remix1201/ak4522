# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

"""Measure an impulse response against the DAC response gates.

The input is an impulse response at the high (oversampled) rate for a unit
impulse at the DAC rate, as a .npy file or a text file with one value per
line. Prints a JSON report; exit status 1 if any check fails.

    uv run python analyze_response.py response.npy --fs-high 352800 --dac-rate 44100
    uv run python analyze_response.py response.npy --fs-high 352800 --dac-rate 44100 \\
        --combined --plot response.png
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

from ak4522_tools import dac_gates, response


def load(path: Path) -> np.ndarray:
    if path.suffix == ".npy":
        return np.load(path).astype(float).ravel()
    return np.loadtxt(path, dtype=float).ravel()


def plot(h: np.ndarray, fs_high: float, dac_rate: float, path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    edges = dac_gates.band_edges(dac_rate)
    f = np.linspace(1.0, fs_high / 2.0, 8192)
    ref = dac_gates.REFERENCE_HZ
    db = response.relative_db(h, fs_high, f, ref)
    fig, (full, passband) = plt.subplots(2, 1, figsize=(8, 7))
    full.plot(f / 1e3, db)
    full.axhline(-43.0, linestyle="--", linewidth=0.8)
    full.axvline(edges.stopband_hz / 1e3, linestyle=":", linewidth=0.8)
    full.set_ylim(-140, 5)
    full.set_xlabel("kHz")
    full.set_ylabel("dB re 1 kHz")
    full.set_title("Full high-rate band")
    fp = np.linspace(1.0, edges.passband_hz, 4096)
    passband.plot(fp / 1e3, response.relative_db(h, fs_high, fp, ref))
    for level in (-0.06, 0.06):
        passband.axhline(level, linestyle="--", linewidth=0.8)
    passband.set_xlabel("kHz")
    passband.set_ylabel("dB re 1 kHz")
    passband.set_title("Passband")
    fig.tight_layout()
    fig.savefig(path, dpi=120)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("impulse_response", type=Path)
    parser.add_argument("--fs-high", type=float, required=True, help="impulse-response rate, Hz")
    parser.add_argument("--dac-rate", type=float, required=True, help="DAC sample rate, Hz")
    parser.add_argument("--combined", action="store_true",
                        help="also check the digital plus analog response (gate 2)")
    parser.add_argument("--core", action="store_true",
                        help="whole linear chip model: combined response and delay only")
    parser.add_argument("--no-delay", action="store_true", help="skip the delay check")
    parser.add_argument("--plot", type=Path, help="write a response plot (PNG)")
    args = parser.parse_args(argv)

    h = load(args.impulse_response)
    if args.core:
        checks = dac_gates.core_checks(h, args.fs_high, args.dac_rate)
    else:
        checks = dac_gates.digital_filter_checks(h, args.fs_high, args.dac_rate,
                                                 check_delay=not args.no_delay)
    if args.combined and not args.core:
        checks.append(dac_gates.combined_response_check(h, args.fs_high, args.dac_rate))
    result = dac_gates.report(checks, source=str(args.impulse_response), taps=int(h.size),
                              fs_high=args.fs_high, dac_rate=args.dac_rate)
    print(json.dumps(result, indent=2))
    if args.dac_rate != dac_gates.DATASHEET_RATE:
        print("note: the datasheet gives these values at 44.1 kHz only; results are informational",
              file=sys.stderr)
        return 0
    if args.plot:
        plot(h, args.fs_high, args.dac_rate, args.plot)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
