# Feature record

What exists in this branch, as distinct from what AGENTS.md specifies. Implementation, validation, approval, and availability are tracked separately: a feature can be implemented without being approved or released (REQ-DOC-01, REQ-DOC-02).

Last substantive update: 2026-10-16. M0 work was merged into `main` from `m0-foundation` at the project owner's request on 2026-10-16.

## Summary

| Feature                         | Requirements                          | Implementation | Validation               | Approval                          | Availability          |
| ------------------------------- | ------------------------------------- | -------------- | ------------------------ | --------------------------------- | --------------------- |
| Build and test foundation       | AGENTS.md s.13, M0                    | implemented    | passing (see below)      | not yet approved                  | merged into `main` |
| Evidence and assumptions ledgers | AGENTS.md s.2, M0                    | implemented    | reviewed against [DS]    | not yet approved                  | merged into `main` |
| Public API proposal             | REQ-ARCH-01, REQ-ARCH-02, REQ-RT-02   | in progress (declared, not implemented) | compile-time contract checks pass | decisions O-01 to O-03 approved | merged into `main` |
| PCM mapping and quantization    | REQ-PCM-01 to REQ-PCM-03              | planned (M1)   | none                     | none                              | none                  |
| Serial decoder and timing validator | REQ-SERIAL-01 to REQ-SERIAL-03    | planned (M1)   | none                     | none                              | none                  |
| Clock configuration and reset   | REQ-CLOCK-01, REQ-RESET-01, REQ-RESET-02 | planned (M1) | none                    | none                              | none                  |
| Soft mute                       | REQ-MUTE-01, REQ-MUTE-02              | planned (M1)   | none                     | none                              | none                  |
| De-emphasis                     | REQ-DEEM-01 to REQ-DEEM-03            | planned (M2)   | none                     | none                              | none                  |
| Interpolation and reconstruction | REQ-FILTER-01, REQ-FILTER-02, REQ-LAT-02 | planned (M2) | none                   | none                              | none                  |
| Host-rate adaptation            | REQ-RATE-01 to REQ-RATE-04, REQ-CTRL-01 | planned (M3) | none                     | none                              | none                  |
| NominalAnalog profile           | REQ-NOISE-01, REQ-NOISE-02, REQ-DIST-01, REQ-CHANNEL-01 | planned (M4) | none     | none                              | none                  |
| VST3 plugin adapter             | REQ-VST-01, REQ-VST-02, REQ-LAT-03    | planned (M5)   | none                     | none                              | none                  |
| Measured profile                | AGENTS.md s.14                        | planned (M6, conditional on hardware) | none | none                         | none                  |

None of the emulator's signal processing is usable yet.

## Build and test foundation

- **Purpose:** reproducible C++20 build, test runner, and offline Python tooling.
- **Contents:** CMake library target `ak4522::ak4522`; Catch2 v3.16.0 pinned by commit; CTest labels from test tags (`unit`, `dsp`, `soak`); JUnit XML output in `build/<preset>/test-results/`; presets `dev` (warnings as errors), `asan` (address and undefined-behavior sanitizers), `release`; uv-locked Python 3.12 environment in `tools/` with numpy, scipy, matplotlib.
- **Validation:** `dev` and `asan` presets build without warnings and pass all tests. See [validation.md](validation.md).
- **Limitations:** tested only on macOS with Apple clang. No CI yet. Allocation instrumentation and the benchmark harness arrive with the first real-time code.
- **Usage:**

  ```sh
  cmake --preset dev && cmake --build --preset dev && ctest --preset dev
  cd tools && uv sync && uv run python <script>.py
  ```

## Evidence and assumptions ledgers

- **Purpose:** trace every hardware fact to the datasheet and every policy to its rationale.
- **Contents:** [evidence.md](evidence.md) (D/I classifications, seven discrepancies, ADC exclusions), [assumptions.md](assumptions.md) (project policies and twelve open uncertainties).
- **Validation:** all numeric constants in AGENTS.md sections 4 to 10 checked against [DS] M0020-E-01 on 2026-10-15.
- **Limitations:** based on one datasheet revision. A newer revision, if found, must be compared and recorded.

## Public API proposal

- **Purpose:** framework-independent interface for the DAC core, host-rate processing, and serial conformance.
- **Contents:** headers in `include/ak4522/`; design and decisions in [api.md](api.md).
- **Implementation:** pin mappings, defaults, and data types are defined. All processing functions are declared only; calling them fails to link.
- **Validation:** `tests/api_contract_test.cpp` checks pin mappings against [DS] tables, P-01 defaults, same-frame event order, and `noexcept` on every audio-thread entry point at compile time.
- **Approval:** project owner approved decisions O-01 (8x core output), O-02 (256 pending events), and O-03 (reserve the measured profile) on 2026-10-15. The API as a whole is not yet approved.
