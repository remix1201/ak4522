# Validation record

Measurement methods, results, and known deviations. Every result states the model profile, rates, stimulus, bandwidth, weighting, and method where they apply. Published hardware values, typical targets, and project tolerances stay distinguishable (AGENTS.md section 13).

## Environment

| Item       | Value                                         |
| ---------- | --------------------------------------------- |
| Machine    | Apple Silicon (arm64), macOS (Darwin 25.5.0)  |
| Compiler   | Apple clang 21.0.0                            |
| CMake      | 4.3.2                                         |
| Catch2     | v3.16.0 (commit 317ac1ed)                     |
| Python     | 3.12.15; numpy 2.5.4, scipy 1.18.1, matplotlib 3.11.2 |

No reference CPU for REQ-PERF-01 has been named yet.

## Results

### 2026-10-15: M0 foundation, branch `m0-foundation`

| Check                                | Method                                  | Result |
| ------------------------------------ | --------------------------------------- | ------ |
| Debug build, warnings as errors      | `cmake --build --preset dev`            | pass   |
| Sanitizer build (ASan, UBSan)        | `cmake --build --preset asan`           | pass   |
| Unit tests, dev                      | `ctest --preset dev` (3 tests)          | pass   |
| Unit tests, sanitizers               | `ctest --preset asan` (3 tests)         | pass   |
| API contract (compile time)          | static_asserts in `api_contract_test.cpp` | pass |

No signal-processing measurements exist yet. DSP gates from AGENTS.md section 13.2 begin in M1 and M2.

## Known deviations

None.

## Measurement methods

To be written with the first DSP measurements: response analysis, latency fitting, A-weighting and CCIR-ARM filters, SINAD and dynamic-range procedures, and alias containment.
