# Feature record

What exists in this branch, as distinct from what AGENTS.md specifies. Implementation, validation, approval, and availability are tracked separately: a feature can be implemented without being approved or released (REQ-DOC-01, REQ-DOC-02).

Last substantive update: 2026-10-16. M0 to M5 were merged into `main` and approved by the project owner on 2026-10-16; the approved states are tagged `m0-approved` to `m5-approved`. The M5 approval includes the CPU reference machine (P-93).

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
| Sample-rate converter           | REQ-RATE-02, REQ-RATE-04              | implemented    | passing (see below)      | approved 2026-10-16 (`m3-approved`)                  | merged into `main`    |
| Host-rate adaptation            | REQ-RATE-01, REQ-RATE-03, REQ-CTRL-01, REQ-LAT-03 | implemented | passing (see below) | approved 2026-10-16 (`m3-approved`)           | merged into `main`    |
| Host-rate matrix verification, example, published responses | REQ-RATE-01 to REQ-RATE-04, REQ-RT-01 | implemented | passing (see below) | approved 2026-10-16 (`m3-approved`) | merged into `main` |
| Platform support                | REQ-PLATFORM-01                       | implemented    | passing on macOS, Windows ARM64 and x64, Linux (see below) | approved 2026-10-16 | merged into `main` |
| Specification profile lock      | REQ-SCOPE-03                          | implemented    | passing (see below)      | approved 2026-10-16 (`m4-approved`)                  | merged into `main`    |
| Weighted measurement tools      | REQ-NOISE-01, REQ-DIST-01             | implemented    | passing (see below)      | approved 2026-10-16 (`m4-approved`)                  | merged into `main`    |
| NominalAnalog noise             | REQ-NOISE-01, REQ-NOISE-02            | implemented    | passing (see below)      | approved 2026-10-16 (`m4-approved`)                  | merged into `main`    |
| NominalAnalog profile report    | REQ-NOISE-01, REQ-DIST-01             | implemented    | passing (see below)      | approved 2026-10-16 (`m4-approved`)                  | merged into `main`    |
| NominalAnalog output stage      | REQ-DIST-01, REQ-CHANNEL-01, REQ-ANALOG-01, REQ-ANALOG-02 | implemented | passing (see below) | approved 2026-10-16 (`m4-approved`)          | merged into `main`    |
| DacStream (pull and push, fractional DAC rates) | REQ-API-01, REQ-RATE-02 | implemented | passing (see below) | approved 2026-10-16 (`m5-approved`)              | merged into `main`    |
| Extended DAC-rate range (opt-in, up to 50.4 kHz) | REQ-CLOCK-02          | implemented    | passing (see below)      | approved 2026-10-16 (`m5-approved`)                  | merged into `main`    |
| C API                           | REQ-API-02                            | implemented    | passing (see below)      | approved 2026-10-16 (`m5-approved`)                  | merged into `main`    |
| Example output circuit (Fig.11), optional | REQ-ANALOG-02                 | implemented    | passing (see below)      | approved 2026-10-16                  | merged into `main`    |
| Packaging, integration guide, examples | REQ-API-03                     | implemented    | passing (see below)      | approved 2026-10-16 (`m5-approved`)                  | merged into `main`    |
| CPU benchmark                   | REQ-PERF-01                           | implemented    | budget met on the reference machine (see below) | approved 2026-10-16 (`m5-approved`) | merged into `main`    |
| Demo plugin (VST3, AU), separate project | REQ-VST-01, REQ-VST-02, REQ-LAT-03 | planned (after M5) | none                     | none                              | none                  |
| Measured profile                | AGENTS.md s.14                        | planned (M6, conditional on hardware) | none | none                         | none                  |
| ADC section and loopback        | REQ-SCOPE-04, REQ-ADC-01 to REQ-ADC-07 | in progress (M7): evidence, filters, `AdcCore` | passing so far (see below) | none                 | branch `m7-core`      |

Available on `main`: `DacCore` (DAC-rate PCM in, 8x the DAC rate out), `HostProcessor` (host-rate audio in and out), `DacStream` (a source at its own exact DAC rate, pull or push), the opt-in extended rate range to 50.4 kHz, a C API, CMake packaging with an integration guide and examples, the CPU benchmark, the optional Fig.11 example output circuit, the Specification profile, and the optional NominalAnalog profile. A demo plugin follows in a separate project.

## Build and test foundation

- **Purpose:** reproducible C++20 build, test runner, and offline Python tooling.
- **Contents:** CMake library target `ak4522::ak4522`; Catch2 v3.16.0 pinned by commit; CTest labels from test tags (`unit`, `dsp`, `soak`); JUnit XML output in `build/<preset>/test-results/`; presets `dev` (warnings as errors), `asan` (address and undefined-behavior sanitizers), `release`; uv-locked Python 3.12 environment in `tools/` with numpy, scipy, matplotlib.
- **Validation:** `dev` and `asan` presets build without warnings and pass all tests. See [validation.md](validation.md).
- **Real-time checks:** `tests/support/allocation_counter.hpp` replaces global `operator new` and `delete` in the test executable and counts allocations per thread while armed (REQ-RT-01). Its own tests confirm it detects plain, array-backed, and over-aligned allocations.
- **Limitations:** CI is defined but has not run. Platform results are under Platform support.
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
- **Limitations:** none in the mapping itself. Optional TPDF dither (P-12) and 16-bit input resolution are host-input features, offered by `HostProcessor` and, for float input, `DacStream`.
- **Updated:** 2026-10-16.

## Configuration validation

- **Purpose:** reject invalid chip and host configurations before processing, with a specific `Status`.
- **Requirements:** REQ-CLOCK-01, REQ-RATE-03; evidence D-04, D-05, D-C01 to D-C05, I-C01, I-C02; policies P-32, P-35; decision O-03.
- **Implementation:** internal `detail::validate` in `src/core/config_validation.hpp`, for `DacConfig` and `HostConfig`. `DacCore::prepare` and `HostProcessor::prepare` will call it when they are implemented (M2, M3); it is not callable through the public API yet.
- **Rules:**
  - DAC rate in [16000, 48000] Hz, inclusive; NaN and infinity rejected (`invalidDacRate`). With `allowExtendedRate`, up to 50 400 Hz (P-103).
  - When `masterClockHz` is set, it must be finite and equal the CMODE ratio (256, 384, or 512) times the DAC rate within 1e-9 relative (`invalidMasterClock`). This implies the datasheet MCLK range, so there is no separate range check.
  - `ModelProfile::measured` and out-of-range enum values are rejected (`invalidConfiguration`).
  - Host rate must be exactly 44.1, 48, 88.2, 96, 176.4, or 192 kHz (`unsupportedHostRate`); `maxBlockFrames` and `maxPendingEvents` must be non-zero.
- **Validation:** 14 test cases in `tests/config_validation_test.cpp`, including the nine p.9 Table 1 clock values, MCLK endpoints at both rate limits, the synchronization tolerance on both sides, and NaN inputs. All eight deliberate mutations are detected (see [validation.md](validation.md)).
- **Limitations:** the host/DAC rate-pair matrix (`unsupportedRatePair`) is defined in M3 with the SRC. Clock loss while running (D-C07) is a runtime fault reported by the timing validator, not a configuration check.
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
- **Usage:** `TimingValidator::prepare(cmode)`, then `observe(PinEdge{timeNs, pin, level})` for each transition in time order, and `advanceTo(timeNs)` at the end of a capture; read `violations(kind)` or `totalViolations()`. Also in the C API.
- **Checks** (inclusive limits): SCLK period >= 160 ns; SCLK high and low >= 65 ns; LRCK edge to first SCLK rise >= 45 ns; SCLK rise to LRCK edge >= 45 ns; an SCLK rise at the same time as an LRCK edge (reported instead of the two spacing checks); SDTI setup >= 25 ns and hold >= 40 ns; LRCK duty 45 to 55 %; LRCK 16 to 48 kHz; MCLK frequency and pulse width per CMODE (4.096 to 12.288 MHz and 27 ns; 6.144 to 18.432 MHz and 20 ns; 8.192 to 24.576 MHz and 15 ns); MCLK cycles per LRCK period equal to 256, 384, or 512; PD low >= 150 ns.
- **Behavior:** a level equal to the pin's current level is not a transition; edges earlier than the previous one are ignored; a check runs only when both of its edges were observed since reset; MCLK synchronization is counted only over LRCK periods that begin with MCLK running. Frequency limits have a 1e-9 relative tolerance (P-36).
- **Validation:** 22 test cases in `tests/timing_validator_test.cpp`: generated ideal clocks for every CMODE, 16/32/44.1/48 kHz, and 32/64/128fs with no violations; SCLK at 256fs and 48 kHz flagged; each limit at its value (passes) and just beyond (fails); coincidence in both orders; synchronization with and without MCLK; ignored edges; reset; no allocations. Passes in `dev` and `asan`.
- **Clock stop** (D-C07, P-31): with PD high, an observed MCLK, SCLK, or LRCK with no transition for more than 62.5 us counts once as `clockStopped`; not checked while PD is low, and the clocks have 62.5 us to resume after release. 6 test cases: a stop mid-stream, the limit exactly and just beyond, stops found by `advanceTo`, PD low and release, two stops counted twice. A mutation check detected 6 of 6 faults after the last test was added.
- **Limitations:** metastability and recovery are not modeled (P-24).
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
- **Requirements:** REQ-FILTER-01; evidence D-02, D-F01 to D-F05, D-F08, D-F09; policies P-87, P-88.
- **Design:** `tools/design_interpolator.py` (linear program, HiGHS) writes `src/dsp/interpolator_coefficients.inc` (exact hexadecimal doubles) and `src/dsp/interpolator_design.json` (parameters, versions, SHA-256, measured checks). 191 taps, linear phase, delay 95 samples at 8x = 11.875 DAC frames. Measured: passband ripple 0.058 dB, -0.058 dB at 20/44.1 fs, -6.000 dB at 0.5 fs, stopband 43.46 dB from 24.1/44.1 fs to the 8x Nyquist frequency. One coefficient set at every DAC rate, as on the chip; the datasheet's values apply at 44.1 kHz. The coefficients are fitted, not AKM's.
- **Implementation:** internal `detail::Interpolator8x` in `src/dsp/interpolator.hpp`: 8-phase polyphase FIR with a mirrored history buffer, double precision, stereo.
- **Validation:** the C++ impulse response is dumped by `ak4522_impulse_dump` and measured by `analyze_response.py` at 44.1 kHz, the datasheet's filter-table rate, against 1 kHz (CTest `dsp` label). `tools/tests/test_generated_files.py` checks that the coefficient file, design report, checksum, and the C++ length agree. 9 C++ test cases cover exact impulse response, symmetry, DC with bounded image ripple, channel independence, block partitioning, reset, full-scale bound, and no allocations. Seven deliberate mutations detected. Passes in `dev` and `asan`.
- **Limitations:** the 14.7-frame delay gate applies to the whole core and is checked in M2 once the pipeline delay and reconstruction exist.
- **Updated:** 2026-10-16.

## Analog reconstruction and pipeline delay

- **Purpose:** complete the linear chip model from input register to analog output: undocumented pipeline stages and the switched-capacitor plus continuous-time filters (D-03).
- **Requirements:** REQ-FILTER-02, REQ-LAT-02; evidence D-03, D-F06, D-F07, D-F10; policies P-81, P-85, P-87, P-89.
- **Design:** `tools/design_reconstruction.py` writes `src/dsp/reconstruction_coefficients.inc` and `src/dsp/reconstruction_design.json`. A 21-sample pipeline delay at 8x (2.625 frames) is followed by a 4th-order Butterworth lowpass as two biquads at 8x, normalized to fs, with its cutoff (1.764485 fs; 77.8 kHz at 44.1 kHz) solved so the chain delay is exactly 14.7 frames. These are fitted, not AKM's circuit.
- **Implementation:** internal `detail::PipelineDelay` and `detail::Reconstruction` in `src/dsp/reconstruction.hpp`; transposed direct form II, double precision, states below 1e-15 flushed to zero.
- **Measured (whole linear chain, C++ output, at 44.1 kHz against 1 kHz):** delay 14.700 DAC frames; combined response within 0.058 dB of the reference from 0 to 20/44.1 fs (limit 0.2 dB); dispersion 0.013 frames.
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
- **Measured through the public API** (CTest `dsp.dac_core_response_44100`, at the datasheet's 44.1 kHz conditions against 1 kHz): passband ripple 0.058 dB, -0.058 dB at 20/44.1 fs, -6.000 dB at 0.5 fs, stopband and images 43.47 dB to the 8x Nyquist frequency, delay 14.700 DAC frames, combined response 0.058 dB.
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
- **Requirements:** REQ-RATE-01, REQ-RATE-03, REQ-CTRL-01, REQ-LAT-01, REQ-LAT-03, REQ-VST-01; policies P-10, P-12, P-39, P-41, P-74, P-84; API decision O-09 (approved).
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
- **Validation:** 19 test cases: configuration and call validation; latency bookkeeping; output timing within 0.01 frame by passband sine-phase fits at 10 rate pairs (equal rates, 44.1 / 48 in both directions, up to 192 kHz host, DAC 16 to 48 kHz including 37.8 kHz); no underruns with random blocks; level within 0.01 dB at 1 kHz with a 44.1 kHz DAC (0.06 dB at other DAC rates, where 1 kHz falls elsewhere in the ripple); image level at high host rates; bitwise block-partition invariance with events; mute aligned with the reported latency and exact silence after; exact event-to-frame mapping at a grid point; pending events cleared by reset; dropped-event counting; float and double agreement; in-place processing; 16-bit resolution; seeded dither; non-finite and clipped counting; reset; no allocations. Thirteen deliberate mutations detected. Passes in `dev` and `asan`.
- **Limitations:** the full rate matrix, long runs, near-Nyquist sweeps, and alias containment are the next branch (`m3-verify`).
- **Updated:** 2026-10-16.

## Host-rate matrix verification, example, and published responses

- **Purpose:** close the M3 gate across every supported rate pair, and give users a working example and measured responses.
- **Requirements:** REQ-RATE-01 to REQ-RATE-04, REQ-RT-01, REQ-LAT-03; policies P-33, P-34, P-38, P-39, P-84.
- **Matrix tests** (`tests/matrix/host_matrix_test.cpp`, executable `ak4522_matrix_tests`, CTest label `matrix`, about 15 s in a release build): 30 pairs, every host rate (44.1, 48, 88.2, 96, 176.4, 192 kHz) against DAC rates 16, 32, 37.8, 44.1, and 48 kHz. Each pair: alignment within 0.01 frame; 4-second runs in random blocks with no underruns and no phase drift (below 1e-6 rad); tones at 0.40, 0.45, and 0.49 of the DAC rate, 0.45 and 0.49 of the host rate, and a two-tone pair, with everything except the intended components (the tone, converter transition-band fold-over or images, and DAC images) below -110 dBFS; bitwise block invariance; no allocations. Three deliberate faults (weakened converter stopband, reduced prefill, no alignment offset) are each detected.
- **Example:** `examples/host_processor_example.cpp`, built as `ak4522_host_processor_example` and run by CTest: a 1 kHz tone at 48 kHz through a 44.1 kHz DAC with SMUTE raised halfway; prints the latency (114 frames, 2.375 ms) and confirms silence after the mute.
- **Published responses:** [responses.md](responses.md), generated by `tools/publish_responses.py` from `ak4522_impulse_dump` (core) and `ak4522_host_response` (wrapper), separately as the M3 gate requires.
- **Updated:** 2026-10-16.

## Specification profile lock

- **Purpose:** prove that adding the NominalAnalog models does not change the clean Specification profile.
- **Requirements:** REQ-SCOPE-03; policy P-01.
- **Implementation:** `tests/golden_stimulus.hpp` defines a fixed stimulus (two tones and a fixed pseudo-random sequence, with DEM, SMUTE, and PD events) for `DacCore` and for `HostProcessor` at 48 / 44.1 kHz. `ak4522_golden_dump` wrote their outputs to `tests/data/golden_core.txt` and `golden_host.txt` (38 000 hexadecimal doubles) before any M4 change. `tests/golden_test.cpp` requires the current outputs to match within 1e-12, a tolerance that allows different multiply-add fusion on other compilers but no modeling change.
- **Validation:** a one-sample pipeline change and a 1e-9 gain error are each detected.
- **Regeneration:** only on a deliberate, reviewed change to the clean signal path: `build/dev/ak4522_golden_dump tests/data`.
- **Updated:** 2026-10-16.

## Weighted measurement tools

- **Purpose:** measure noise, dynamic range, and distortion the way [DS] p.5 specifies them, independently of the C++ code, for the NominalAnalog calibration and the M4 gate.
- **Requirements:** REQ-NOISE-01, REQ-DIST-01; evidence D-A01 to D-A03, D-A11 to D-A13; AGENTS.md section 13.2 gates 4 to 6.
- **Location:** `tools/ak4522_tools/weighting.py` (A-weighting per IEC 61672-1; ITU-R BS.468-4; CCIR-ARM, the 468 curve renormalized to 0 dB at 2 kHz with an average-responding meter; frequency-domain weighting with a 10 Hz to 20 kHz band) and `tools/ak4522_tools/metrics.py` (A-weighted S/N, CCIR-ARM S/N, dynamic range at -60 dBFS with a least-squares tone notch, SINAD with unweighted residual). Levels are relative to a full-scale sine (RMS 1/sqrt 2).
- **Validation:** 10 tests in `tools/tests/test_weighting.py`: A-weighting within 0.05 dB of the IEC 61672-1 table at 13 exact band frequencies from 10 Hz to 20 kHz; ITU-R 468 within 0.15 dB of the BS.468-4 table at 20 frequencies; CCIR-ARM 0 dB at 2 kHz and -5.6 dB at 1 kHz; weighting of coherent sines equal to the curve; band limiting; average-responding meter on a sine and on Gaussian noise (sqrt(pi)/2 of RMS); S/N of white noise against the analytic integral; SINAD with known harmonics; dynamic range equal to S/N after the notch.
- **Updated:** 2026-10-16.

## NominalAnalog noise

- **Purpose:** the DAC's analog noise floor, calibrated to the datasheet's typical S/N. Synthetic.
- **Requirements:** REQ-NOISE-01, REQ-NOISE-02; evidence D-A02, D-A03, D-A12, D-A13, D-A16, DISC-08; policies P-54, P-74, P-91, P-94, P-95.
- **Usage:** `DacConfig::profile = ModelProfile::nominalAnalog` with `analog.noise = true`; `analog.noiseSeed` sets the stream. The Specification profile rejects every analog option and stays bit-for-bit as before (golden lock).
- **Model:** white Gaussian noise at a fixed physical density, added at 8x after the mute and before the reconstruction sections; independent per channel; present while muted; absent in power-down; continued across host resets.
- **Measured** (CTest label `analog`, 10 s captures through `HostProcessor` at 44.1 kHz with `ak4522_capture` and `tools/measure_profile.py`): A-weighted S/N 100.006 dB (target 100 +/- 1); with a full-scale tone held muted by SMUTE, identical; CCIR-ARM 97.32 dB, the disclosed value for flat noise (96 dB typical, DISC-08).
- **Validation:** 8 C++ test cases: level against density at three rates; zero mean, variance within 1 %, Gaussian kurtosis; channel correlation below 0.005; seed reproducibility; NominalAnalog without options equal to the clean profile; noise while muted and none in power-down; reset not reseeding; no allocations. Seven deliberate mutations detected; an eighth (both channels drawing alternately from one generator) was dropped as equivalent, since Box-Muller's paired outputs are independent.
- **Limitations:** distortion, mismatch, crosstalk, and DC offset are next (`m4-imperfections`); the real noise spectrum, idle tones, and correlation are unknown (U-09).
- **Updated:** 2026-10-16.

## NominalAnalog output stage

- **Purpose:** the remaining datasheet typical imperfections, each optional and labeled synthetic, plus voltage scaling and profile metadata.
- **Requirements:** REQ-DIST-01, REQ-CHANNEL-01, REQ-ANALOG-01, REQ-ANALOG-02; evidence D-A01, D-A04, D-A05, D-A07, D-A15; policies P-96 to P-99; API decision O-10 (approved).
- **Usage:** with `profile = ModelProfile::nominalAnalog`, set any of `analog.distortion`, `channelMismatch`, `crosstalk`, `dcOffset` (and `noise`), or use `AnalogOptions::nominal()` for all except the DC offset. Shapes: `distortionShape` (`equal2nd3rd` default, `mostly3rd`, `mostly2nd`), `crosstalkShape` (`capacitive` default, `flat`), `mismatchMode` (`fixed` default, `perInstance`). `ak4522::outputVolts(x, vrefh)` converts a normalized sample to volts; `ak4522::describe(profile)` returns the profile's name, synthetic flag, availability, and summary.
- **Model:** at 8x after the reconstruction sections: channel gain, distortion polynomial, crosstalk between channels, DC offset. Power-down output stays exactly VCOM.
- **Measured** (44.1 kHz, `measure_profile.py`): S/(N+D) at 0 dBFS 90.004 / 90.006 / 90.002 dB for the three distortion shapes and 89.88 dB with the full nominal profile (target 90 +/- 1); dynamic range at -60 dBFS 99.89 dB A-weighted (100 +/- 1); isolation 110 dB at 1 kHz through the host path with noise (gate 7); fixed mismatch 0.200 dB.
- **Validation:** 14 C++ test cases: harmonic levels per shape within 0.05 dB of the design; compressive 3rd-harmonic phase; level scaling; crosstalk in both directions; fixed and per-instance mismatch (seeded, median 0.2 dB, capped at 0.5 dB, both signs); crosstalk level at 1 and 10 kHz and phase per shape; DC offset; power-down; block invariance with every option; no allocations; isolation through the host path; voltage scaling and profile descriptions. Python gates for SINAD per shape and dynamic range. Ten deliberate mutations detected.
- **Updated:** 2026-10-16.

## NominalAnalog profile report

- **Purpose:** the M4 typical-target report: the NominalAnalog profile measured against the datasheet's minimum and typical values with project tolerances.
- **Location:** [analog_profile.md](analog_profile.md), generated by `tools/report_profile.py` from `ak4522_capture` (release build).
- **Results:** S/(N+D) 89.87 dB; dynamic range 99.89 dB; A-weighted S/N 99.91 dB; CCIR-ARM 97.22 dB (DISC-08); 16-bit dynamic range 97.13 dB undithered and 94.30 dB with TPDF dither (98 dB typical); S/(N+D) from -60 to 0 dBFS; unweighted S/N 97.86 dB.
- **Updated:** 2026-10-16.

## Platform support

- **Purpose:** build and pass the full test suite on macOS and Windows, with Linux as a should (REQ-PLATFORM-01).
- **Approval:** approved by the project owner on 2026-10-16, on the platforms verified below. Untested platforms (Intel Macs, native x64 Windows hardware, Clang on Windows or Linux) are not covered by the approval.
- **Status:** verified at commit `4e41845` on macOS (Apple Silicon, AppleClang), Windows 11 ARM64 (MSVC 14.51, Build Tools 2026) for both ARM64 and x64 (x64 runs under emulation), and CentOS Stream 9 aarch64 (GCC 11.5). Not yet verified on macOS x86_64, on native x64 Windows hardware, or with Clang on Windows or Linux. 32-bit x86 is not supported.
- **Changes for portability:** exact 128-bit multiply-divide in portable C++ (`src/core/wide_math.hpp`) replacing `__int128`, which MSVC lacks (its `_udiv128` is also unavailable on ARM64); `_aligned_malloc` and `_aligned_free` in the test allocation counter under MSVC; configurations in every CMake build and test preset for multi-configuration generators; tool lookup in the Python report scripts that finds `.exe` files and configuration folders; `.gitattributes` keeping coefficient and golden data files byte-stable; `--config` in `tools/mutation_check.py`; `_CRT_SECURE_NO_WARNINGS` and an explicit float conversion for MSVC warnings; an `__extension__` typedef for GCC's pedantic check of the test-only `__int128`; an allocation-counter self-test that no longer depends on the standard library's internal allocations (MSVC debug containers allocate a proxy); `dev-x64` and `release-x64` presets on Windows; `.github/workflows/ci.yml` running the suite on macOS, Windows, and Linux (defined but not yet run).
- **Validation:** `tests/wide_math_test.cpp` checks the multiply-divide against hand-computed values and, where the compiler has 128-bit integers, against 200 000 random cases. The golden lock (1e-12) passes on every platform, so `HostProcessor` output agrees across them. Full suite: 214 tests on macOS and Linux, 213 on Windows (the 128-bit random comparison is compiled out under MSVC); host-rate matrix 5 of 5 on all four builds. Details in [validation.md](validation.md).
- **Updated:** 2026-10-16.

## DacStream

- **Purpose:** play a source that runs at its own DAC rate, such as an instrument or an emulated system, at a host rate: DAC frames in, the chip model, output SRC, host frames out (REQ-API-01).
- **Status:** implemented and merged into `main`; approved 2026-10-16 (`m5-approved`).
- **Usage:** [docs/api.md](api.md#dacstream). Configure `DacStreamConfig` with the host rate, an `ExactRate` DAC rate such as `{112000000, 2304}`, a sample format, and a mode. In pull mode, `render()` calls your `DacPullCallback` for the DAC frames it needs; in push mode, a producer thread calls `push()` and the audio thread calls `render()`.
- **Supported:** host rates of P-32; DAC rates as exact fractions inside 16 to 48 kHz, or to 50.4 kHz with the extended range, denominator up to 2^20 (P-100); 16-, 20-, and 24-bit integer and 32- and 64-bit float input; float and double output; every profile and analog option of `DacConfig`; DEM, SMUTE, and PD events in DAC frames.
- **Behavior:** after T host frames exactly ceil(T fs / H) DAC frames are consumed (P-101); latency alignment as `HostProcessor` without the input converter (P-39); push underruns play zeros and overruns drop frames, both counted (P-102). 24-bit input keeps its top 20 bits (I-P03).
- **Validation:** 13 test cases (`[stream]`): exact consumption at a 44 100 000 / 1001 Hz rate against four host rates over 400 random blocks; block-size invariance with events (gate 8); phase-delay timing within 0.01 frame of the reported latency and level within the response tolerance, at fractional and whole rates (gate 3); format equivalences (16-bit times 16, 24-bit floor over 16, exact float); saturation counts; invalid callback events; push and pull identical, also with the producer on another thread; underrun and overrun accounting; mismatched calls; reset; no allocations. Passes under the address and undefined-behavior sanitizers, and the push tests under ThreadSanitizer. A mutation check detected 14 of 14 deliberate faults.
- **Dither:** `inputDither` adds TPDF dither of +/-1 20-bit LSB to float input before quantization (P-12), off by default, seeded by `ditherSeed` and continuing across `reset()`; integer input is never dithered. Tested: a 0.3 LSB level averages to within 10% with dither and to zero without; reproducible per seed; independent channels; push and pull identical.
- **Updated:** 2026-10-16.

## Extended DAC-rate range

- **Purpose:** run sources slightly above the datasheet's 48 kHz, such as an emulated system at 112 MHz / 2304 = 48 611.1 Hz (REQ-CLOCK-02).
- **Status:** implemented and merged into `main`; approved 2026-10-16 (`m5-approved`).
- **Usage:** set `DacConfig::allowExtendedRate = true` (in `HostConfig::dac` or `DacStreamConfig::dac`). Rates up to 50 400 Hz are then accepted; without it they are rejected with `invalidDacRate`. `latency().outsideDocumentedRate` is true for any rate above 48 kHz.
- **Behavior:** not documented chip behavior (P-103). The model is unchanged: filters keep their normalized frequencies, so the response scales with the rate; de-emphasis keeps the selected set; soft mute still takes 1024 frames (20.3 ms at 50.4 kHz). The lower limit stays 16 kHz. `TimingValidator` still reports LRCK above 48 kHz.
- **Validation:** 6 test cases (`[extended]`): opt-in required; rejection above 50.4 kHz and below 16 kHz; latency flag in `DacCore` and `HostProcessor`; identical core output at 50.4 and 44.1 kHz; `HostProcessor` at 48 and 96 kHz hosts with a 50.4 kHz DAC, level within the response tolerance and timing within 0.01 frame; `DacStream` at 112 MHz / 2304 with exact frame consumption, level, and timing. A mutation check detected 4 of 4 faults. Documented-range gates are not claimed above 48 kHz.
- **Updated:** 2026-10-16.

## C API

- **Purpose:** use the library from C and from any language with a C foreign function interface (REQ-API-02).
- **Status:** implemented and merged into `main`; approved 2026-10-16 (`m5-approved`).
- **Usage:** `#include "ak4522/ak4522.h"`; see [docs/api.md](api.md#c-api). Handles for the host processor and the stream; plain configuration structs filled by `*_default` functions; status codes; caller-owned planar buffers; C pull callbacks.
- **Supported:** everything the C++ `HostProcessor` and `DacStream` offer, including every profile and analog option, exact and extended rates, all stream formats, push and pull, events, latency, and diagnostics.
- **Validation:** `tests/c_api/c_api_test.c`, compiled as C99 with warnings as errors, calls every function from C: NULL handling, status strings, host processing with a soft mute to exact silence, a C pull source at 112 MHz / 2304 with the extended range and an event, push with underrun and overrun accounting, format mismatch. `tests/c_api_equivalence_test.cpp` compares C and C++ output bit for bit in 16 configurations (now including the example circuit), each setting one field, and for a pull stream with events; checks defaults, master-clock and CMODE validation, dropped malformed events, and that processing does not allocate. A mutation check detected 14 of 14 faults in the wrapper after one test was added (swapped underrun and overrun counters first survived).
- **Also exposed:** the serial decoder and timing validator (`ak4522_serial_decoder_*`, `ak4522_timing_validator_*`) and the stream's dither option. Tested from C and against the C++ classes (all four serial formats with random framing; random pin edges producing at least eight kinds of violation). Mutation check of these additions: 7 of 8 detected; the survivor removes a range check that the C++ call repeats, so it changes nothing observable.
- **Limitations:** no ABI stability during alpha.
- **Updated:** 2026-10-16.

## Packaging, integration guide, and examples

- **Purpose:** let other projects use the library as it is, without this repository's build (REQ-API-03).
- **Status:** implemented and merged into `main`; approved 2026-10-16 (`m5-approved`).
- **Usage:** [docs/integration.md](integration.md). `cmake --install` installs the library, headers, and a `find_package(ak4522)` config with the target `ak4522::ak4522`; `add_subdirectory` and FetchContent also work. Static by default; `BUILD_SHARED_LIBS=ON` builds a shared library. Alpha compatibility: the same minor version only.
- **Examples** (each built and run by CTest, label `examples`): `host_processor_example.cpp` (effect), `dac_stream_example.cpp` (pull source at 112 MHz / 2304 with the extended range and a mute event), `dac_stream_push_example.cpp` (producer thread), `dac_core_example.cpp` (chip model alone, impulse delay), `c_api_example.c` (C).
- **Validation:** CTest label `package` builds `tests/package/consumer`, a separate project with a C++ and a C program, three ways: against this build installed to a prefix, through `add_subdirectory`, and against a shared library built and installed for the test; each program runs and checks its result. The shared case was confirmed to link the shared library.
- **pkg-config:** the install includes a relocatable `lib/pkgconfig/ak4522.pc` (not with MSVC); `pkg-config --static --cflags --libs ak4522` also names the C++ standard library for C programs. CTest `package.pkgconfig` compiles the C consumer with those flags, without CMake, where pkg-config exists.
- **Limitations:** no binary packages.
- **Updated:** 2026-10-16.

## CPU benchmark

- **Purpose:** measure the cost of one stereo instance against the budget of REQ-PERF-01 (P-93): a clean `HostProcessor` at 48 kHz host and 44.1 kHz DAC under 10% of the callback period at the 99.9th percentile for blocks of 64 frames or more.
- **Status:** implemented and merged into `main`; approved 2026-10-16 (`m5-approved`), including the reference machine.
- **Usage:** `cmake --preset release && cmake --build --preset release`, then `build/release/ak4522_benchmark [--check] [--seconds S]` on an otherwise idle machine. It times every callback after a warm-up and prints the median, 99.9th percentile, and maximum as a share of the period, and the callbacks that took longer than the period, for the budget case at 64 to 512 frames and for the nominal profile, a 192 kHz host, and `DacStream` at 48.611 kHz.
- **Reference machine (approved by the project owner on 2026-10-16):** Apple M1 Max, macOS 26.5.2, Apple clang 21.0.0, `release` preset, one instance, ordinary thread priority.
- **Result** (60 s of audio per case): budget case p99.9 3.4% at 64 frames and 2.5 to 2.9% at 128 to 512 frames; median 2.1%; no callback over the period. Maximums up to 10% are single callbacks interrupted by the operating system; the benchmark does not run at real-time priority.
- **Optimization:** the first run measured a median of 6.2% and p99.9 of 9.2% at 64 frames, just inside the budget. The output converter's kernel (about 640 taps at 8x the DAC rate) dominated; it now computes both channels in one pass with four partial sums each, which tripled throughput. Only the rounding of the sums changed: the golden lock (1e-12), the matrix, and every response gate pass. A new test checks that both channels are converted identically, after a mutation in the right channel's taps went unnoticed by the existing tests.
- **Updated:** 2026-10-16.

## Example output circuit (Fig.11)

- **Purpose:** the datasheet's example external circuit after each DAC output ([DS] p.16 Fig.11): AC coupling and an inverting op-amp stage, for hearing or measuring the DAC as that board would present it (REQ-ANALOG-02). Board behavior, not chip behavior; `DacCore` never applies it.
- **Status:** implemented and merged into `main`; approved by the project owner on 2026-10-16. Verified on macOS, Windows ARM64 and x64, and Linux.
- **Usage:** set `exampleOutputCircuit = true` in `HostConfig` or `DacStreamConfig` (options in `exampleCircuit`), or run `ExampleOutputCircuit` yourself, for example after `DacCore`, at any rate. In C: `example_output_circuit` and `example_circuit` in the configs, or `ak4522_circuit_*`.
- **Behavior** (P-104): 27 uF into 10 kohm and 10 uF into 27 kohm, both 0.589 Hz high-pass corners (I-O03), around a gain of -1.8, 5.1 dB, not the labeled 6 dB (I-O02, DISC-05). DC offsets are removed, which is what the coupling is for (D-A15). Options: `includeGain` (default on; off keeps the DAC's level, avoiding the 5.1 dB rise in a host), `invert` (default on, as drawn), `loadOhms` (an external load across the 27 kohm, default none). The op-amp, bias network, and supply are ideal: no noise, no bandwidth limit, no clipping. No latency is added.
- **Validation:** 10 test cases (`[circuit]`): corners and gains from the components; magnitude within 0.01 dB and phase within 0.5 degrees of the analog transfer function from 0.2 Hz to 40 kHz at 1, 48, and 192 kHz, with and without a load; -1.8 times the input at 1 kHz; DC removed to exactly zero; channels independent; float and double agree; invalid options rejected; unprepared input unchanged; no allocations; the `HostProcessor` and `DacStream` options equal the circuit applied to their plain output, with unchanged latency; the NominalAnalog +3 mV offset removed. C99 test and C/C++ equivalence. A mutation check detected 8 of 8 faults.
- **Limitations:** an idealized circuit; a real NJM5532 adds noise and has finite swing. Output level above 1.0 can clip in a host when the gain is kept.
- **Updated:** 2026-10-16.

## ADC filters (M7, in progress)

- **Purpose:** the ADC's decimation filter and DC-blocking high-pass filter, the signal path of the coming `AdcCore` (REQ-ADC-02).
- **Status:** implemented as internal components on branch `m7-filters`; `AdcCore` is not built yet, so nothing is usable from the public API.
- **Decimator** (P-105): 360-tap linear-phase FIR from 8x the ADC rate to the ADC rate, designed by `tools/design_decimator.py` (coefficients and report in `src/dsp/decimator_coefficients.inc`, `decimator_design.json`). Ripple 0.0048 dB to 19.76 kHz; -0.02, -0.06, and -6.0 dB at 20.02, 20.20, and 22.05 kHz; 80.55 dB from 24.34 kHz; 29.3125 frames of delay including a 55-sample pipeline. Fitted, not AKM's coefficients.
- **High-pass** (P-107): first order, -3 dB at 0.9 Hz at 44.1 kHz, scaling with fs; -0.46 dB at 2.7 Hz against the printed -0.5 (I-AD03).
- **Validation:** 8 C++ test cases (`[adc]`): impulse response against the coefficients and delay; symmetry and DC gain; level within 0.005 dB and delay within 0.01 frame at 1 to 19 kHz; tones from 24.4 to 176 kHz at least 80 dB down after decimation; block invariance; channel independence; reset; HPF points at 32, 44.1, and 48 kHz; DC removed to exactly zero; no allocations. Python checks that the committed coefficients match their report, checksum, and C++ constants and pass the gates. A mutation check detected 9 of 9 faults after a reset test was added.
- **Limitations:** content near multiples of 64fs (AD-08) cannot be represented with input at 8x the rate.
- **Updated:** 2026-10-16.

## AdcCore (M7, in progress)

- **Purpose:** the ADC section as a framework-independent core: normalized analog input at 8x the ADC rate in, 20-bit frames out (REQ-ADC-01, REQ-ADC-03, REQ-ADC-05).
- **Status:** implemented on branch `m7-core`; the SDTO encoder, host adapter, C API, NominalAnalog profile, and loopback adapter are still to come.
- **Usage:** `AdcConfig` (rate, extended range, CMODE, MCLK), `AdcCore::prepare(config, maxBlockFrames)`, then `process(input, events, output)` with `8 * n` input samples per channel for `n` output frames. `adcInputFromVolts()` converts a voltage across the inputs, or on one input against VCOM, to the normalized input. `latency()` reports the modeled 29.3125 frames, the nominal 29.3, and the 516-frame initialization.
- **Behavior** (P-108): decimator, HPF, 20-bit quantization with saturation to 7FFFFh and 80000h (counted). PD low gives "0" output and clears the filters; after release, and after `prepare` or `reset`, the output stays "0" for 516 frames while the filters run. DEM and SMUTE events have no effect. Specification profile only.
- **Validation:** 12 test cases (`[adc]`): configuration checks; 516 zero frames at startup, then the input with 29.3125 frames of delay within 0.01 and unity level at 1 kHz; zero input gives 00000h; over-range saturates at the full-scale codes and is counted; DC removed; PD low and release, with the restarted filters equal to a fresh core's; DAC pins ignored; block invariance with PD events; bad calls rejected; voltage conversion and latency report; `DacCore` output fed directly to `AdcCore` returns the signal after 44.0125 frames; no allocations. A mutation check detected 8 of 8 faults.
- **Limitations:** no NominalAnalog noise or distortion for the ADC yet; content near multiples of 64fs is not represented (AD-08); no initialization click (AD-10, no captures).
- **Updated:** 2026-10-16.

## Documentation process

How this record is kept current (REQ-DOC-01 to REQ-DOC-05):

- Substantial features get usage documentation under `docs/features/`, linked from the summary table (REQ-DOC-03). None exists yet because no feature is usable.
- Each feature branch reviews and updates `README.md` so available and planned features stay separate (REQ-DOC-04).
- Before a feature is marked complete, its implementation, tests, this record, detailed documentation, and the README must agree. `tools/check_docs.py` runs as the CTest `docs` test and checks evidence rows, ID citations, requirement tracing, the datasheet checksum, local links, and dash style. It does not check that recorded values match the datasheet; that remains a review step (REQ-DOC-05).
