# Feature record

What exists in this branch, as distinct from what AGENTS.md specifies. Implementation, validation, approval, and availability are tracked separately: a feature can be implemented without being approved or released (REQ-DOC-01, REQ-DOC-02).

Last substantive update: 2026-10-16. M0 was merged into `main` and approved by the project owner on 2026-10-16; the approved state is tagged `m0-approved`.

## Summary

| Feature                         | Requirements                          | Implementation | Validation               | Approval                          | Availability          |
| ------------------------------- | ------------------------------------- | -------------- | ------------------------ | --------------------------------- | --------------------- |
| Build and test foundation       | AGENTS.md s.13, M0                    | implemented    | passing (see below)      | approved 2026-10-16 (`m0-approved`) | merged into `main` |
| Evidence and assumptions ledgers | AGENTS.md s.2, M0                    | implemented    | reviewed against [DS]    | approved 2026-10-16 (`m0-approved`) | merged into `main` |
| Public API proposal             | REQ-ARCH-01, REQ-ARCH-02, REQ-RT-02   | in progress (declared, not implemented) | compile-time contract checks pass | approved 2026-10-16 (`m0-approved`) | merged into `main` |
| PCM mapping and quantization    | REQ-PCM-01 to REQ-PCM-03              | implemented    | passing (see below)      | not yet approved                  | merged into `main`    |
| Serial decoder                  | REQ-SERIAL-01, REQ-SERIAL-03          | implemented    | passing (see below)      | not yet approved                  | merged into `main`    |
| Serial timing validator         | REQ-SERIAL-02                         | implemented    | passing (see below)      | not yet approved                  | merged into `main`    |
| Configuration validation        | REQ-CLOCK-01, REQ-RATE-03             | implemented    | passing (see below)      | not yet approved                  | merged into `main`    |
| Reset and control timeline      | REQ-RESET-01, REQ-RESET-02, REQ-CTRL-01 | implemented  | passing (see below)     | not yet approved                  | merged into `main`    |
| Soft mute                       | REQ-MUTE-01, REQ-MUTE-02              | implemented    | passing (see below)      | not yet approved                  | merged into `main`    |
| De-emphasis                     | REQ-DEEM-01 to REQ-DEEM-03            | planned (M2)   | none                     | none                              | none                  |
| Interpolation and reconstruction | REQ-FILTER-01, REQ-FILTER-02, REQ-LAT-02 | planned (M2) | none                   | none                              | none                  |
| Host-rate adaptation            | REQ-RATE-01 to REQ-RATE-04, REQ-CTRL-01 | planned (M3) | none                     | none                              | none                  |
| NominalAnalog profile           | REQ-NOISE-01, REQ-NOISE-02, REQ-DIST-01, REQ-CHANNEL-01 | planned (M4) | none     | none                              | none                  |
| VST3 plugin adapter             | REQ-VST-01, REQ-VST-02, REQ-LAT-03    | planned (M5)   | none                     | none                              | none                  |
| Measured profile                | AGENTS.md s.14                        | planned (M6, conditional on hardware) | none | none                         | none                  |
| ADC section and loopback        | REQ-SCOPE-04, REQ-ADC-01 to REQ-ADC-07 | planned (M7)  | none                     | none                              | none                  |

None of the emulator's signal processing is usable yet. PCM conversion exists as a library function but is not yet connected to any processing path.

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
- **Requirements:** REQ-SERIAL-02; evidence D-T01 to D-T08, D-04, D-C02 to D-C05, D-R03, I-T01; policies P-24, P-36; API decision O-06 (proposed).
- **Usage:** `TimingValidator::prepare(cmode)`, then `observe(PinEdge{timeNs, pin, level})` for each transition in time order; read `violations(kind)` or `totalViolations()`.
- **Checks** (inclusive limits): SCLK period >= 160 ns; SCLK high and low >= 65 ns; LRCK edge to first SCLK rise >= 45 ns; SCLK rise to LRCK edge >= 45 ns; an SCLK rise at the same time as an LRCK edge (reported instead of the two spacing checks); SDTI setup >= 25 ns and hold >= 40 ns; LRCK duty 45 to 55 %; LRCK 16 to 48 kHz; MCLK frequency and pulse width per CMODE (4.096 to 12.288 MHz and 27 ns; 6.144 to 18.432 MHz and 20 ns; 8.192 to 24.576 MHz and 15 ns); MCLK cycles per LRCK period equal to 256, 384, or 512; PD low >= 150 ns.
- **Behavior:** a level equal to the pin's current level is not a transition; edges earlier than the previous one are ignored; a check runs only when both of its edges were observed since reset; MCLK synchronization is counted only over LRCK periods that begin with MCLK running. Frequency limits have a 1e-9 relative tolerance (P-36).
- **Validation:** 22 test cases in `tests/timing_validator_test.cpp`: generated ideal clocks for every CMODE, 16/32/44.1/48 kHz, and 32/64/128fs with no violations; SCLK at 256fs and 48 kHz flagged; each limit at its value (passes) and just beyond (fails); coincidence in both orders; synchronization with and without MCLK; ignored edges; reset; no allocations. Passes in `dev` and `asan`.
- **Limitations:** clock loss while PD is high (D-C07) needs a time-out policy and is not reported. Metastability and recovery are not modeled (P-24).
- **Updated:** 2026-10-16.

## Documentation process

How this record is kept current (REQ-DOC-01 to REQ-DOC-05):

- Substantial features get usage documentation under `docs/features/`, linked from the summary table (REQ-DOC-03). None exists yet because no feature is usable.
- Each feature branch reviews and updates `README.md` so available and planned features stay separate (REQ-DOC-04).
- Before a feature is marked complete, its implementation, tests, this record, detailed documentation, and the README must agree. `tools/check_docs.py` runs as the CTest `docs` test and checks evidence rows, ID citations, requirement tracing, the datasheet checksum, local links, and dash style. It does not check that recorded values match the datasheet; that remains a review step (REQ-DOC-05).
