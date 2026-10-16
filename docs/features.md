# Feature record

What exists in this branch, as distinct from what AGENTS.md specifies. Implementation, validation, approval, and availability are tracked separately: a feature can be implemented without being approved or released (REQ-DOC-01, REQ-DOC-02).

Last substantive update: 2026-10-16. M0, M1, and M2 were merged into `main` and approved by the project owner on 2026-10-16; the approved states are tagged `m0-approved`, `m1-approved`, and `m2-approved`.

## Summary

| Feature                         | Requirements                          | Implementation | Validation               | Approval                          | Availability          |
| ------------------------------- | ------------------------------------- | -------------- | ------------------------ | --------------------------------- | --------------------- |
| Build and test foundation       | AGENTS.md s.13, M0                    | implemented    | passing (see below)      | approved 2026-10-16 (`m0-approved`) | merged into `main` |
| Evidence and assumptions ledgers | AGENTS.md s.2, M0                    | implemented    | reviewed against [DS]    | approved 2026-10-16 (`m0-approved`) | merged into `main` |
| Public API proposal             | REQ-ARCH-01, REQ-ARCH-02, REQ-RT-02   | in progress (declared, not implemented) | compile-time contract checks pass | approved 2026-10-16 (`m0-approved`) | merged into `main` |
| PCM mapping and quantization    | REQ-PCM-01 to REQ-PCM-03              | implemented    | passing (see below)      | approved 2026-10-16 (`m1-approved`) | merged into `main`    |
| Serial decoder                  | REQ-SERIAL-01, REQ-SERIAL-03          | implemented    | passing (see below)      | approved 2026-10-16 (`m1-approved`) | merged into `main`    |
| Serial timing validator         | REQ-SERIAL-02                         | implemented    | passing (see below)      | approved 2026-10-16 (`m1-approved`) | merged into `main`    |
| Configuration validation        | REQ-CLOCK-01, REQ-RATE-03             | implemented    | passing (see below)      | approved 2026-10-16 (`m1-approved`) | merged into `main`    |
| Reset and control timeline      | REQ-RESET-01, REQ-RESET-02, REQ-CTRL-01 | implemented  | passing (see below)     | approved 2026-10-16 (`m1-approved`) | merged into `main`    |
| Soft mute                       | REQ-MUTE-01, REQ-MUTE-02              | implemented    | passing (see below)      | approved 2026-10-16 (`m1-approved`) | merged into `main`    |
| De-emphasis                     | REQ-DEEM-01 to REQ-DEEM-03            | implemented    | passing (see below)      | approved 2026-10-16 (`m2-approved`)       | merged into `main`    |
| DAC core (linear Specification profile) | REQ-ARCH-02, REQ-ARCH-03, REQ-LAT-01, REQ-RT-01, REQ-RT-02 | implemented | passing (see below) | approved 2026-10-16 (`m2-approved`)| merged into `main`    |
| Response analysis tools         | REQ-FILTER-01, REQ-LAT-02, REQ-DEEM-02 | implemented   | passing (see below)      | approved 2026-10-16 (`m2-approved`)       | merged into `main`    |
| 8x interpolator                 | REQ-FILTER-01                         | implemented    | passing (see below)      | approved 2026-10-16 (`m2-approved`)       | merged into `main`    |
| Analog reconstruction and pipeline delay | REQ-FILTER-02, REQ-LAT-02    | implemented    | passing (see below)      | approved 2026-10-16 (`m2-approved`)       | merged into `main`    |
| Sample-rate converter           | REQ-RATE-02, REQ-RATE-04              | implemented    | passing (see below)      | not yet approved                  | merged into `main`    |
| Host-rate adaptation            | REQ-RATE-01, REQ-RATE-03, REQ-CTRL-01, REQ-LAT-03 | implemented | passing (see below) | not yet approved           | branch `m3-host`      |
| NominalAnalog profile           | REQ-NOISE-01, REQ-NOISE-02, REQ-DIST-01, REQ-CHANNEL-01 | planned (M4) | none     | none                              | none                  |
| VST3 plugin adapter             | REQ-VST-01, REQ-VST-02, REQ-LAT-03    | planned (M5)   | none                     | none                              | none                  |
| Measured profile                | AGENTS.md s.14                        | planned (M6, conditional on hardware) | none | none                         | none                  |
| ADC section and loopback        | REQ-SCOPE-04, REQ-ADC-01 to REQ-ADC-07 | planned (M7)  | none                     | none                              | none                  |

The DAC core produces audio through the public `DacCore` API at the DAC rate in, 8x the DAC rate out, with the Specification profile. Host-rate processing (`HostProcessor`), analog imperfections, and the plugin do not exist yet.

## Build and test foundation

- **Purpose:** reproducible C++20 build, test runner, and offline Python tooling.
- **Contents:** CMake library target `ak4522::ak4522`; Catch2 v3.16.0 pinned by commit; CTest labels from test tags (`unit`, `dsp`, `soak`); JUnit XML output in `build/<preset>/test-results/`; presets `dev` (warnings as errors), `asan` (address and undefined-behavior sanitizers), `release`; uv-locked Python 3.12 environment in `tools/` with numpy, scipy, matplotlib.
- **Validation:** `dev` and `asan` presets build without warnings and pass all tests. See [validation.md](validation.md).
- **Real-time checks:** `tests/support/allocation_counter.hpp` replaces global `operator new` and `delete` in the test executable and counts allocations per thread while armed (REQ-RT-01). Its own tests confirm it detects plain, array-backed, and over-aligned allocations.
- **Limitations:** tested only on macOS with Apple clang. No CI yet. The CPU benchmark harness (REQ-PERF-01) is not written yet.
- **Usage:**

  ```sh
  cmake --preset dev && cmake --build --preset dev && ctest --preset dev
  cd tools && uv sync && uv run python <script>.py
  ```

## Evidence and assumptions ledgers

- **Purpose:** trace every hardware fact to the datasheet and every policy to its rationale.
- **Contents:** [evidence.md](evidence.md) (D/I classifications, six open discrepancies and one withdrawn, ADC exclusions), [assumptions.md](assumptions.md) (project policies and twelve open uncertainties).
- **Validation:** all numeric constants in AGENTS.md sections 4 to 10 checked against [DS] M0020-E-01 on 2026-10-15. Project owner spot-checked 14 items on 2026-10-16; DISC-04 withdrawn and DISC-01 wording corrected as a result.
- **Limitations:** based on one datasheet revision. A newer revision, if found, must be compared and recorded.

## Public API proposal

- **Purpose:** framework-independent interface for the DAC core, host-rate processing, and serial conformance.
- **Contents:** headers in `include/ak4522/`; design and decisions in [api.md](api.md).
- **Implementation:** pin mappings, defaults, and data types are defined. All processing functions are declared only; calling them fails to link.
- **Validation:** `tests/api_contract_test.cpp` checks pin mappings against [DS] tables, P-01 defaults, same-frame event order, and `noexcept` on every audio-thread entry point at compile time.
- **Approval:** project owner approved decisions O-01 (8x core output), O-02 (256 pending events), and O-03 (reserve the measured profile) on 2026-10-15, and approved the API proposal as part of M0 on 2026-10-16 (tag `m0-approved`). Approval covers the proposal; implementations are approved with their milestones.

## PCM mapping and quantization

- **Purpose:** convert serial words, 16-bit samples, and host floating-point audio into the DAC's 20-bit codes, and back to normalized values.
- **Requirements:** REQ-PCM-01, REQ-PCM-02, REQ-PCM-03; evidence D-P01, D-P02, I-P01, I-P02; policy P-10.
- **Functions** (`include/ak4522/pcm.hpp`):
  - `pcm20FromRaw(word)`: low 20 bits as two's complement; upper bits ignored.
  - `pcm20FromInt16(sample)`: multiplies by 16, so the four low bits are zero.
  - `quantizeToPcm20(x, counters)`: NaN and infinity become 0 and increment `nonFinite`; otherwise `x * 2^19` rounded to nearest with ties to even (implemented explicitly, independent of the floating-point rounding mode), then saturated to [-524288, 524287] with `clipped` incremented. +1.0 is not representable and saturates to 524287; -1.0 maps exactly to -524288.
  - `toNormalized(pcm)`: `code / 2^19`.
- **Validation:** 12 tests in `tests/pcm_test.cpp`, including all 2^20 raw words, all 2^20 codes through normalize and quantize, all 65,536 16-bit samples, half-LSB ties on both signs and at both range ends, saturation, non-finite input, and 200,000 random and tie inputs against the C library's `std::nearbyint`. Seven deliberate code mutations were each caught by the tests (`tools/mutations/pcm.txt`). Passes in the `dev` and `asan` presets.
- **Limitations:** optional TPDF dither (P-12) and the host-side 16-bit input resolution are not implemented; they belong to `HostProcessor` (M3).
- **Updated:** 2026-10-16.

## Configuration validation

- **Purpose:** reject invalid chip and host configurations before processing, with a specific `Status`.
- **Requirements:** REQ-CLOCK-01, REQ-RATE-03; evidence D-04, D-05, D-C01 to D-C05, I-C01, I-C02; policies P-32, P-35; decision O-03.
- **Implementation:** internal `detail::validate` in `src/core/config_validation.hpp`, for `DacConfig` and `HostConfig`. `DacCore::prepare` and `HostProcessor::prepare` will call it when they are implemented (M2, M3); it is not callable through the public API yet.
- **Rules:**
  - DAC rate in [16000, 48000] Hz, inclusive; NaN and infinity rejected (`invalidDacRate`).
  - When `masterClockHz` is set, it must be finite and equal the CMODE ratio (256, 384, or 512) times the DAC rate within 1e-9 relative (`invalidMasterClock`). This implies the datasheet MCLK range, so there is no separate range check.
  - `ModelProfile::measured` and out-of-range enum values are rejected (`invalidConfiguration`).
  - Host rate must be exactly 44.1, 48, 88.2, 96, 176.4, or 192 kHz (`unsupportedHostRate`); `maxBlockFrames` and `maxPendingEvents` must be non-zero.
- **Validation:** 14 test cases in `tests/config_validation_test.cpp`, including the nine p.9 Table 1 clock values, MCLK endpoints at both rate limits, the synchronization tolerance on both sides, and NaN inputs. All eight deliberate mutations are detected (see [validation.md](validation.md)).
- **Limitations:** the host/DAC rate-pair matrix (`unsupportedRatePair`) is defined in M3 with the SRC. Clock loss while running (D-C07) is a runtime fault for the serial adapter, not a configuration check.
- **Updated:** 2026-10-16.

## Soft mute

- **Purpose:** the SMUTE pin's digital attenuation ramp.
- **Requirements:** REQ-MUTE-01, REQ-MUTE-02; evidence D-M02 to D-M04, I-M01; policies P-50 to P-53, P-72.
- **Implementation:** internal `detail::SoftMute` in `src/core/soft_mute.hpp`. It is not connected to `DacCore` yet; the control timeline (M1) and `DacCore::process` (M2) will drive it.
- **Behavior:**
  - Integer position 0 to 1024; gain is position / 1024, so 0.0 and 1.0 are exact.
  - Each DAC frame moves one step toward the target. A full mute or release takes exactly 1024 frames: 32 ms at 32 kHz, 23.21995 ms at 44.1 kHz, 21.33333 ms at 48 kHz.
  - A level change applies before the next frame's step; from unity, the 1024th frame after SMUTE goes high has gain 0.
  - Reversing mid-ramp retraces from the current position at the same slope.
  - Repeating the current level is ignored (`setPin` returns false).
  - `reset(pinHigh)` latches the level with no ramp: gain 0 if high, 1 if low.
  - One ramp is shared by both channels.
- **Validation:** 11 test cases in `tests/soft_mute_test.cpp` against an integer reference model, including full transitions, reversals after 1, 511, and 1023 frames with no gain step larger than 1/1024, reversal during release, repeated events, reset, and identical output across single-frame, mixed, and random block partitions. No allocations during processing. Seven deliberate mutations detected (`tools/mutation_check.py`, `tools/mutations/soft_mute.txt`). Passes in `dev` and `asan`.
- **Limitations:** propagation through downstream filters, group delay, and residual analog noise while muted (D-M05, D-M06, P-54) are verified in M2 and M4, when those stages exist.
- **Updated:** 2026-10-16.

## Reset and control timeline

- **Purpose:** apply PD, DEM, and SMUTE pin changes at exact DAC frames and split each block into runs of constant control state for the signal path.
- **Requirements:** REQ-CTRL-01, REQ-RESET-01, REQ-RESET-02; evidence D-R01, D-R04, D-R05; policies P-42, P-52, P-53, P-64, P-70 to P-72.
- **Implementation:** internal `detail::ControlTimeline` in `src/core/control_timeline.hpp`. `DacCore::process` will call it in M2; the host-to-DAC event mapping (P-41) is part of `HostProcessor` in M3.
- **Behavior:**
  - Events are validated first: offsets below the block length and non-decreasing, known types, pin values 0 or 1, DEM values 0 to 3. Any failure rejects the whole block with no state change.
  - Events on the same frame apply in the order PD, DEM, SMUTE regardless of array order; same-type events on one frame apply in array order.
  - PD low: segments report `powerDown` (output zero relative to VCOM) and `clearState` on the first segment. DEM and SMUTE changes during reset update the pin levels only.
  - PD high: the current DEM and SMUTE levels are latched; the mute starts at 0 if SMUTE is high, unity if low, with no ramp. The DAC runs from that frame; there is no 516-frame delay.
  - A DEM change marks the segment so the signal path can reset the new section.
  - Repeated levels on any pin are ignored and counted.
  - `reset(dem, smute)` is the software startup: release with the configured pin levels.
- **Validation:** 12 test cases in `tests/control_timeline_test.cpp`, including all six array orders of a same-frame PD, DEM, SMUTE group, PD edges, latching during reset, no 516-frame delay, repeated levels, rejection without side effects, identical per-frame state across random block partitions, and no allocations. Eleven deliberate mutations detected. Passes in `dev` and `asan`.
- **Updated:** 2026-10-16.

## Serial decoder

- **Purpose:** decode the SDTI serial input into 20-bit stereo frames for conformance tests and device-style use. The plugin path does not use it.
- **Requirements:** REQ-SERIAL-01, REQ-SERIAL-03, REQ-SCOPE-02; evidence D-S01 to D-S11, I-T01; policies P-20, P-21, P-23.
- **Usage:** `SerialDecoder::prepare(format)`, then `sclkRisingEdge(lrck, sdti)` once per SCLK rising edge. A result with `SerialEvent::frameComplete` carries the decoded `Pcm20Frame`.
- **Behavior:**
  - Modes: 0 (16-bit right justified, scaled by 16), 1 (20-bit right justified), 2 (20-bit left justified), 3 (I2S). Left is LRCK high in modes 0 to 2 and LRCK low in mode 3.
  - Any number of bit clocks per channel at or above the minimum (16 for modes 0 and 3, 20 for modes 1 and 2) is accepted; padding is ignored; left and right halves may differ in length.
  - I2S: data starts one bit clock after the LRCK edge, and a word's last bit may fall on the first bit clock after the next edge. With fewer than 20 bit clocks per channel (32fs), the untransmitted low bits are zero.
  - The half-frame in progress at reset or startup is discarded silently.
  - Too few bit clocks in a half: `sclkRatioTooLow`, word discarded. A right word with no valid left word before it: `incompleteFrame`, nothing output.
  - The format is fixed until the next `prepare`.
- **Validation:** 12 test cases in `tests/serial_decoder_test.cpp` against an encoder that places each bit by slot number from Figures 1 to 4, itself checked against those figures. All four modes at 16, 18, 20, 21, 24, 32, and 64 bit clocks per channel where valid (32fs to 128fs), 208 words each including both full-scale codes; unequal half lengths; the I2S boundary bit; startup alignment; short words; reset, including reset followed by a right-channel start; no allocations. Eleven deliberate mutations detected (`tools/mutations/serial_decoder.txt`). Passes in `dev` and `asan`.
- **Limitations:** nanosecond timing is checked separately by the timing validator (planned). SDTO output belongs to the ADC (M7).
- **Updated:** 2026-10-16.

## Serial timing validator

- **Purpose:** check timestamped pin transitions against the datasheet timing limits in conformance tests.
- **Requirements:** REQ-SERIAL-02; evidence D-T01 to D-T08, D-04, D-C02 to D-C05, D-R03, I-T01; policies P-24, P-36; API decision O-06 (approved).
- **Usage:** `TimingValidator::prepare(cmode)`, then `observe(PinEdge{timeNs, pin, level})` for each transition in time order; read `violations(kind)` or `totalViolations()`.
- **Checks** (inclusive limits): SCLK period >= 160 ns; SCLK high and low >= 65 ns; LRCK edge to first SCLK rise >= 45 ns; SCLK rise to LRCK edge >= 45 ns; an SCLK rise at the same time as an LRCK edge (reported instead of the two spacing checks); SDTI setup >= 25 ns and hold >= 40 ns; LRCK duty 45 to 55 %; LRCK 16 to 48 kHz; MCLK frequency and pulse width per CMODE (4.096 to 12.288 MHz and 27 ns; 6.144 to 18.432 MHz and 20 ns; 8.192 to 24.576 MHz and 15 ns); MCLK cycles per LRCK period equal to 256, 384, or 512; PD low >= 150 ns.
- **Behavior:** a level equal to the pin's current level is not a transition; edges earlier than the previous one are ignored; a check runs only when both of its edges were observed since reset; MCLK synchronization is counted only over LRCK periods that begin with MCLK running. Frequency limits have a 1e-9 relative tolerance (P-36).
- **Validation:** 22 test cases in `tests/timing_validator_test.cpp`: generated ideal clocks for every CMODE, 16/32/44.1/48 kHz, and 32/64/128fs with no violations; SCLK at 256fs and 48 kHz flagged; each limit at its value (passes) and just beyond (fails); coincidence in both orders; synchronization with and without MCLK; ignored edges; reset; no allocations. Passes in `dev` and `asan`.
- **Limitations:** clock loss while PD is high (D-C07) needs a time-out policy and is not reported. Metastability and recovery are not modeled (P-24).
- **Updated:** 2026-10-16.

## Response analysis tools

- **Purpose:** measure filter and DAC impulse responses against the M2 gates, independently of the C++ implementation.
- **Requirements:** REQ-FILTER-01, REQ-LAT-02, REQ-DEEM-02; AGENTS.md section 13.2 gates 1 to 3; evidence D-F01 to D-F09, I-F01 to I-F04; policies P-80 to P-83.
- **Location and usage:** `tools/ak4522_tools/` and `tools/analyze_response.py`; see [tools/README.md](../tools/README.md).
- **Validation:** 16 tests in `tools/tests/test_response.py` against analytic responses, exact delays, and a 4-million-point FFT, including filters whose ripple and stopband attenuation fall just outside the limits. See [validation.md](validation.md) for the mutation check.
- **Limitations:** analyzes impulse responses; the C++ side that exports `DacCore` impulse responses arrives with the core in M2. Weighted noise, SINAD, and alias analysis belong to M3 and M4.
- **Updated:** 2026-10-16.

## 8x interpolator

- **Purpose:** the DAC's 8x interpolation filter (D-02), the first stage of the DAC response.
- **Requirements:** REQ-FILTER-01; evidence D-02, D-F01 to D-F05, D-F08, D-F09, I-F07; policies P-87, P-88.
- **Design:** `tools/design_interpolator.py` (linear program, HiGHS) writes `src/dsp/interpolator_coefficients.inc` (exact hexadecimal doubles) and `src/dsp/interpolator_design.json` (parameters, versions, SHA-256, measured checks). 191 taps, linear phase, delay 95 samples at 8x = 11.875 DAC frames. Measured: passband ripple 0.058 dB, -0.058 dB at 20/44.1 fs, -6.000 dB at 0.5 fs, stopband 43.46 dB from 24.1/44.1 fs to the 8x Nyquist frequency. One coefficient set at every DAC rate. The coefficients are fitted, not AKM's.
- **Implementation:** internal `detail::Interpolator8x` in `src/dsp/interpolator.hpp`: 8-phase polyphase FIR with a mirrored history buffer, double precision, stereo.
- **Validation:** the C++ impulse response is dumped by `ak4522_impulse_dump` and measured by `analyze_response.py` at 32, 44.1, and 48 kHz (CTest `dsp` label). `tools/tests/test_generated_files.py` checks that the coefficient file, design report, checksum, and the C++ length agree. 9 C++ test cases cover exact impulse response, symmetry, DC with bounded image ripple, channel independence, block partitioning, reset, full-scale bound, and no allocations. Seven deliberate mutations detected. Passes in `dev` and `asan`.
- **Limitations:** the 14.7-frame delay gate applies to the whole core and is checked in M2 once the pipeline delay and reconstruction exist.
- **Updated:** 2026-10-16.

## Analog reconstruction and pipeline delay

- **Purpose:** complete the linear chip model from input register to analog output: undocumented pipeline stages and the switched-capacitor plus continuous-time filters (D-03).
- **Requirements:** REQ-FILTER-02, REQ-LAT-02; evidence D-03, D-F06, D-F07, D-F10; policies P-81, P-85, P-87, P-89.
- **Design:** `tools/design_reconstruction.py` writes `src/dsp/reconstruction_coefficients.inc` and `src/dsp/reconstruction_design.json`. A 21-sample pipeline delay at 8x (2.625 frames) is followed by a 4th-order Butterworth lowpass as two biquads at 8x, normalized to fs, with its cutoff (1.764485 fs; 77.8 kHz at 44.1 kHz) solved so the chain delay is exactly 14.7 frames. These are fitted, not AKM's circuit.
- **Implementation:** internal `detail::PipelineDelay` and `detail::Reconstruction` in `src/dsp/reconstruction.hpp`; transposed direct form II, double precision, states below 1e-15 flushed to zero.
- **Measured (whole linear chain, C++ output, 32 / 44.1 / 48 kHz):** delay 14.700 DAC frames; combined response within 0.058 dB of the reference from 0 to 20/44.1 fs (limit 0.2 dB); dispersion 0.013 frames.
- **Validation:** 8 C++ test cases: exact 21-sample delay, stability and unity DC gain, agreement with an independent direct-form-I reference, signals at 1/16 LSB unaffected by the flush, exact decay to zero, block partitioning, channels and reset, no allocations. `tools/tests/test_generated_files.py` checks the coefficient files, checksum, C++ constants, stability, and core gates. Ten deliberate mutations detected. Passes in `dev` and `asan`.
- **Updated:** 2026-10-16.

## De-emphasis

- **Purpose:** the DEM-selected 50/15 us de-emphasis at the DAC rate.
- **Requirements:** REQ-DEEM-01 to REQ-DEEM-03; evidence D-E01 to D-E03, I-E01; policies P-60 to P-62, P-64; API decisions O-07, O-08.
- **Design:** `tools/design_deemphasis.py` writes `src/dsp/deemphasis_coefficients.inc` and `src/dsp/deemphasis_design.json`. One first-order section per selection, exact unity DC gain, fitted to the analog reference from 20 Hz to the lesser of 20 kHz and 0.45 fs. Magnitude error 0.093 / 0.073 / 0.048 dB at 32 / 44.1 / 48 kHz (limit 0.1); phase error up to 19.0 / 15.0 / 12.6 degrees. Fitted, not AKM's coefficients.
- **Implementation:** internal `detail::Deemphasis` in `src/dsp/deemphasis.hpp`.
- **Behavior:**
  - DEM1, DEM0 = 00: 44.1 kHz set; 01: OFF, an exact bypass; 10: 48 kHz set; 11: 32 kHz set.
  - The DEM pins alone choose the set, which runs unchanged at any DAC rate, as on the chip. There is no automatic selection by DAC rate in the core; the plugin adapter may offer one by setting the DEM parameter.
  - A change of set resets the new section's state; re-selecting the current set changes nothing. There is no crossfade in the core: any smoothing would be host behavior in the plugin adapter.
- **Validation:** 11 C++ test cases: OFF bypass, closed-form impulse responses, unity DC and Nyquist gain, each selection within 0.1 dB of the analog curve at its own rate, a selection kept at another DAC rate, switching reset, re-selection keeps state, reset of both channels, block partitioning across selection changes, and no allocations. `tools/tests/test_generated_files.py` evaluates the committed sets against the analog reference independently. Six deliberate mutations detected. Passes in `dev` and `asan`.
- **Limitations:** de-emphasis is not yet connected to `DacCore`; that happens in `m2-core`. The chip's switching transient and coefficients are unknown (U-07).
- **Updated:** 2026-10-16.

## DAC core (linear Specification profile)

- **Purpose:** the public chip model: 20-bit PCM frames and pin events in, the reconstructed analog-equivalent signal out at 8x the DAC rate.
- **Requirements:** REQ-ARCH-02, REQ-ARCH-03, REQ-LAT-01, REQ-RT-01, REQ-RT-02; AGENTS.md section 13.2 gates 1 to 3; evidence D-M05, D-M06, I-R01; policies P-40, P-71, P-72, P-87, P-89; API decisions O-01 to O-03.
- **Usage** (`include/ak4522/dac_core.hpp`):

  ```cpp
  ak4522::DacCore core;
  core.prepare(ak4522::DacConfig{}, 512);            // off the audio thread
  // per block: frames.size() DAC frames in, 8 x frames.size() samples out
  auto result = core.process(frames, events, {left, right});
  ```

- **Signal path:** PCM to normalized double, de-emphasis, mute gain, 8x interpolator, 21-sample pipeline delay, reconstruction sections. Pin events are applied at their DAC frame through the control timeline; PD low outputs exact zeros and clears all filter state on both edges.
- **Measured through the public API** (CTest `dsp.dac_core_response_*`, 32 / 44.1 / 48 kHz): passband ripple 0.058 dB, -0.058 dB at 20/44.1 fs, -6.000 dB at 0.5 fs, stopband and images 43.47 dB to the 8x Nyquist frequency, delay 14.700 DAC frames, combined response 0.058 dB.
- **Behavior:** `prepare` validates the configuration and allocates; `process`, `reset`, `latency`, and `diagnostics` never allocate or throw. Blocks larger than the prepared maximum are processed in chunks. Rejected calls change no state. `latency()` reports 14.7 DAC frames. The NominalAnalog and Measured profiles and every analog imperfection flag are rejected with `invalidConfiguration` until they exist.
- **Validation:** 17 C++ test cases: configuration and call validation, output equal to the component chain for three DEM selections, de-emphasis before the mute ramp, mute to exact silence after ramp and filter delay, PD zeros and clean restart, SMUTE latched through PD, DEM change at its frame, identical output for single-frame, mixed, and random partitions and for chunk sizes from 7 to 4000 frames, chunk and ignored-event counters, reset, latency, move, full-scale bound, and no allocations. Twelve deliberate mutations detected. Passes in `dev` and `asan`, including the response gates.
- **Limitations:** no host-rate conversion yet (M3); no noise, distortion, or channel imperfections (M4).
- **Updated:** 2026-10-16.

## Sample-rate converter

- **Purpose:** convert between the host rate and the DAC rate (input side) and between 8x the DAC rate and the host rate (output side). Host behavior, not part of the chip model.
- **Requirements:** REQ-RATE-02, REQ-RATE-04; policies P-33, P-34, P-37, P-38.
- **Implementation:** internal `detail::Resampler` in `src/dsp/resampler.hpp`. Kaiser-windowed sinc designed for 125 dB, per-phase unity DC gain; exact phase rows for ratios such as 147/160 and 8/1, interpolated rows (4096 plus linear interpolation) otherwise. Rates are exact integers in micro-hertz; output m is input time m P / Q exactly; delay W input samples (`halfWidth()`). The kernel is designed in `prepare`, off the audio thread.
- **Validation:** 20 test cases across 14 rate pairs from both sides of the plugin path (including 44 123 Hz, which needs interpolated rows), measured by least-squares sine fits: passband error <= 0.005 dB and residual <= -110 dBFS at 12 frequencies per pair; stopband <= -120 dB where the input reaches the stopband; transition-band content folding only above the passband edge; exact output timing (impulse peak and sine phase); unity DC; exact output counts over 3 million inputs in random blocks; block invariance; reset; channels; no allocations. Twelve deliberate mutations detected. Passes in `dev` and `asan`.
- **Limitations:** not yet used by `HostProcessor` (next branch).
- **Updated:** 2026-10-16.

## Host-rate adaptation

- **Purpose:** the plugin-facing path: host audio at any supported host rate in, the emulated DAC output at the host rate out, with a fixed, exactly reported latency.
- **Requirements:** REQ-RATE-01, REQ-RATE-03, REQ-CTRL-01, REQ-LAT-01, REQ-LAT-03, REQ-VST-01; policies P-10, P-12, P-39, P-41, P-74, P-84; API decision O-09 (proposed).
- **Usage** (`include/ak4522/host_processor.hpp`):

  ```cpp
  ak4522::HostConfig config;              // host 48 kHz, DAC 44.1 kHz by default
  ak4522::HostProcessor host;
  host.prepare(config);                   // off the audio thread
  host.process(input, output, events);    // float or double, N frames in, N out
  auto frames = host.latency().reportedHostFrames;
  ```

- **Signal path:** input converter host to DAC rate (bypassed at equal rates), 20-bit quantization (or 16-bit, with optional TPDF dither), `DacCore`, output converter from 8x the DAC rate to the host rate, output FIFO. Host events become DAC-frame events through a fixed-capacity queue.
- **Latency:** the output converter's time origin is shifted so the total is whole host frames, reported as `reportedHostFrames`; the components are in `LatencyInfo`. Measured timing matches the report within 0.01 frame at 10 rate pairs.
- **Behavior:** at host rates whose band reaches the DAC images (for example 96 kHz with a 32 kHz DAC), the images appear in the output at the DAC's own stopband level, as in the analog output. `reset()` restores the post-prepare state but continues the dither stream (P-74).
- **Validation:** 19 test cases: configuration and call validation; latency bookkeeping; output timing within 0.01 frame by passband sine-phase fits at 10 rate pairs (equal rates, 44.1 / 48 in both directions, up to 192 kHz host, DAC 16 to 48 kHz including 37.8 kHz); no underruns with random blocks; level within 0.01 dB at the reference frequency; image level at high host rates; bitwise block-partition invariance with events; mute aligned with the reported latency and exact silence after; exact event-to-frame mapping at a grid point; pending events cleared by reset; dropped-event counting; float and double agreement; in-place processing; 16-bit resolution; seeded dither; non-finite and clipped counting; reset; no allocations. Thirteen deliberate mutations detected. Passes in `dev` and `asan`.
- **Limitations:** the full rate matrix, long runs, near-Nyquist sweeps, and alias containment are the next branch (`m3-verify`).
- **Updated:** 2026-10-16.

## Documentation process

How this record is kept current (REQ-DOC-01 to REQ-DOC-05):

- Substantial features get usage documentation under `docs/features/`, linked from the summary table (REQ-DOC-03). None exists yet because no feature is usable.
- Each feature branch reviews and updates `README.md` so available and planned features stay separate (REQ-DOC-04).
- Before a feature is marked complete, its implementation, tests, this record, detailed documentation, and the README must agree. `tools/check_docs.py` runs as the CTest `docs` test and checks evidence rows, ID citations, requirement tracing, the datasheet checksum, local links, and dash style. It does not check that recorded values match the datasheet; that remains a review step (REQ-DOC-05).
