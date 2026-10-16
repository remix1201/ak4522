# Tools

Offline coefficient design, independent reference vectors, and response analysis. Nothing here runs during the C++ build or on the audio thread.

The environment is pinned by `pyproject.toml`, `.python-version`, and `uv.lock`. Install [uv](https://docs.astral.sh/uv/) and run scripts from this directory:

```sh
uv sync
uv run python <script>.py
```

`check_docs.py` checks the engineering records for consistency and runs in CTest under the `docs` label. It uses only the standard library, so CTest runs it with any Python 3.9 or later; `test_check_docs.py` tests the checker itself.

`mutation_check.py` checks that tests detect deliberate faults: it applies each entry of a list in `mutations/` to a source file, rebuilds, and runs a CTest label. Use a build configured with `-DAK4522_WARNINGS_AS_ERRORS=OFF`:

```sh
cmake -S . -B build/mutation -DAK4522_WARNINGS_AS_ERRORS=OFF
python3 tools/mutation_check.py src/core/pcm.cpp pcm tools/mutations/pcm.txt
```

It is slow (a few seconds per mutation) and is not part of CTest.

Generated coefficients are committed under `src/` together with a JSON design report (parameters, tool versions, checksum, measured response). Regeneration is checked against the response gates, not against bit-identical coefficients, because optimizer results can differ in the last bits across platforms.
