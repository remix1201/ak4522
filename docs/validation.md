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

### 2026-10-16: documentation consistency checker, branch `docs-check`

| Check                                | Method                                  | Result |
| ------------------------------------ | --------------------------------------- | ------ |
| Documentation consistency            | `ctest -L docs` runs `tools/check_docs.py` | pass after fixes below |
| Checker self-test (13 cases)         | `tools/test_check_docs.py`: each check fails on a broken copy of the docs | pass |

First run found and fixed: evidence row I-T01 had its cells shifted into the wrong columns; REQ-SCOPE-03 and REQ-PERF-01 were not cited by their policies (P-01, P-93); REQ-DOC-03 to REQ-DOC-05 were not traced. The checker confirms structure and cross-references only. It does not confirm that recorded values match the datasheet.

### 2026-10-16: owner spot-check of the evidence ledger

| Item | Result |
| ---- | ------ |
| DISC-04, combined response column (p.6) | **Ledger wrong.** Value is in the typ column; confirmed by PDF text coordinates. DISC-04 withdrawn, AGENTS.md note reverted. |
| DISC-01, output voltage wording (p.5, p.16) | **Ledger imprecise.** p.16 literally says "input signal range". Ledger now quotes it and explains why it is read as the output. Conclusion unchanged. |
| DISC-02, DISC-03, DISC-05 | confirmed |
| Serial modes (p.9 Table 2), Fig.4 32fs and 64fs framing | confirmed; 64fs bit 0 is in slot 20 per text coordinates |
| DEM, CMODE, mute, reset, DAC filter values, filter conditions, serial timing | confirmed |

### 2026-10-16: PCM mapping and quantization, branch `m1-pcm`

| Check | Method | Result |
| ----- | ------ | ------ |
| Raw 20-bit words, exhaustive (2^20) | compare with integer two's complement | pass |
| Normalize then quantize, exhaustive (2^20 codes) | round trip must be identity with no clipping | pass |
| 16-bit scaling, exhaustive (65,536) | `code == s * 16`, low four bits zero, `s / 32768` normalization | pass |
| Ties to even, saturation, non-finite, signed zero, subnormal | table-driven cases | pass |
| Random and tie inputs (200,000, fixed seed) | compare with `std::nearbyint` under FE_TONEAREST, then clamp | pass |
| Mutation check | six deliberate faults (tie direction, sign extension, scaling, saturation bound, counter, tie boundary) in a separate non-Werror build | all six detected |
| Sanitizers | `asan` preset, all tests | pass |

No signal-processing measurements exist yet. DSP gates from AGENTS.md section 13.2 begin in M1 and M2.

## Known deviations

None.

## Measurement methods

To be written with the first DSP measurements: response analysis, latency fitting, A-weighting and CCIR-ARM filters, SINAD and dynamic-range procedures, and alias containment.
