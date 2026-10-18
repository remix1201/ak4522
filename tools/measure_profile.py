# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

"""Measure a capture from ak4522_capture with the validated metrics.

    uv run python measure_profile.py capture.f64 --rate 44100 --metric snr_a
    uv run python measure_profile.py capture.f64 --rate 44100 --metric snr_a --expect 100 --tolerance 1

Metrics: snr_a, snr_ccir_arm, dynamic_range, sinad (the last two at --tone,
default 1 kHz). With --expect, exits 1 if the value is outside the tolerance.
"""

from __future__ import annotations

import argparse
import json
import sys

import numpy as np

from ak4522_tools import metrics


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture")
    parser.add_argument("--rate", type=float, required=True)
    parser.add_argument("--metric", required=True,
                        choices=["snr_a", "snr_ccir_arm", "dynamic_range", "sinad"])
    parser.add_argument("--tone", type=float, default=1000.0)
    parser.add_argument("--expect", type=float)
    parser.add_argument("--tolerance", type=float, default=1.0)
    args = parser.parse_args(argv)

    x = np.fromfile(args.capture, dtype="<f8")
    if args.metric in ("snr_a", "snr_ccir_arm"):
        value = getattr(metrics, args.metric)(x, args.rate)
    else:
        value = getattr(metrics, args.metric)(x, args.rate, args.tone)
    result = {"metric": args.metric, "value_db": round(float(value), 4), "seconds": x.size / args.rate}
    if args.expect is not None:
        result.update(expect=args.expect, tolerance=args.tolerance,
                      passed=bool(abs(value - args.expect) <= args.tolerance))
    print(json.dumps(result))
    return 0 if result.get("passed", True) else 1


if __name__ == "__main__":
    sys.exit(main())
