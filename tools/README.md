# Tools

Offline coefficient design, independent reference vectors, and response analysis. Nothing here runs during the C++ build or on the audio thread.

The environment is pinned by `pyproject.toml`, `.python-version`, and `uv.lock`. Install [uv](https://docs.astral.sh/uv/) and run scripts from this directory:

```sh
uv sync
uv run python <script>.py
```

## Response analysis

`ak4522_tools/` measures impulse responses against the DAC response gates in AGENTS.md section 13.2. It is the independent reference for M2: filters are designed here, implemented in C++, and the C++ output is measured here again.

- `response.py`: exact DTFT at any frequency; band minimum and maximum from a dense grid with every local extremum refined by a bounded search; delay from a least-squares fit of passband phase, with dispersion.
- `dac_gates.py`: band edges scaled with the DAC rate, and the checks for passband ripple, passband edge, -6 dB point, stopband attenuation to the high-rate Nyquist frequency, delay, and combined response, each labeled with its evidence or policy ID.
- `analyze_response.py`: command line. Input is an impulse response at the high rate for a unit impulse at the DAC rate (`.npy` or one value per line). Output is a JSON report; `--plot` writes a PNG.

```sh
cd tools
uv run python analyze_response.py response.npy --fs-high 352800 --dac-rate 44100 --plot response.png
uv run python -m unittest discover -s tests -t .
```

The tests compare against independent calculations (analytic responses, exact delays, and a dense FFT instead of the direct DTFT). CTest runs them under the `tools` label (about 70 s).

## Other tools

`check_docs.py` checks the engineering records for consistency and runs in CTest under the `docs` label. It uses only the standard library, so CTest runs it with any Python 3.9 or later; `test_check_docs.py` tests the checker itself.

`mutation_check.py` checks that tests detect deliberate faults: it applies each entry of a list in `mutations/` to a source file, rebuilds, and runs a CTest label. Use a build configured with `-DAK4522_WARNINGS_AS_ERRORS=OFF`:

```sh
cmake -S . -B build/mutation -DAK4522_WARNINGS_AS_ERRORS=OFF
python3 tools/mutation_check.py src/core/pcm.cpp pcm tools/mutations/pcm.txt
```

It is slow (a few seconds per mutation) and is not part of CTest.

Generated coefficients are committed under `src/` together with a JSON design report (parameters, tool versions, checksum, measured response). Regeneration is checked against the response gates, not against bit-identical coefficients, because optimizer results can differ in the last bits across platforms.
