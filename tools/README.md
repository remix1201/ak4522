# Tools

Offline coefficient design, independent reference vectors, and response analysis. Nothing here runs during the C++ build or on the audio thread.

The environment is pinned by `pyproject.toml`, `.python-version`, and `uv.lock`. Install [uv](https://docs.astral.sh/uv/) and run scripts from this directory:

```sh
uv sync
uv run python <script>.py
```

## Filter design

Each script writes coefficients under `src/` with a JSON design report (parameters, tool versions, checksum, and measured response):

- `design_interpolator.py`: the DAC's 8x interpolation filter (P-87, P-88).
- `design_reconstruction.py`: the DAC's analog reconstruction sections and pipeline delay (P-89).
- `design_deemphasis.py`: the three de-emphasis sets (P-60, P-61).
- `design_noise.py`: the NominalAnalog noise density (P-94).
- `design_decimator.py`: the ADC's 8x decimation filter (P-105).

## Response analysis

`ak4522_tools/` measures responses against the gates in AGENTS.md section 13.2. It is the independent reference: filters are designed here, implemented in C++, and the C++ output is measured here again.

- `response.py`: exact DTFT at any frequency; band minimum and maximum from a dense grid with every local extremum refined by a bounded search; delay from a least-squares fit of passband phase, with dispersion.
- `dac_gates.py`: band edges scaled with the DAC rate, and the checks for passband ripple, passband edge, -6 dB point, stopband attenuation to the high-rate Nyquist frequency, delay, and combined response, each labeled with its evidence or policy ID.
- `adc_gates.py`: the ADC's decimation and high-pass gates (AD-03 to AD-06, P-106, P-107).
- `weighting.py` and `metrics.py`: A-weighting, ITU-R 468, and CCIR-ARM curves, and the S/N, dynamic-range, and S/(N+D) measurements.
- `analyze_response.py`: command line. Input is an impulse response at the high rate for a unit impulse at the DAC rate (`.npy` or one value per line). Output is a JSON report; `--plot` writes a PNG.

```sh
cd tools
uv run python analyze_response.py response.npy --fs-high 352800 --dac-rate 44100 --plot response.png
uv run python -m unittest discover -s tests -t .
```

The tests compare against independent calculations (analytic responses, exact delays, and a dense FFT instead of the direct DTFT). CTest runs them under the `tools` label (about 70 s).

## Published responses

`publish_responses.py` measures the core and the wrapper and writes `docs/responses.md`. It needs a release build with the tools enabled:

```sh
cmake --preset release && cmake --build --preset release
cd tools && uv run python publish_responses.py ../build/release
```

## Analog profile measurements

`measure_profile.py` measures one metric from a capture and checks it against a target; CTest's `analog` gates use it. `report_profile.py` writes `docs/analog_profile.md` for both converters. The captures come from two C++ tools built with the tests when uv is available:

- `cpp/capture.cpp` (`ak4522_capture`): `HostProcessor` output.
- `cpp/adc_capture.cpp` (`ak4522_adc_capture`): `AdcCore` output, optionally through the DAC first.

## C++ tools

- `cpp/benchmark.cpp` (`ak4522_benchmark`): CPU cost per callback against REQ-PERF-01; run it in a release build.
- `cpp/impulse_dump.cpp`, `cpp/host_response.cpp`: impulse and wrapper responses for the response gates and `publish_responses.py`.
- `cpp/golden_dump.cpp`: regenerates the golden Specification-profile outputs in `tests/data/`, only for a deliberate change to the clean signal path.

## Other tools

`check_docs.py` checks the engineering records for consistency and runs in CTest under the `docs` label. It uses only the standard library, so CTest runs it with any Python 3.9 or later; `test_check_docs.py` tests the checker itself.

`mutation_check.py` checks that the tests detect deliberate faults. Each list in `mutations/` names its source file and the CTest labels to run in two header lines (`# source:`, `# label:`), followed by `(name, old, new)` entries; the tool applies each entry, rebuilds, and reports whether a test failed. Use a build configured with `-DAK4522_WARNINGS_AS_ERRORS=OFF`:

```sh
cmake -S . -B build/mutation -DAK4522_WARNINGS_AS_ERRORS=OFF
python3 tools/mutation_check.py tools/mutations/pcm.txt   # one list
python3 tools/mutation_check.py --all                     # every list
python3 tools/mutation_check.py --check                   # every entry still applies; no build
```

A full run is slow (a few seconds per entry, about an hour for all lists) and is not part of CTest. The `--check` mode is: CTest runs it as `tools.mutation_lists`, so a change that leaves a list stale fails the test suite. When code changes, update the affected entries; when a mutation survives, add a test that catches it.

Generated coefficients are committed under `src/` together with a JSON design report (parameters, tool versions, checksum, measured response). Regeneration is checked against the response gates, not against bit-identical coefficients, because optimizer results can differ in the last bits across platforms.
