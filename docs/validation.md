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

### 2026-10-16: PCM mapping and quantization (branch `m1-pcm`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Raw 20-bit words, exhaustive (2^20) | compare with integer two's complement | pass |
| Normalize then quantize, exhaustive (2^20 codes) | round trip must be identity with no clipping | pass |
| 16-bit scaling, exhaustive (65,536) | `code == s * 16`, low four bits zero, `s / 32768` normalization | pass |
| Ties to even, saturation, non-finite, signed zero, subnormal | table-driven cases | pass |
| Random and tie inputs (200,000, fixed seed) | compare with `std::nearbyint` under FE_TONEAREST, then clamp | pass |
| Mutation check | six deliberate faults (tie direction, sign extension, scaling, saturation bound, counter, tie boundary) in a separate non-Werror build | all six detected |
| Sanitizers | `asan` preset, all tests | pass |

### 2026-10-16: configuration validation (branch `m1-config`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| DAC rate limits, NaN and infinity | table-driven cases at and beside 16 and 48 kHz | pass |
| Derived MCLK | nine values from [DS] p.9 Table 1 (exact product for the rounded 22.579 MHz entry) | pass |
| MCLK synchronization | wrong rate, wrong ratio, tolerance at 1e-10 (accepted) and 1e-8 (rejected) | pass |
| Host rates and capacities | six supported rates, six unsupported values, zero capacities | pass |
| Mutation check | seven deliberate faults | six detected; one survived (widened 512fs MCLK range) |

The surviving mutation showed that the explicit MCLK range check could never change a result: with the DAC rate in range and MCLK equal to ratio times rate, MCLK is always in range (I-C02). The check was removed and replaced by an explicit finite test, which the range check had been providing for NaN. Both replacement mutations were then detected.

### 2026-10-16: soft mute and allocation counter (branch `m1-mute`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Allocation counter self-test | vector, `make_unique`, and 64-byte aligned allocation inside an armed counter | 3 allocations and 3 deallocations counted |
| Full mute and release | gain per frame against integer reference; exact 0.0 and 1.0 endpoints | pass |
| Duration | 1024 frames; 32, 23.21995, 21.33333 ms at 32, 44.1, 48 kHz | pass |
| Reversal | after 1, 511, 1023 frames, and during release; maximum gain step 1/1024 | pass |
| Repeated same-level events, reset latching | direct checks | pass |
| Block invariance | one block vs 1-frame, mixed (including 0-frame), and random partitions; bitwise equality | pass |
| Real-time | no allocations in `reset`, `setPin`, `process`, `nextGain` | pass |
| Mutation check | seven deliberate faults | all detected |
| Sanitizers | `asan` preset, all 45 tests | pass |

### 2026-10-16: control timeline, and a correction to earlier mutation results (branch `m1-control`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Segmentation | splits exactly at event offsets; zero-frame blocks visit nothing | pass |
| Same-frame order | all six array orders of a PD, DEM, SMUTE group give identical per-frame state | pass |
| Power-down | zero output, state clear on both PD edges, no 516-frame delay | pass |
| Latching | DEM and SMUTE changes during reset latched on release with no ramp | pass |
| Validation | out-of-range, unsorted, and invalid events rejected with no side effects | pass |
| Block invariance | per-frame state identical for one block, 1-frame, mixed, and five random partitions | pass |
| Real-time | no allocations in `process` | pass |
| Sanitizers | `asan` preset, all 57 tests | pass |

**Correction to the mutation checks above.** The earlier checks for PCM, configuration, and soft mute applied each mutation and rebuilt immediately. make compares file times at one-second resolution, so a mutant written within the same second as the previous build could be tested against the previous mutant's binary. That can report a fault as detected or as surviving incorrectly; it was found when a control-timeline mutant survived in one run and was detected when repeated alone. The earlier counts are therefore not reliable on their own.

All mutation checks were rerun with `tools/mutation_check.py`, which moves each mutated file's timestamp ahead and waits so every mutant is rebuilt. Mutation lists are in `tools/mutations/`.

| Component | Mutations | Detected | Notes |
| --------- | --------: | -------: | ----- |
| PCM (`pcm`) | 7 | 7 | |
| Configuration, DAC (`clock`) | 5 | 5 | |
| Configuration, host (`rate`) | 3 | 3 | The first rerun ran these against the `clock` label by mistake and reported two as surviving. |
| Soft mute (`mute`) | 7 | 7 | |
| Control timeline (`control`) | 11 | 11 | 9 in the source file, 2 in the header |

A twelfth control mutation survived: removing the PD check before updating the mute ramp's pin during reset. It has no observable effect, because the ramp does not advance during reset and release re-latches it. The check was removed from the code instead of being kept untestable.

### 2026-10-16: serial decoder (branch `m1-serial`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Test encoder | slot positions checked against Figures 1 to 4, including the I2S 32fs boundary bit and 64fs slots 1 to 20 | pass |
| All modes and ratios | 18 mode and bit-clock combinations (16 to 64 per channel, 32fs to 128fs), 208 words each, both full-scale codes, random padding | pass |
| Unequal halves, I2S boundary bit, startup alignment, short words, reset | targeted cases | pass |
| Real-time | no allocations while decoding | pass |
| Mutation check | 12 deliberate faults | 10 detected at first. One survivor exposed a missing test (reset followed by a right-channel start could pair a stale left word); the test was added and the fault is now detected. The other survivor removed a redundant line with no observable effect; the line was deleted. Final: 11 of 11 detected. |
| Sanitizers | `asan` preset, all 69 tests | pass |

### 2026-10-16: serial timing validator (branch `m1-timing`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Ideal clocks | generated MCLK, SCLK, LRCK, SDTI for 3 CMODE ratios x 4 rates x 32/64/128fs | no violations |
| SCLK at 256fs, 48 kHz | generated stream | period and width violations reported (I-T01) |
| Each limit | value at the limit passes; 0.1 ns or 0.1 % beyond fails; only the intended kind is counted | pass |
| Coincidence, MCLK synchronization, ignored edges, reset | targeted cases | pass |
| Real-time | no allocations | pass |
| Mutation check | 15 deliberate faults | 14 detected at first. The survivor (tLRS checked on every SCLK rise, not only the first) exposed a test whose second rise was too far from the LRCK edge to distinguish the two; the test now uses a sub-45 ns SCLK and the fault is detected. Final: 15 of 15. |
| Sanitizers | `asan` preset, all 90 tests | pass |

Development notes: the first version reported MCLK as unsynchronized when no MCLK edges were supplied at all (zero cycles per LRCK period). The check now counts only LRCK periods that begin with MCLK running.

### 2026-10-16: response analysis tools (branch `m2-analysis`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| DTFT | against `numpy.fft.rfft` at bin frequencies; against the analytic response of a first-order IIR | pass |
| Band extrema | against a 4-million-point FFT for an equiripple lowpass, including from a deliberately coarse 64-point grid | agree within 1e-6 dB |
| Delay | pure delays exact; symmetric FIR delay (N-1)/2; 2nd-order Butterworth low-frequency delay against sqrt(2)/wc | pass |
| Gates | band edges at 44.1 and 48 kHz; ripple, stopband, delay, and combined checks on filters inside, between, and outside the limits | pass |
| Mutation check | 13 deliberate faults in `response.py` and `dac_gates.py` | 11 detected at first. Loosening the ripple limit to 0.6 dB or the stopband limit to 0 dB went unnoticed because no test filter fell between the real and loosened limits. Tests with an echo (ripple of about 0.3 dB) and a 15-tap lowpass (partial attenuation) were added, cross-checked with a dense FFT. Final: 13 of 13. |

### 2026-10-16: 8x interpolator (branch `m2-interpolator`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Design | LP design; shortest odd length with stopband >= 43.2 dB | 191 taps; ripple 0.05802 dB, edge -0.05800 dB, half rate -6.00000 dB, stopband 43.465 dB |
| C++ response, 32 / 44.1 / 48 kHz | `ak4522_impulse_dump` then `analyze_response.py` | pass at all three |
| Generated files | length, checksum, and C++ constant agree; gates on the committed coefficients | pass |
| Time domain | exact impulse response, DC, channels, partitions, reset, bound, allocations | pass |
| Mutation check | 7 deliberate faults | 6 detected at first; the reset test checked only the left channel. Fixed; 7 of 7. |

Findings during design:

- A plain equiripple (Remez) design meets the passband edge gate (-0.06 dB at 20 kHz relative to 1 kHz) at only one length in twenty, because it does not control where the ripple falls. The LP design states all four datasheet numbers as constraints instead (P-88).
- With equal passband and stopband weighting, 0.06 dB ripple and 43 dB attenuation are the same error amplitude. The datasheet numbers are met at about 190 taps, which leaves about 2.8 frames of the 14.7-frame delay to other stages (P-87).
- Measured against an absolute 1 kHz, the same filter shows 0.088 dB ripple at 32 kHz and 0.065 dB at 48 kHz. The reference is taken to scale with fs (I-F07).
- A DC input does not give a constant 8x output: its images at multiples of fs, each at most -43 dB, form a repeating 8-sample pattern. The test now checks the average and an analytic bound on the pattern.

### 2026-10-16: pipeline delay and reconstruction (branch `m2-reconstruction`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Design | cutoff solved for a 14.7-frame chain delay with a 21-sample pipeline | 1.764485 fs; pole radius 0.673 |
| Linear chain, C++ output, 32 / 44.1 / 48 kHz | `ak4522_impulse_dump core-linear` then `analyze_response.py --core` | delay 14.700 frames (limit 14.7 +/- 0.1); combined deviation 0.058 dB (limit 0.2) |
| Pipeline length choice | prototype at 19 to 22 samples | 19: cutoff 0.92 fs, 0.08 frames dispersion; 20: 1.21 fs; 21: 1.76 fs, 0.013 frames; 22: 2.91 fs, near the 8x Nyquist frequency |
| Generated files and time domain | file checks; 8 C++ test cases | pass |
| Mutation check | 10 deliberate faults | 9 detected at first. Raising the state flush threshold to 1e-6 went unnoticed because every test signal was near full scale; a test with a tone at 1/16 LSB was added. Final: 10 of 10. |

### 2026-10-16: de-emphasis (branch `m2-deemphasis`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Fit | minimax first-order sections; independent evaluation with `scipy.signal.freqz` | 0.093 / 0.073 / 0.048 dB at 32 / 44.1 / 48 kHz (limit 0.1); bilinear would be 1.59 / 0.97 / 0.85 dB |
| Phase | reported against the analog reference | up to 19.0 / 15.0 / 12.6 degrees; a second-order fit reduced magnitude error but not phase error |
| C++ behavior | 14 test cases; 11 after follow mode (O-07) and the host crossfade (O-08) were removed | pass |
| Mutation check | 11 deliberate faults | 10 detected at first. Swapping the 32 and 48 kHz coefficient sets went unnoticed because the tests took expected values from the same table. A test now checks each selection against the analog curve at its own rate. Final: 11 of 11. After O-07 and O-08 the list was rebuilt for the simpler component: 6 faults, 5 detected at first; the reset test checked only the left channel and now checks both. Final: 6 of 6. A seventh candidate (removing the OFF early return) was dropped as equivalent, since OFF's identity coefficients give bit-identical output. |

Development notes: a guessed bound on the Nyquist gain (0.25 to 0.36) failed for the 32 kHz set (0.365), whose fit band ends at 14.4 kHz; the test now checks the implementation against its closed-form Nyquist gain instead. A first draft of the per-rate test probed 21.6 kHz at 48 kHz, beyond the 20 kHz fit band.

### 2026-10-16: DAC core, branch `m2-core`

| Check | Method | Result |
| ----- | ------ | ------ |
| Response gates through the public API | `ak4522_impulse_dump dac-core`, then `analyze_response.py --combined` at 32 / 44.1 / 48 kHz | ripple 0.05803 dB, edge -0.05802 dB, half rate -6.00005 dB, stopband 43.468 dB, delay 14.700 frames, combined 0.05803 dB: all pass |
| Wiring | output equal to the internal components chained independently, three DEM selections | bitwise equal |
| Controls | mute, PD, SMUTE through PD, DEM change, processing order | pass |
| Streaming | single-frame, mixed, random partitions; chunk sizes 7 to 4000 | bitwise equal |
| Real-time | no allocations in `process`, `reset`, `latency`, `diagnostics` | pass |
| Latency constant | `coreDelayFrames` in C++ against the reconstruction design report | equal |
| Mutation check | 12 deliberate faults | 11 detected at first. Swapping mute and de-emphasis (P-40) went unnoticed: no test combined de-emphasis with a moving mute ramp, the only case where the order matters. Test added. Final: 12 of 12. |
| Sanitizers | `asan` preset, unit and dsp labels | pass |

## M2 gate status

AGENTS.md section 16, M2: "Deliver reproducible coefficient generation, interpolation/reconstruction, three de-emphasis modes, and core delay accounting. Gate: frequency, stopband-image, delay, stability, and streaming tests pass. If filter fit and delay cannot simultaneously meet targets, document the conflict and revise before claiming completion."

| Gate item | Evidence | Status |
| --------- | -------- | ------ |
| Reproducible coefficient generation | `tools/design_interpolator.py`, `design_reconstruction.py`, `design_deemphasis.py`; committed hex coefficients with checksums; `test_generated_files.py` | met |
| Frequency response | `dsp.dac_core_response_*`: ripple, edge, half rate, combined | pass |
| Stopband images | stopband check from 24.1/44.1 fs to the 8x Nyquist frequency, on DacCore output | pass (43.47 dB) |
| Delay | passband phase fit on DacCore output; `latency()` | 14.700 frames; pass |
| Stability | pole radii (0.673 reconstruction, de-emphasis below 0.66); decay to exact zero; full-scale bound | pass |
| Streaming | block-partition and chunk-size invariance in every component and in DacCore | pass |
| Three de-emphasis modes | 0.093 / 0.073 / 0.048 dB from the reference | pass |
| Fit and delay conflict | Met together. The datasheet filter numbers take 11.875 frames; the remaining 2.825 frames are an explicit pipeline delay and the reconstruction delay (P-87, P-89) | resolved by owner decision |

Awaiting project-owner review.

## M1 gate status

AGENTS.md section 16, M1: "boundary/serial/control tests pass, including I2S short-word framing and partial mute reversal. No plugin GUI yet."

| Gate item | Evidence | Status |
| --------- | -------- | ------ |
| PCM boundary tests | `pcm_test.cpp`, exhaustive 20-bit and 16-bit | pass |
| Serial tests, all four modes | `serial_decoder_test.cpp` | pass |
| I2S short-word framing | 32fs and 18-bit-clock I2S cases, boundary-bit test | pass |
| Timing validator | `timing_validator_test.cpp` | pass |
| Clock configuration | `config_validation_test.cpp` | pass |
| Reset and control | `control_timeline_test.cpp` | pass |
| Partial mute reversal | reversals after 1, 511, 1023 frames | pass |
| No plugin GUI | none exists | met |

Approved by the project owner on 2026-10-16; the approved state is tagged `m1-approved`. The M1 components are not yet connected to `DacCore::process`; that happens in M2.

No signal-processing measurements exist yet. DSP gates from AGENTS.md section 13.2 begin in M1 and M2.

## Known deviations

None.

## Measurement methods

To be written with the first DSP measurements: response analysis, latency fitting, A-weighting and CCIR-ARM filters, SINAD and dynamic-range procedures, and alias containment.
