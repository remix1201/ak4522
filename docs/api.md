# Public API

Status: **implemented**. The headers in `include/ak4522/` declare this API; this file describes its contract and records the design decisions behind it, and the [integration guide](integration.md) shows how to use it. Alpha releases may change any part of it.

Requirements addressed: REQ-ARCH-01, REQ-ARCH-02, REQ-SCOPE-02, REQ-RT-01, REQ-RT-02, REQ-LAT-01, REQ-VST-01.

## Layers

```text
Plugin adapters (separate projects)
        |
        v
HostProcessor        host rate: input SRC, quantization, latency alignment, output SRC;
                     paths dac, loopback (DAC into ADC), and adc
DacStream            a source at its own DAC rate: pull or push, latency alignment, output SRC
        |
        v
DacCore              DAC rate in, 8x DAC rate out: de-emphasis, soft mute,
        ^            interpolation, reconstruction, optional analog profile
        |
AdcCore              8x ADC rate in, ADC rate out: decimation, high-pass, quantization,
                     optional analog profile
SerialDecoder        SDTI/LRCK/SCLK bits to Pcm20Frame (conformance and device use)
SerialEncoder        Pcm20Frame to SDTO bits
TimingValidator      nanosecond pin timing checks (conformance only)
ak4522.h             C API over all of the above
```

Decoded PCM and serial input feed the same `DacCore` (REQ-SCOPE-02). The plugin path never serializes samples.

Of the ten abstractions REQ-ARCH-01 names, `DacConfig`, `DacCore`, `Pcm20`, `SerialDecoder`, and `LatencyInfo` are public types, and the host-rate adapter is the public `HostProcessor`. `Deemphasis`, `SoftMute`, `Reconstruction`, and the analog profile are internal components under `src/`: separable and individually tested, but not composed by users, so not part of the public contract. The public API adds `DacStream`, `AdcCore`, `SerialEncoder`, `TimingValidator`, `ExampleOutputCircuit`, and the C API.

## Headers

| Header               | Contents                                                                                       |
| -------------------- | ---------------------------------------------------------------------------------------------- |
| `config.hpp`         | `DacConfig`, pin enums (`CmodePin`, `SerialFormat`, `DeemphasisSelect`), pin mapping functions, and profile and policy enums |
| `pcm.hpp`            | `Pcm20`, `Pcm20Frame`, and conversion and quantization functions                                   |
| `events.hpp`         | `ControlEvent` for PD, DEM, and SMUTE changes                                                  |
| `buffers.hpp`        | `StereoView<T>`, the non-owning planar buffer view                                             |
| `adc_core.hpp`       | `AdcCore`, `AdcConfig`, `AdcInputMode`, `AdcLatencyInfo`, `adcInputFromVolts()` (M7)           |
| `dac_core.hpp`       | `DacCore`, `ProcessResult`                                                                     |
| `host_processor.hpp` | `HostProcessor`, `HostConfig`, `InputResolution`                                               |
| `dac_stream.hpp`     | `DacStream`, `DacStreamConfig`, `ExactRate`, `DacSampleFormat`, `DacStreamMode`, `DacPullBlock`, `DacPullCallback`, `PushResult` |
| `serial.hpp`         | `SerialDecoder`, `SerialEncoder` (SDTO, M7), `TimingValidator`, `PinEdge`                                                  |
| `output_circuit.hpp` | `ExampleOutputCircuit`, `ExampleCircuitOptions` (Fig.11 board circuit, optional)                 |
| `latency.hpp`        | `LatencyInfo`                                                                                  |
| `diagnostics.hpp`    | `Diagnostics` counters                                                                         |
| `status.hpp`         | `Status`                                                                                       |
| `ak4522.hpp`         | Umbrella include                                                                               |
| `ak4522.h`           | C API (REQ-API-02); see below                                                                  |

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
| `DacStream::render`     | (from the source)      | N host frames                      | host frames    |
| `DacStream::push`       | N DAC frames           | (queued)                           | DAC frames     |
| `AdcCore::process`      | 8N samples at `inputRate()` | N `Pcm20Frame`                | ADC frames     |
| `ExampleOutputCircuit::process` | N samples, in place | N samples                      | any rate       |

Any N is valid, including 0 and 1. Blocks larger than the prepared maximum are processed in bounded internal chunks and counted in `Diagnostics::chunkedBlocks`.

`HostProcessor` and `DacStream` support `float` and `double` host buffers (REQ-VST-01); `AdcCore` takes `double` input. Internal state is double (P-86).

## Events

`ControlEvent` carries an offset, a pin, and a level. Offsets are in host frames for `HostProcessor`, in DAC frames for `DacCore` and `DacStream`, and in ADC frames for `AdcCore`, relative to the block start. Only PD affects the ADC. Offsets must be less than the block length and must not decrease; otherwise the block is rejected with `eventOutOfRange` or `eventsNotSorted`.

Events on the same frame apply in `ControlType` order: PD, then DEM, then SMUTE (P-42). An SMUTE event at frame n changes direction before that frame's ramp update (P-52). Repeating the current level is accepted and ignored (P-53).

`HostProcessor` maps each host event to the first DAC frame at or after its input-stream time, including input SRC latency (P-41). The PD pin is emulated only through `ControlType::powerDown`; `reset()` is a host lifecycle operation and does not emulate PD (P-74).

## AdcCore

`AdcCore` is the ADC section (REQ-ADC-01): `prepare(AdcConfig, maxBlockFrames)`, then `process(input, events, output)` with `8 * n` normalized input samples per channel at 8x the ADC rate for `n` 20-bit output frames. 1.0 is the positive full-scale peak, 1.5 V at VREFH = 5 V across the inputs or on one input (`adcInputFromVolts`). The path is decimator, HPF, and quantization with saturation (P-105 to P-108); PD low gives "0", and release, `prepare`, and `reset` are followed by 516 "0" frames (AD-10). `AdcConfig` shares the DAC's rate and clock rules and takes the NominalAnalog profile with `AnalogOptions` (not DC offset) and `AdcInputMode` (P-110). `latency()` reports 29.3125 modeled frames, 29.3 nominal, and the 516-frame initialization. `DacCore`'s output is valid `AdcCore` input, so a loopback needs no conversion. `SerialEncoder` produces SDTO from its frames (P-109).

## ADC paths in HostProcessor

`HostConfig::path` selects what host audio passes through (P-111): `HostPath::dac` (the default), `HostPath::loopback` (DAC, then the DAC's 8x analog output into `AdcCore`, REQ-ADC-06), or `HostPath::adc` (host audio as the ADC's analog input at 8x the rate). The ADC uses the `dac` configuration's rate, clocks, profile, and analog options except DC offset, and `adcInputMode`. `latency().coreDacFrames` reports the path's chip delay (14.7, 44.0125, or 29.3125 frames) and `reportedHostFrames` stays exact. `Diagnostics::adcClippedSamples` counts saturated ADC codes.

## DacStream

`DacStream` plays a source that runs at its own DAC rate, such as an instrument or an emulated system, at a host rate (REQ-API-01). There is no input converter; the source's samples go straight to `DacCore`, then the output converter and the same latency alignment as `HostProcessor` (P-39).

- **Rate.** `ExactRate{numerator, denominator}` in Hz, for example `{112000000, 2304}`. It must lie in the DAC range (above 48 kHz only with `dac.allowExtendedRate`, P-103) and the denominator may be at most 2^20 (P-100). Position is kept in exact integers, so long runs do not drift.
- **Formats.** `int16` (scaled by 16), `int20`, `int24` (top 20 bits kept, I-P03), `float32`, and `float64` (quantized per REQ-PCM-03; optional TPDF dither with `inputDither`, P-12). Integer values outside the format's range saturate and count as clipped.
- **Pull mode.** `render(output)` asks `DacPullCallback` for exactly the DAC frames the host frames need: after T host frames, ceil(T fs / H) in total (P-101). The callback runs on the audio thread and fills the view matching the format; it may add events with DAC-frame offsets. The same source sequence gives the same output for any render block sizes.
- **Push mode.** A producer thread calls `push(frames, events)`; the audio thread calls `render(output)`. The queue is single-producer, single-consumer, wait-free, and bounded. Missing frames play as zeros and count as `sourceUnderrunFrames`; frames that do not fit are dropped and count as `sourceOverrunFrames` (P-102). `dacFramesFor(n)` tells a producer how many frames the next n host frames will consume, and `queuedFrames()` and `freeFrames()` report the queue.
- **Events** are in DAC frames relative to the frames they accompany, in both modes. Invalid events from a pull source are dropped and counted.
- **Latency.** DAC frame j is presented at host time j H / fs and appears `latency().reportedHostFrames` host frames later.

`prepare()`, `reset()`, and destruction run off the audio thread and never concurrently with `render()` or `push()`. `render()` and `push()` may run on two different threads.

## C API

`include/ak4522/ak4522.h` exposes the library to C99 and later and to any language with a C foreign function interface (REQ-API-02): `HostProcessor`, `DacStream`, `AdcCore`, the serial decoder and encoder, the timing validator, and the example output circuit. It adds no behavior; results are identical to the C++ classes.

- **Handles.** `ak4522_host_create`, `ak4522_stream_create`, `ak4522_adc_create`, and the other `*_create` functions return opaque handles (NULL when out of memory); `*_destroy` frees them and ignores NULL. Create, prepare, and destroy off the audio thread.
- **Configuration.** Plain structs mirroring the C++ ones. Fill them with `ak4522_dac_config_default`, `ak4522_host_config_default`, or `ak4522_stream_config_default`, then change fields. Enum fields take the `AK4522_*` constants; out-of-range values are rejected by `prepare`. `master_clock_hz` 0 means not given. The stream's exact rate is `dac_rate_numerator / dac_rate_denominator`; its `dac.dac_rate` is ignored.
- **Status.** Every fallible call returns `ak4522_status`: the C++ `Status` values, plus `AK4522_INVALID_ARGUMENT` (a required pointer was NULL, or an event's type or value is out of range) and `AK4522_TOO_MANY_EVENTS`. `ak4522_status_string` describes any value.
- **Buffers and events.** Planar channel pointers and a frame count, owned by the caller. Events are `ak4522_event` arrays; each call accepts at most `max_events_per_call` of them (default 256), converted into preallocated storage so the call stays allocation-free.
- **Pull sources.** `ak4522_pull_fn` receives an `ak4522_pull_block` whose pointers for the configured format are set and the others NULL. Events written beyond `event_capacity`, or with an invalid type or value, cause the block's events to be dropped and counted.
- **Conformance tools.** `ak4522_serial_decoder_*` and `ak4522_timing_validator_*` wrap `SerialDecoder` and `TimingValidator`, with `AK4522_SERIAL_*`, `AK4522_PIN_*`, and `AK4522_TIMING_*` constants.
- **ADC.** `ak4522_adc_*` wraps `AdcCore`; `ak4522_serial_encoder_*` wraps `SerialEncoder`; the host config's `path` and `adc_input_mode` select `HostPath` and `AdcInputMode`.
- **Threads.** As the C++ classes: `ak4522_stream_push_*` may run on a producer thread while `ak4522_stream_render_*` runs on the audio thread; diagnostics and queue queries from any thread.

## Example output circuit

`ExampleOutputCircuit` models the datasheet's Fig.11 board circuit: two 0.589 Hz high-pass sections around a gain of -1.8 (P-104). It is not part of the chip: `DacCore` never applies it. `prepare(sampleRate, options)` designs it for any rate; `process` works in place on float or double. `HostConfig` and `DacStreamConfig` apply it at the host rate when `exampleOutputCircuit` is true; reported latency is unchanged.

## Latency and diagnostics

`latency()` returns `LatencyInfo` with each component in its own time domain, the total in seconds, and the integer frame count the plugin reports to the host (REQ-LAT-01, REQ-LAT-03). It is fixed between prepares.

`diagnostics()` returns a snapshot of monotonically increasing counters. It is safe to call from any thread.

## Serial conformance

`SerialDecoder::sclkRisingEdge(lrck, sdti)` is called once per SCLK rising edge with the levels latched on that edge (D-S05). It returns a completed `Pcm20Frame` at the end of each stereo frame, or reports an incomplete frame or insufficient bit clocks (P-21, P-22). The format is fixed until the next `prepare` (P-23).

`SerialEncoder::sclkFallingEdge(lrck)` produces SDTO for the ADC's frames in all four modes (P-109).

`TimingValidator::observe(edge)` consumes timestamped pin transitions and counts violations of the limits in D-T01 to D-T08, D-C02 to D-C04, and D-R03, and clocks that stop while PD is high (D-C07, P-31); `advanceTo(time)` checks a time without an edge. It reports; it does not simulate metastability (P-24).

## Design decisions

Decisions approved by the project owner on 2026-10-15 are marked *approved*.

| ID   | Decision                                                                                                                                                                                                  | Status   |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| O-01 | `DacCore` outputs at 8x the DAC rate. The output represents the analog signal including its images above the DAC Nyquist frequency; `HostProcessor`'s output SRC is the only stage that band-limits to the host rate. | approved |
| O-02 | `HostConfig::maxPendingEvents` (default 256) bounds events deferred past the current block by input SRC latency. Overflowing events are dropped and counted in `Diagnostics::droppedEvents`.               | approved |
| O-03 | `ModelProfile::measured` stays in the enum so it is stable; `prepare` returns `invalidConfiguration` for it until a measured profile exists.                                                                | approved |
| O-04 | The core is stereo only. The plugin adapter rejects mono layouts or documents a mono-to-stereo policy (REQ-VST-01).                                                                                       | proposed |
| O-05 | `SerialDecoder` and `TimingValidator` are value types with fixed-size state, defined in M1 (`m1-serial`, `m1-timing`). `SerialDecoder`'s event comments were clarified. | implemented |
| O-10 | Added `DistortionShape`, `CrosstalkShape`, `MismatchMode`, and their fields in `AnalogOptions`, with `AnalogOptions::nominal()`; `ak4522/analog.hpp` with `outputVolts()` (REQ-ANALOG-01) and `describe(ModelProfile)` (profile metadata). Approved by the project owner on 2026-10-16. | approved |
| O-09 | Added `HostConfig::ditherSeed` (reproducible TPDF dither, P-12, P-91), `Diagnostics::hostUnderruns` (must stay 0), and `quantizeTo16Bit()` (16-bit host input, REQ-PCM-02). Approved by the project owner on 2026-10-16. | approved |
| O-08 | Removed `DeemphasisSwitching` and `DacConfig::deemphasisSwitching` (the 5 ms host crossfade). It was host behavior inside the core; DEM changes now always behave as the chip baseline (P-64). Smoothing, if needed, belongs to the plugin adapter. Approved by the project owner on 2026-10-16. | approved |
| O-07 | Removed `DeemphasisRatePolicy` and `DacConfig::deemphasisRatePolicy` ("follow DAC rate"). The core selects de-emphasis from the DEM pins only; any automatic selection belongs to the plugin adapter (AGENTS.md REQ-DEEM-03). Approved by the project owner on 2026-10-16. | approved |
| O-06 | `TimingValidator::prepare` takes only `CmodePin`; the proposed `dacRate` argument had no pin-level check to drive. Added `TimingViolation::lrckFrequency` (16 to 48 kHz, D-04), `TimingViolation::mclkNotSynchronized` (MCLK cycles per LRCK cycle equal the CMODE ratio, D-C05), and `totalViolations()`. Approved by the project owner on 2026-10-16. | approved |
