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

Reference machine for REQ-PERF-01 (P-93): Apple M1 Max, macOS 26.5.2, Apple clang 21.0.0, `release` preset. Approved by the project owner on 2026-10-16.

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

### 2026-10-16: DAC core (branch `m2-core`, merged into `main`)

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

### 2026-10-16: sample-rate converter (branch `m3-resampler`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Passband and residual, 14 pairs | sine fits at 12 frequencies from 50 Hz to the passband edge | error <= 0.005 dB, residual <= -110 dBFS: pass |
| Stopband | tones from the stopband edge to the input Nyquist frequency, where that range exists | <= -120 dB: pass |
| Fold-over | downsampling tones between the output Nyquist frequency and the stopband edge | alias lands above the passband edge, nothing else above -110 dBFS: pass |
| Timing | impulse at 8:1 peaks at output 100 symmetrically; 1 kHz sine at 147/160 matches its expected times within 1e-5 | pass |
| Counts, partitions, reset, channels, allocations | targeted tests | pass |
| Mutation check | 12 deliberate faults | 10 detected at first. Shifting the kernel by one input sample went unnoticed because the quality tests fit sines with free phase, and the converter's timing feeds the reported latency; a timing test was added. One mutation string did not match the source indentation and was corrected. Final: 12 of 12. |

Development notes: the first stopband test generated tones below the stopband edge for 44.1 / 48 kHz pairs, whose stopband lies above the input Nyquist frequency; it now tests only where a stopband exists, and a fold-over test covers the transition band. One mutation (lost remainder carry) made the long-run test effectively endless; C++ tests now have a 120-second CTest timeout.

### 2026-10-16: host-rate adaptation (branch `m3-host`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Timing alignment, 10 rate pairs | phase delay from sine fits at six passband frequencies against `reportedHostFrames` | within 0.01 frame on average (P-84); single frequencies within the reconstruction dispersion |
| Sample counts | random blocks from 0 to 700 frames and single frames, maximum block 256 | no underruns |
| Level and images | tone at the reference frequency; first DAC image at 96 kHz host / 32 kHz DAC | level within 0.01 dB; image -61.1 dB re the tone (<= -43 dB) |
| Events | mute alignment, grid-point mapping, pending queue across reset, overflow | pass |
| Mutation check | 13 deliberate faults | 11 detected at first. Rounding the event mapping down instead of up, and keeping pending events across reset, went unnoticed. Floor and ceil group host frames identically except at exact grid points (multiples of 160 host frames at 48 / 44.1 kHz), so the mapping test now uses one. Final: 13 of 13. |
| Sanitizers | `asan` preset, unit, host, and rate labels | pass |

Development note: the first level test expected only the tone in the output at every rate pair; at 96 kHz host with a 32 kHz DAC the DAC's images are inside the host band (O-01) and are now checked against the stopband level instead.

### 2026-10-16: response reference fixed at 1 kHz (branch `ref-1khz`, merged into `main`)

The project owner decided the response reference is 1 kHz as stated in [DS] p.6 note 10, not scaled with fs (I-F07 withdrawn). Because the filter table's conditions fix fs = 44.1 kHz, the datasheet gates are now evaluated at 44.1 kHz against 1 kHz only. The 32 and 48 kHz response measurements recorded above were made against a scaled reference and are superseded; at those rates the datasheet makes no ripple or edge claim. Regenerating the interpolator and reconstruction designs reproduced bit-identical coefficients; only report labels changed. `analyze_response.py` now marks results at other rates as informational.

### 2026-10-16: host-rate matrix (branch `m3-verify`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Alignment, 30 pairs | phase fits at three passband frequencies | within 0.01 frame |
| Long runs, 30 pairs | 4 s in random blocks of 0 to 1500 frames | no underruns; phase change below 1e-6 rad between the first and last windows |
| Alias containment, 30 pairs | six stimuli per pair near both Nyquist frequencies, including a two-tone pair; multi-sine fit removing intended components | residual below -110 dBFS (P-34) |
| Block invariance, allocations | random partitions; allocation counter | pass |
| Matrix tests detect faults | converter stopband weakened to 70 dB; prefill reduced; alignment offset removed | all detected |
| Example program | CTest `examples.host_processor` | pass |
| Published responses | `docs/responses.md` | core and wrapper published separately |

Development note: the first long-run test also required the start and end amplitude estimates to agree within 1e-6 dB; they differ by a few 1e-6 dB from 20-bit quantization noise (about -125 dBFS), so that tolerance is now 1e-4 dB. Drift is detected by the phase check.

### 2026-10-16: Specification profile lock (branch `m4-lock`, merged into `main`)

Golden outputs of `DacCore` and `HostProcessor` were recorded before any M4 change. The regression test (tolerance 1e-12) detects a one-sample pipeline change and a 1e-9 gain error.

### 2026-10-16: weighted measurement tools (branch `m4-measure`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| A-weighting | IEC 61672-1 table at 13 exact band frequencies | within 0.05 dB |
| ITU-R 468 | BS.468-4 table at 20 frequencies | within 0.15 dB |
| CCIR-ARM | 0 dB at 2 kHz; -5.63 dB at 1 kHz | pass |
| Metrics | white noise against the analytic integral; known harmonics; DR equal to S/N after the notch | pass |
| Mutation check | 10 deliberate faults | 9 detected at first. A wrong full-scale reference (1.0 instead of 1/sqrt 2, a 3 dB error in every S/N and DR) went unnoticed because the test took its expected value from the same constant. Final: 10 of 10. |

Development note: the IEC 61672-1 table is tabulated at exact base-ten frequencies (1000 x 10^(n/10)) under nominal labels; the first test evaluated the formula at the labels (31.5 Hz rather than 31.62 Hz) and missed by up to 0.13 dB.

**CCIR-ARM versus A-weighting (DISC-08).** With white noise set to 100 dB A-weighted, CCIR-ARM gives 97.33 dB against the 96 dB typical. Checks of possible measurement causes:

| Hypothesis | Test | CCIR-ARM minus A-weighted |
| ---------- | ---- | ------------------------: |
| Analytic, white noise, 10 Hz to 20 kHz | integral of the analog curves | -2.67 dB |
| Band limits | 22 Hz to 22 kHz; 20 Hz to 20 kHz | -2.60; -2.67 dB |
| 1 kHz reference instead of 2 kHz | ITU-R 468 with ARM meter | -8.26 dB (further from -4.0) |
| Leakage without a brick wall | unlimited to 48 kHz | A-weighting gains 0.30 dB; CCIR-ARM 0.001 dB |
| Noise statistics, 10 s windows | Gaussian; clipped at 3x RMS; uniform; MLS-like | -2.67; -2.69; -2.84; -3.03 dB |
| Sample rate and filter warping | 44.1, 48, 96 kHz, exact curves per FFT bin | -2.67 dB at all three |
| Level dependence | 10 dB lower | unchanged |

The datasheet pair implies -4.0 dB. No measurement effect accounts for the remaining 1.3 dB.

### 2026-10-16: NominalAnalog noise (branch `m4-noise`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| A-weighted S/N | 10 s zero input, 44.1 kHz host and DAC, `measure_profile.py` | 100.006 dB (100 +/- 1, P-90) |
| Noise while muted | 10 s, full-scale tone with SMUTE high from the start | 100.006 dB, identical to zero input (P-54) |
| CCIR-ARM S/N | same capture | 97.32 dB (analysed 97.33; 96 dB typical, DISC-08) |
| Generator statistics and wiring | 8 C++ test cases | pass |
| Clean profile | golden lock | unchanged |
| Mutation check | 8 deliberate faults | 7 detected; one equivalent (see the feature record) |

### 2026-10-16: NominalAnalog output stage (branch `m4-imperfections`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| S/(N+D) at 0 dBFS, 997 Hz | 5 s captures, `measure_profile.py` | equal 2nd/3rd 90.004 dB; 3rd only 90.006 dB; 2nd only 90.002 dB; full nominal profile 89.88 dB (90 +/- 1) |
| Dynamic range | -60 dBFS, A-weighted, 10 s | 99.89 dB (100 +/- 1) |
| Harmonics, mismatch, crosstalk, offset | joint sine fits on `DacCore` output | within 0.05 dB of design |
| Isolation through the host path | 10 s with noise, coherent fit (gate 7) | 110 dB at 1 kHz (after the 0.2 dB mismatch) |
| Mutation check | 10 deliberate faults | 8 detected at first. Coupling from the wrong channel went unnoticed because only one direction was tested; gate 7 requires both, and a test now covers it. The cubic sign was invisible to magnitude tests; the model was changed to the physically usual compressive sign, and a phase test pins it. Final: 10 of 10. |

Development notes: a level-scaling test at -20 dBFS measured the harmonics of the periodic 20-bit quantization error of a 1 kHz test tone rather than the distortion; the tests now use 997 Hz at -6 dBFS. A first phase test fitted the 3rd harmonic alone and picked up leakage from the fundamental (about 9e-5); it now uses a joint fit.

### 2026-10-16: NominalAnalog report (branch `m4-verify`, merged into `main`)

[analog_profile.md](analog_profile.md) records the measured profile against the datasheet. Notes: the full profile's A-weighted S/N (99.91 dB) is 0.1 dB below the noise alone because the fixed mismatch raises the left channel's gain, noise included. 16-bit dynamic range is 97.13 dB without dither, within 1 dB of the 98 dB typical, which most likely refers to undithered input; TPDF dither adds noise by design (94.30 dB).

### 2026-10-16: platform support (branch `portability`, merged into `main`)

All runs at commit `4e41845`; debug builds for the full suite, release builds for the matrix.

| Platform | Compiler | Full suite | Host-rate matrix |
| -------- | -------- | ---------- | ---------------- |
| macOS, Apple Silicon | AppleClang | 214 of 214 | 5 of 5 |
| Windows 11 ARM64 (VM) | MSVC 14.51 (Build Tools 2026), ARM64 | 213 of 213 | 5 of 5 |
| Windows 11 ARM64 (VM), x64 build under emulation | MSVC 14.51, x64 (`dev-x64`, `release-x64`) | 213 of 213 | 5 of 5 |
| CentOS Stream 9 aarch64 (VM) | GCC 11.5 | 214 of 214 | 5 of 5 |

Windows has one test fewer: the comparison of the portable multiply-divide against 128-bit integers needs `__int128`, which MSVC lacks; the hand-computed cases still run. The x64 binaries were confirmed as x64 (PE machine 0x8664). The golden lock (tolerance 1e-12) passes on every platform. Not covered: macOS x86_64, native x64 Windows hardware, and Clang on Windows or Linux.

Found during the runs and fixed: GCC `-Wpedantic` rejected `__int128` in a test; MSVC warned about `fopen` (C4996) in tools and about an implicit double-to-float conversion in a test (C4244); the allocation-counter self-test counted an extra allocation from MSVC's debug vector proxy, and now uses direct `new` and `delete`.

### 2026-10-16: DacStream (branch `m5-stream`)

| Check | Method | Result |
| ----- | ------ | ------ |
| DAC frames consumed | 44 100 000 / 1001 Hz against 44.1, 48, 96, 192 kHz hosts; 400 random render blocks; compared after every block with ceil(T N / (D H)) computed in the test | exact |
| Block-size invariance | one block against random blocks with 0 and 1 frames, with SMUTE and DEM events | within 1e-12 (gate 8) |
| Timing | phase delay at six passband frequencies, four rate pairs including two fractional DAC rates | each within 0.02 frame of the reported latency, mean within 0.01 (gate 3) |
| Formats | 16-bit against 20-bit times 16; 24-bit against 20-bit floor over 16; float32 and float64 against exact codes | identical output |
| Push against pull | same frames and events, random render sizes; also with the producer on another thread | identical output, no underruns |
| Real-time safety | allocation counter around render, push, reset, and queries; ThreadSanitizer on the push tests | no allocations; no races reported |
| Mutation check | 14 deliberate faults in `src/core/dac_stream.cpp` | 14 detected |

Development note: the first threaded test pushed more frames than the renders would consume, so the producer waited forever on a full queue; the test now pushes exactly `dacFramesFor` of the rendered length.

### 2026-10-16: extended DAC-rate range (branch `m5-extended-rate`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Opt-in and bounds | validation at 48 000.001, 48 611.111, 50 400 Hz with and without the option; 50 400.001, 52 800, 96 000, 15 999.999 Hz, 0, NaN, infinity with it | accepted only with the option and only up to 50 400 Hz |
| Normalized response | `DacCore` output for the same input at 50.4 and 44.1 kHz | identical sample sequences |
| Host path | 50.4 kHz DAC at 48 and 96 kHz hosts, six passband frequencies | level within the response tolerance; timing within 0.01 frame of the reported latency |
| Stream | 112 MHz / 2304 source at 48 kHz | exact frame consumption; level and timing as above |
| Mutation check | 4 deliberate faults in the validation | 4 detected |

Above 48 kHz these are consistency checks of the unchanged model; the datasheet's gates are claimed only from 16 to 48 kHz (P-103).

### 2026-10-16: C API (branch `m5-c-api`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Built and run as C | `tests/c_api/c_api_test.c`, C99, warnings as errors, every function called from C | passes |
| Equivalence with C++ | host output in 14 configurations, one field set in each; pull stream with events; latency and diagnostics | bit-identical |
| Real-time safety | allocation counter around host processing, push, render, diagnostics | no allocations |
| Mutation check | 14 deliberate faults in `src/c_api/ak4522_c.cpp` | 13 detected at first; swapped underrun and overrun counters survived because no test produced both. A C test of push accounting now does: 14 of 14 |

### 2026-10-16: packaging, integration guide, examples (branch `m5-package`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Installed package | `tests/package/consumer` (C++ and C programs) configured with `find_package(ak4522)` against this build installed to a prefix | builds and runs |
| Subproject | the same consumer with `add_subdirectory` of the source tree | builds and runs |
| Shared library | library built with `BUILD_SHARED_LIBS=ON`, installed, consumer built against it; linkage confirmed with `otool -L` on macOS | builds and runs |
| Examples | five programs, one per entry point and the C API, run by CTest with checked output | pass |

Development note: the push example first rendered as fast as the CPU allowed, which could outrun the producer on a slow machine; it now paces renders like a host callback.

### 2026-10-16: CPU benchmark (branch `m5-package`)

`build/release/ak4522_benchmark --check --seconds 60` on the reference machine, one instance, ordinary thread priority, other virtual machines idle. Shares of the callback period:

| Case | Block | Median | p99.9 | Max | Over the period |
| ---- | ----: | -----: | ----: | --: | --------------: |
| HostProcessor 48 / 44.1 kHz, clean (budget case) | 64 | 2.10% | 3.40% | 10.3% | 0 of 45 000 |
| | 128 | 2.08% | 2.88% | 5.9% | 0 |
| | 256 | 2.07% | 2.51% | 2.7% | 0 |
| | 512 | 2.11% | 2.59% | 3.0% | 0 |
| HostProcessor 48 / 44.1 kHz, nominal profile | 64 | 2.83% | 4.14% | 9.7% | 0 |
| HostProcessor 192 / 48 kHz, clean | 64 | 2.64% | 5.66% | 28.4% | 0 of 180 000 |
| DacStream 48 kHz / 48.611 kHz | 64 | 2.13% | 3.59% | 7.5% | 0 |

Budget (p99.9 below 10% for blocks of 64 or more): met. Before the output converter's loop was restructured, the same case measured a median of 6.2% and p99.9 of 9.2% (and 10.1% with other virtual machines busy). The change alters only the rounding of the kernel sums; golden lock, matrix, and response gates pass. A mutation check of the new loop detected 4 of 4 faults after a channel-symmetry test was added (a wrong tap index in the right channel first survived).

### 2026-10-16: evidence ledger status review (branch `ledger-review`)

The 24 ledger rows still marked *planned* were checked against the tests. 11 are covered by existing passing tests (D-01 only for its 20-bit stereo part) and now say so with the test tag (for example D-07 and I-O01 by the voltage-scaling test, D-P03 by the zero-code mapping, D-C06 by the serial decoder's startup handling, D-R02 by the startup latch test). 8 record facts with no software counterpart: supply voltages, gain drift, load limits, power-supply rejection, modulator noise, and PD-edge clicks; they are now *not modeled*, a status defined in the ledger. D-10 (package and temperature range) is *informational*. 4 remain planned with what they wait for: D-S12 (the ADC, M7), D-C07 (a clock-stop report, P-31, not yet implemented), and D-O02 and I-O02 (the optional Fig.11 board circuit).

### 2026-10-16: small gaps (branch `small-gaps`)

| Item | Checks | Result |
| ---- | ------ | ------ |
| Clock stop in the timing validator (D-C07, P-31) | 6 test cases; mutation check | pass; 5 of 6 faults detected at first (a clock that stops twice was counted once), 6 of 6 after a test was added |
| `DacStream` float dither (P-12) | sub-LSB level preserved on average; seeded; independent channels; integer input untouched; push equals pull | pass |
| C API: serial decoder, timing validator, stream dither | C99 test; bit-identical to C++ over random serial framing in all four modes and random pin edges | pass; mutation check 7 of 8, the survivor equivalent |
| pkg-config file | `package.pkgconfig` compiles and runs the C consumer from pkg-config flags where pkg-config exists (Linux); this Mac has none, so the test is skipped here | see platform runs |

The shared dither generator (`src/core/dither.hpp`) leaves `HostProcessor` output unchanged; the golden lock passes.

### 2026-10-16: example output circuit, Fig.11 (branch `fig11-circuit`, merged into `main`)

| Check | Method | Result |
| ----- | ------ | ------ |
| Components | corners 1 / (2 pi R C) and gain -18k / 10k | 0.589 Hz, 0.589 Hz, -1.8 (5.1 dB, DISC-05) |
| Response | sine fits at 0.2 Hz to 40 kHz, at 1, 48, and 192 kHz, against the analog transfer function, with and without a 10 kohm load | within 0.01 dB and 0.5 degrees |
| Audio band | 1 kHz sample by sample against -1.8 times the input | within 0.00135 of full scale |
| DC | constant input; NominalAnalog +3 mV offset through `HostProcessor` | removed to exactly zero; mean below 1e-9 |
| Integration | `HostProcessor` and `DacStream` options against the circuit applied to their plain output | bit-identical; latency unchanged |
| C API | C99 test; C/C++ equivalence | pass |
| Mutation check | 8 deliberate faults in `src/analog/example_output_circuit.cpp` | 8 detected |

### 2026-10-16: ADC evidence entries, M7 (branch `m7-evidence`)

Every ADC value in AGENTS.md section 17 was checked against the [DS] text layer, with table columns confirmed from word coordinates (`pdftotext -bbox`) on p.5, p.6, and p.7. All values match their stated columns. Findings: tPDV (516 / fs) is printed in the typ column while p.12 states 516 cycles without qualification (recorded in AD-10); Fig.1 shows SDTO truncated to bits 19 to 4 at 32fs in mode 0 as well as mode 3 (DISC-10); p.1 calls the signal inputs single-ended while p.3 and p.15 describe differential inputs (DISC-09). Inferences: the HPF points fit a first-order section (I-AD03); the passband edge equals fs minus the stopband edge (I-AD02). The documentation checker now validates AD and I-AD IDs (it had not recognized them), with a self-test.

## M5 gate status

M5 approved by the project owner on 2026-10-16 (tag `m5-approved`).

AGENTS.md section 16, M5: "Deliver, in this order: `DacStream` ...; the opt-in extended rate range (REQ-CLOCK-02); the C API (REQ-API-02); packaging, the integration guide, and generic examples (REQ-API-03). Gate: library tests for each new API, including block-partition invariance, drift-free long runs at a fractional DAC rate, push and pull equivalence, and real-time allocation checks; a C API test built as C; a consumer project, outside the library's own build, built against the installed package and run; release benchmark meeting the stated budget."

| Gate item | Evidence | Status |
| --------- | -------- | ------ |
| Tests for each new API | `[stream]`, `[push]`, `[extended]`, `[capi]`, `package` labels; mutation checks 14/14, 4/4, 14/14, 4/4 | pass |
| Block-partition invariance | `DacStream` random render sizes with events, within 1e-12 | pass |
| Drift-free fractional rate | exact DAC-frame counts at 44 100 000 / 1001 Hz and 112 MHz / 2304 against integer arithmetic | pass |
| Push and pull equivalence | bit-identical, also with the producer on another thread; ThreadSanitizer clean | pass |
| Real-time allocation checks | render, push, and C calls under the allocation counter | pass |
| C API test built as C | `tests/c_api/c_api_test.c`, C99, warnings as errors | pass |
| Consumer against the installed package | `package.install`, plus `add_subdirectory` and shared-library variants | pass |
| Release benchmark meets the budget | p99.9 3.4% at 64 frames on the reference machine (budget 10%); Linux VM 5.4% | pass |
| Platforms | macOS, Windows ARM64 and x64, Linux aarch64: full suite, package tests, and matrix at `a210911` | pass |

## M4 gate status

AGENTS.md section 16, M4: "Deliver calibrated noise, optional distortion/mismatch/crosstalk/voltage diagnostics, and profile metadata. Gate: weighted measurement methods are validated; synthetic assumptions are explicit; typical-target reports pass within project tolerances. Hardware fidelity claims remain limited."

| Gate item | Evidence | Status |
| --------- | -------- | ------ |
| Calibrated noise | P-94; A-weighted S/N 100.006 dB alone, 99.91 dB in the full profile | pass |
| Optional distortion, mismatch, crosstalk | P-96 to P-98; selectable shapes; each flag independent | pass |
| Voltage diagnostics | `outputVolts()`; DC offset option (P-99) | pass |
| Profile metadata | `describe(ModelProfile)` | pass |
| Weighted measurement methods validated | A-weighting and ITU-R 468 against their standard tables; metrics against analytic results | pass |
| Synthetic assumptions explicit | P-94 to P-99, U-09, U-10, DISC-08; `describe()` marks the profile synthetic | pass |
| Typical-target reports within tolerance | S/(N+D) 89.87, DR 99.89, S/N 99.91 dB against 90 / 100 / 100 +/- 1; isolation 110 dB; mismatch 0.2 dB | pass |
| CCIR-ARM | 97.22 dB against 96 dB typical | disclosed deviation (DISC-08) |
| Clean profile unchanged | golden lock | pass |
| Hardware fidelity claims limited | report preamble; `describe()`; README | pass |

Approved by the project owner on 2026-10-16; the approved state is tagged `m4-approved`. API decision O-10 approved the same day.

## M3 gate status

AGENTS.md section 16, M3: "Deliver SRC, all supported rate pairs, event mapping, latency alignment, and public examples. Gate: alias/count/block-invariance tests and real-time allocation checks pass. Publish core versus wrapper response separately."

| Gate item | Evidence | Status |
| --------- | -------- | ------ |
| SRC | `detail::Resampler`; 14-pair quality tests (0.005 dB, -110 dBFS, -120 dB) | pass |
| All supported rate pairs | 30-pair matrix: 6 host rates x 5 DAC rates | pass |
| Event mapping | exact grid-point mapping test; partition invariance with events; pending queue across reset | pass |
| Latency alignment | 0.01-frame alignment at every pair; integer report | pass |
| Public examples | `examples/host_processor_example.cpp`, run by CTest | pass |
| Alias tests | intended-component fits near both Nyquist frequencies, two-tone | pass |
| Count tests | exact converter counts over 3 million inputs; no underruns over 4 s at every pair | pass |
| Block-invariance tests | converter, host processor, and every matrix pair | pass |
| Real-time allocation checks | every component and every matrix pair | pass |
| Core versus wrapper response | [responses.md](responses.md) | published |

Approved by the project owner on 2026-10-16; the approved state is tagged `m3-approved`.

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

Approved by the project owner on 2026-10-16; the approved state is tagged `m2-approved`.

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
