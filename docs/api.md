# Public API proposal

Status: **proposal (M0)**. The headers in `include/ak4522/` declare this API. Only constant pin mappings and plain data types are defined; every processing function is declared but not implemented. Alpha releases may change any part of it.

Requirements addressed: REQ-ARCH-01, REQ-ARCH-02, REQ-SCOPE-02, REQ-RT-01, REQ-RT-02, REQ-LAT-01, REQ-VST-01.

## Layers

```text
Plugin adapter (JUCE or VST3 SDK, later)
        |
        v
HostProcessor        host rate: input SRC, quantization, latency alignment, output SRC
        |
        v
DacCore              DAC rate in, 8x DAC rate out: de-emphasis, soft mute,
        ^            interpolation, reconstruction, optional analog profile
        |
SerialDecoder        SDTI/LRCK/SCLK bits to Pcm20Frame (conformance and device use)
TimingValidator      nanosecond pin timing checks (conformance only)
```

Decoded PCM and serial input feed the same `DacCore` (REQ-SCOPE-02). The plugin path never serializes samples.

REQ-ARCH-01 names ten abstractions. Four are public types: `DacConfig`, `DacCore`, `Pcm20`, and `SerialDecoder`; `LatencyInfo` is a public data type. `Deemphasis`, `SoftMute`, `Reconstruction`, `AnalogProfile`, and `HostRateAdapter` are internal components under `src/`. They stay separable and individually tested, but users do not compose them directly, so they are not part of the public contract.

## Headers

| Header               | Contents                                                                                       |
| -------------------- | ---------------------------------------------------------------------------------------------- |
| `config.hpp`         | `DacConfig`, pin enums (`CmodePin`, `SerialFormat`, `DeemphasisSelect`), pin mapping functions, profile and policy enums |
| `pcm.hpp`            | `Pcm20`, `Pcm20Frame`, conversion and quantization functions                                   |
| `events.hpp`         | `ControlEvent` for PD, DEM, and SMUTE changes                                                  |
| `buffers.hpp`        | `StereoView<T>`, the non-owning planar buffer view                                             |
| `dac_core.hpp`       | `DacCore`, `ProcessResult`                                                                     |
| `host_processor.hpp` | `HostProcessor`, `HostConfig`, `InputResolution`                                               |
| `serial.hpp`         | `SerialDecoder`, `TimingValidator`, `PinEdge`                                                  |
| `latency.hpp`        | `LatencyInfo`                                                                                  |
| `diagnostics.hpp`    | `Diagnostics` counters                                                                         |
| `status.hpp`         | `Status`                                                                                       |
| `ak4522.hpp`         | Umbrella include                                                                               |

## Lifecycle

1. Construct (may allocate; not on the audio thread).
2. `prepare(config, ...)` validates the configuration, loads coefficients, allocates all buffers, and leaves the processor in the reset state. It returns a `Status`; on failure the processor is unprepared and `process` returns `Status::notPrepared`.
3. `process(...)` on the audio thread, any number of times.
4. `reset()` restores the state after `prepare` without allocating.
5. To change rates or configuration, call `prepare` again off the audio thread while processing is stopped (REQ-RATE-04 note on rate changes).

`process` and `reset` are `noexcept` and never allocate, lock, block, log, or design coefficients (REQ-RT-01). `tests/api_contract_test.cpp` checks the `noexcept` guarantees at compile time.

## Buffers

`StereoView<T>` holds two `std::span<T>`, one per channel, each contiguous. The frame count is the span length; both channels must match or the call returns `Status::channelSizeMismatch`. Interleaved buffers are not supported: hosts deliver planar audio, and the serial decoder produces frames one at a time.

Ownership stays with the caller. Views are valid only for the duration of the call.

In-place processing is supported when the input and output views refer to exactly the same memory. Partial overlap is not supported.

| Call                    | Input                  | Output written                     | Unit           |
| ----------------------- | ---------------------- | ---------------------------------- | -------------- |
| `DacCore::process`      | N `Pcm20Frame`         | 8N frames at `outputRate()`        | DAC frames     |
| `HostProcessor::process`| N host frames          | N host frames                      | host frames    |

Any N is valid, including 0 and 1. Blocks larger than the prepared maximum are processed in bounded internal chunks and counted in `Diagnostics::chunkedBlocks`.

`HostProcessor` supports `float` and `double` host buffers (REQ-VST-01). Internal state is double (P-86).

## Events

`ControlEvent` carries an offset, a pin, and a level. Offsets are in DAC frames for `DacCore` and host frames for `HostProcessor`, relative to the block start. Offsets must be less than the block length and must not decrease; otherwise the block is rejected with `eventOutOfRange` or `eventsNotSorted`.

Events on the same frame apply in `ControlType` order: PD, then DEM, then SMUTE (P-42). An SMUTE event at frame n changes direction before that frame's ramp update (P-52). Repeating the current level is accepted and ignored (P-53).

`HostProcessor` maps each host event to the first DAC frame at or after its input-stream time, including input SRC latency (P-41). The PD pin is emulated only through `ControlType::powerDown`; `reset()` is a host lifecycle operation and does not emulate PD (P-74).

## Latency and diagnostics

`latency()` returns `LatencyInfo` with each component in its own time domain, the total in seconds, and the integer frame count the plugin reports to the host (REQ-LAT-01, REQ-LAT-03). It is fixed between prepares.

`diagnostics()` returns a snapshot of monotonically increasing counters. It is safe to call from any thread.

## Serial conformance

`SerialDecoder::sclkRisingEdge(lrck, sdti)` is called once per SCLK rising edge with the levels latched on that edge (D-S05). It returns a completed `Pcm20Frame` at the end of each stereo frame, or reports an incomplete frame or insufficient bit clocks (P-21, P-22). The format is fixed until the next `prepare` (P-23).

`TimingValidator::observe(edge)` consumes timestamped pin transitions and counts violations of the limits in D-T01 to D-T08, D-C02 to D-C04, and D-R03. It reports; it does not simulate metastability (P-24).

## Design decisions

Decisions approved by the project owner on 2026-10-15 are marked *approved*.

| ID   | Decision                                                                                                                                                                                                  | Status   |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| O-01 | `DacCore` outputs at 8x the DAC rate. The output represents the analog signal including its images above the DAC Nyquist frequency; `HostProcessor`'s output SRC is the only stage that band-limits to the host rate. | approved |
| O-02 | `HostConfig::maxPendingEvents` (default 256) bounds events deferred past the current block by input SRC latency. Overflowing events are dropped and counted in `Diagnostics::droppedEvents`.               | approved |
| O-03 | `ModelProfile::measured` stays in the enum so it is stable; `prepare` returns `invalidConfiguration` for it until a measured profile exists.                                                                | approved |
| O-04 | The core is stereo only. The plugin adapter rejects mono layouts or documents a mono-to-stereo policy (REQ-VST-01).                                                                                       | proposed |
| O-05 | `SerialDecoder` and `TimingValidator` are value types with fixed-size state; their private members are placeholders until M1.                                                                            | proposed |
