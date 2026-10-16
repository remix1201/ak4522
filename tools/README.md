# Tools

Offline coefficient design, independent reference vectors, and response analysis. Nothing here runs during the C++ build or on the audio thread.

The environment is pinned by `pyproject.toml`, `.python-version`, and `uv.lock`. Install [uv](https://docs.astral.sh/uv/) and run scripts from this directory:

```sh
uv sync
uv run python <script>.py
```

Generated coefficients are committed under `src/` together with a JSON design report (parameters, tool versions, checksum, measured response). Regeneration is checked against the response gates, not against bit-identical coefficients, because optimizer results can differ in the last bits across platforms.
