# AGENTS.md: AKM AK4522VF DAC emulator

## 0. Overarching Rules and References

- This is an alpha build. Backward compatibility is not required for APIs, presets, serialized state, or internal formats. Update affected code, tests, and documentation together. Do not add compatibility layers unless explicitly requested.
- Create a dedicated feature branch before implementing a new feature or improvement. Keep changes focused and avoid unrelated refactoring.
- Before merging, run the applicable build, tests, and DSP verification checks. Report failures or known deviations. Do not weaken acceptance criteria merely to make tests pass.
- Do not merge or push changes unless explicitly requested.
- The AK4522 datasheet is the primary hardware reference. Keep the PDF and any extracted text or page images local and excluded from version control. Commit original project documentation, source citations, and implementation decisions.
- Clearly distinguish documented hardware behavior, inferred behavior, project policies, and hardware measurements. Do not invent undocumented internals or claim bit-exact hardware fidelity without evidence.
- Preserve real-time audio safety: no allocation, blocking, locks, file I/O, or coefficient generation on the audio thread.
- Keep the C++ core independent of plugin frameworks. Put host-specific behavior in adapters.
- Do not commit credentials, local recordings, generated build files, or temporary analysis artifacts.

## 1. Mission and working rules

Build an independent, reusable C++20 stereo DAC emulation library. The library is the product: it MUST stay generic and usable however its users need, as an audio effect, inside an instrument or emulator that generates samples at its own rate, in offline tools, or from other languages through a C API. A specific downstream project is a user of the library, never its design target. A demo plugin shows the library in use but does not define it; it lives in a separate sibling project that consumes the library as any outside user would. Model the DAC section of the AKM AK4522VF first. The ADC section follows as a separate core in milestone M7 (section 17); a complete sound card is out of scope. Keep the library independent of JUCE, VST SDK headers, GUI frameworks, and host APIs. This repository contains no plugin framework code or dependency. Plugin adapters, including the demo plugin, live in separate projects and may use JUCE or the VST3 SDK.

The mandatory baseline is a documented, specification-constrained behavioral model. Hardware-characterized fidelity is a later, separately identified profile. Do not describe either an invented delta-sigma modulator or a filter fitted to a few limits as a bit-exact reconstruction of AKM silicon.

Use MUST for mandatory requirements, SHOULD for preferred implementations, and MAY for optional features. Do not use em dashes in project documentation. Work in incremental, buildable milestones. Do not silently relax acceptance criteria to make tests pass.

Before implementation, inspect existing repository instructions and build conventions. Preserve unrelated changes. Implement and test the core before adding a plugin GUI. Never invent measurements, coefficients, hardware captures, or undocumented chip behavior. When a requirement cannot be established, record the uncertainty and implement an explicitly labeled policy rather than blocking unrelated work.

## 2. Primary evidence and traceability

Primary source [DS]: `AK4522.PDF`, Asahi Kasei AK4522, document **M0020-E-01, 1998/10**, 18 pages. AK4522VF is the 24-pin VSOP ordering variant, rated -10 to +70 degrees C [DS p.2]. Page references below use the printed page numbers, which match PDF page numbers.

The copy used for this project was inspected directly, including the filter table and serial/mute diagrams. Its SHA-256 is:

```text
0db2cd2fe7a989570f01610ea13249576371f7d5fff84864d8a95f7014b0adec
```

For a repository checkout, keep a permitted local copy at `reference/AK4522.PDF`, or document its external location and this checksum. This specification is self-contained for implementation planning; consult the PDF when resolving diagram details. Secondhand summaries of the datasheet are not evidence.

Each DSP constant, coefficient set, and acceptance test MUST identify one of:

- **D: documented** in [DS], with page/table/figure.
- **I: inferred** from documented facts, with derivation.
- **P: project policy**, chosen for deterministic software behavior.
- **M: measured**, with capture ID, device/board conditions, calibration, and uncertainty.

Maintain `docs/evidence.md`, `docs/assumptions.md`, and `docs/validation.md`. Track requirement ID, source, confidence, implementation location, test, result, and outstanding discrepancy. Preserve minimum, typical, and maximum distinctions. A typical value is a nominal target, not a guaranteed limit for every physical unit.

## 3. Scope and fidelity profiles

**REQ-SCOPE-01:** Implement stereo signed PCM input, DAC filtering, de-emphasis, digital soft mute, reset behavior, analog-output representation, and explicit sample-rate conversion for host integration.

**REQ-SCOPE-02:** Provide two input front ends sharing the same core: decoded stereo PCM frames for audio use, and a serial SDTI/LRCK/SCLK decoder for conformance tests and device-style applications. Do not serialize and deserialize every plugin audio sample unnecessarily.

**REQ-SCOPE-03:** Provide a deterministic `Specification` profile with documented controls and fitted linear response. Noise and analog imperfections MUST have independent enable flags. The default plugin preset uses the Specification profile, de-emphasis off, mute off, 44.1 kHz DAC rate, 20-bit input, and noise/imperfections off. Defaults are P, not power-on register values.

A separately selectable `NominalAnalog` profile may target typical noise, distortion, channel behavior, and voltage scaling. Label it synthetic until measurements support it. A later `Measured` profile must identify its particular chip and surrounding circuit. An experimental explicit modulator must never be necessary for the baseline.

**REQ-SCOPE-04:** Model the ADC section as a separate `AdcCore` after the DAC baseline (M7, section 17). ADC behavior belongs only to that core.

Do not add the ADC's 0.9 Hz high-pass filter, ADC 29.3-frame delay, ADC 80 dB stopband rejection, or ADC 516-frame initialization interval to the DAC path [DS pp.6,10,12]. The ADC does not affect DAC operation [DS p.12]. External board buffers, AC coupling, clock jitter, and supply contamination are separate optional models.

## 4. Documented DAC facts

### 4.1 Structure and operating conditions

| ID   | Documented behavior                                                                                              | Source                     |
| ---- | ---------------------------------------------------------------------------------------------------------------- | -------------------------- |
| D-01 | Stereo 20-bit delta-sigma DAC; 128x oversampling                                                                 | p.1                        |
| D-02 | Block diagram includes an 8x interpolator before each delta-sigma modulator                                      | p.1                        |
| D-03 | Analog filtering comprises second-order switched-capacitor filtering plus second-order continuous-time filtering | pp.1,16                    |
| D-04 | Supported DAC sample-rate range is 16 to 48 kHz                                                                  | pp.1,7                     |
| D-05 | MCLK ratios are 256fs, 384fs, or 512fs                                                                           | pp.3,9                     |
| D-06 | Analog supply 4.5 to 5.5 V; digital supply 2.7 V minimum, 5 V typical, maximum VA                                | p.4 recommended conditions |
| D-07 | DAC output is single-ended and centered about VCOM; VCOM is VA/2                                                 | pp.3,16                    |

Do not infer proprietary modulator order, quantizer levels, dither, coefficient precision, internal rounding, or exact SCF/CTF poles from these facts. The 128x statement is not a requirement that the plugin run an arbitrary one-bit oscillator at 128fs. An equivalent baseband model is acceptable if its observables satisfy this document and its limitations are stated.

### 4.2 Digital filter and combined response

Conditions: fs = 44.1 kHz, de-emphasis OFF, VA = 4.5 to 5.5 V, VD = 2.7 to 5.5 V, Ta = 25 degrees C [DS p.6]. Response reference frequency is 1 kHz. The source says passband and stopband frequencies scale with fs.

| ID    | Quantity                                  | Datasheet value and status |
| ----- | ----------------------------------------- | -------------------------- |
| D-F01 | Digital filter passband at -0.06 dB       | 0 to 20.0 kHz              |
| D-F02 | Digital filter -6 dB point                | 22.05 kHz                  |
| D-F03 | Digital stopband begins                   | 24.1 kHz                   |
| D-F04 | Digital passband ripple                   | +/-0.06 dB maximum         |
| D-F05 | Digital stopband attenuation              | 43 dB minimum              |
| D-F06 | DAC group delay                           | 14.7 / fs seconds, typical |
| D-F07 | Digital plus analog response, 0 to 20 kHz | +/-0.2 dB, typical         |

Use normalized boundaries `20/44.1 * fs`, `0.5 * fs`, and `24.1/44.1 * fs`. At 48 kHz these are approximately 21.7687, 24, and 26.2313 kHz. Do not fix the passband to 20 kHz at every DAC rate. The precise combined analog response away from the published conditions is not specified; treating it as invariant in normalized frequency is P.

The stopband boundary lies above the input Nyquist frequency. Verify it in the interpolator's high-rate domain, including spectral images. A base-rate FFT alone cannot verify this property.

Note 11 defines the DAC delay from loading both channels' 20-bit input register to analog output [DS p.6]. Do not blindly add a 14.7-frame delay after a filter already providing that latency. The table is under “DAC Digital Filter,” but the note refers to analog output; preserve this interpretation ambiguity in the latency report. The ADC's zero group-delay-distortion entry does not establish zero DAC group-delay distortion.

### 4.3 Analog performance

Unless otherwise specified: Ta = 25 degrees C, VA = VD = VREFH = 5 V, grounds = 0 V, fs = 44.1 kHz, SCLK = 64fs, 1 kHz signal, 20-bit data, measurement band 10 Hz to 20 kHz [DS p.5].

| ID    | DAC quantity                             |     Minimum |         Typical |     Maximum |
| ----- | ---------------------------------------- | ----------: | --------------: | ----------: |
| D-A01 | S/(N+D)                                  |       80 dB |           90 dB | unspecified |
| D-A02 | Dynamic range, -60 dB output, A-weighted |       95 dB |          100 dB | unspecified |
| D-A03 | S/N, A-weighted                          |       95 dB |          100 dB | unspecified |
| D-A04 | Interchannel isolation                   |       90 dB |          110 dB | unspecified |
| D-A05 | Interchannel gain mismatch               | unspecified |          0.2 dB |      0.5 dB |
| D-A06 | Gain drift                               | unspecified | 20 ppm/degree C | unspecified |
| D-A07 | Output voltage                           |    2.65 Vpp |         2.9 Vpp |    3.15 Vpp |
| D-A08 | Load resistance                          |      5 kohm |     unspecified | unspecified |
| D-A09 | Load capacitance                         | unspecified |     unspecified |       25 pF |
| D-A10 | Power-supply rejection                   | unspecified |           50 dB | unspecified |

Relevant footnotes: 16-bit DAC dynamic range is 98 dB; zero input S/N is 100 dB regardless of resolution. CCIR-ARM S/N is 96 dB for each converter. PSR measurement applies 1 kHz, 50 mVpp to VA and VD while holding VREFH constant [DS p.5 notes 5,6,8,9]. Do not substitute A-weighting for CCIR-ARM or treat their numbers as unweighted noise specifications.

Source discrepancy: p.5 labels output voltage `0.6 x VREFH`, while p.16 describes nominal `0.58 x VREFH`; 2.9 Vpp at 5 V agrees with 0.58. Adopt 2.9 Vpp and 0.58 for nominal scaling, retain the discrepancy in evidence records, and never silently change the source.

## 5. PCM and serial input contract

**REQ-PCM-01:** Internally represent decoded samples as sign-extended 20-bit values in `int32_t`, range [-524288, 524287]. Normalized audio is `code / 524288.0`. Thus 0x80000 maps to -1, 0x7FFFF maps to `1 - 2^-19`, and 0 maps to zero relative to VCOM [DS p.16]. Use defined unsigned masks and explicit sign extension; avoid undefined signed shifts or overflow.

**REQ-PCM-02:** Mode 0 and 16-bit I2S map signed 16-bit samples to 20-bit scale by multiplication by 16. Preserve their four zero low bits. Do not reinterpret 16-bit words as quiet 20-bit data.

**REQ-PCM-03:** Float input conversion is P: sanitize NaN/Inf to zero with a diagnostic counter, clamp to representable limits, multiply by 2^19, round to nearest with explicitly implemented ties-to-even, and saturate. Test the half-LSB boundaries. Optional TPDF dither before this quantization is a user-facing host-input feature, off by default, not a documented chip characteristic. Quantize once after host-to-DAC rate conversion. Do not re-quantize every intermediate stage to 20 bits without evidence.

### 5.1 Serial mode table

All modes are MSB first and two's complement. SDTI is latched on SCLK rising edges; SDTO changes on falling edges [DS p.9]. SDTO belongs to the ADC and is outside this emulator's output contract.

| Mode | DIF1 | DIF0 | DAC SDTI format               | LRCK left/right | Minimum SCLK                                |
| ---- | ---: | ---: | ----------------------------- | --------------- | ------------------------------------------- |
| 0    |    0 |    0 | 16-bit LSB/right justified    | H/L             | 32fs                                        |
| 1    |    0 |    1 | 20-bit LSB/right justified    | H/L             | 40fs                                        |
| 2    |    1 |    0 | 20-bit MSB/left justified     | H/L             | 40fs                                        |
| 3    |    1 |    1 | I2S, up to 20 meaningful bits | L/H             | 32fs for 16-bit or at least 40fs for 20-bit |

**REQ-SERIAL-01:** Right-justified data ends with its LSB immediately before the next LRCK channel boundary. Ignore leading padding. Left-justified data starts at the channel boundary; ignore trailing padding. I2S starts its MSB one bit clock after the LRCK transition. At the transition, the bit may still belong to the previous channel. Never discard it merely because LRCK changed. Use Figures 1 to 4 [DS pp.9-10] as framing fixtures.

At 32fs in mode 3, only the top 16 of the 20 bits are transferred; the low four are truncated [DS p.9 and Fig.4]. Decode the final transmitted bit across the LRCK boundary correctly. At adequate word lengths, consume the top 20 bits and ignore remaining padding. Treat an incomplete stereo frame as invalid input, not as two newly valid samples.

Figure 3 prints “23 22” at the start of the right-channel DAC word although Table 2 and the remaining labels specify 20 bits. Treat this as a diagram inconsistency and use a 20-bit MSB numbered 19 for both channels, pending contrary evidence.

**REQ-SERIAL-02:** Provide strict timing validation in the test adapter: SCLK period >=160 ns, high/low pulse widths >=65 ns, LRCK-to-SCLK-rise and SCLK-rise-to-LRCK spacing >=45 ns, SDTI setup >=25 ns and hold >=40 ns; LRCK duty cycle 45 to 55% [DS p.7]. A timestamped validation adapter MAY report violations without simulating metastability. The real-time frame API does not need nanosecond pin simulation.

**REQ-SERIAL-03:** Unsupported clocks, partial words, or active format changes produce a documented status in strict mode. Never invent analog damage, random corruption, or undocumented recovery behavior. For PCM software usage, reconfigure formats only during reset/preparation.

## 6. Clocks and sample rates

**REQ-CLOCK-01:** Distinguish `hostRate`, `dacRate`, and internal processing rates in every interface and formula. Accept DAC rates within [16000, 48000] Hz, or the extended range of REQ-CLOCK-02 when it is enabled. Test 16, 32, 44.1, and 48 kHz. Host rates outside this interval must not silently become DAC rates.

CMODE low selects 256fs, high selects 384fs, and unconnected selects 512fs [DS pp.3,9]. Represent this three-state input with an enum, not a Boolean. At 44.1 kHz the exact derived clocks are 11.2896, 16.9344, and 22.5792 MHz. Calculate from the ratio rather than rounded table examples. MCLK ranges are 4.096-12.288 MHz, 6.144-18.432 MHz, and 8.192-24.576 MHz respectively. Minimum MCLK high/low widths are 27, 20, and 15 ns [DS p.7].

LRCK must be synchronized with MCLK; phase is not critical. Internal timing synchronizes to LRCK at power-up. All external clocks are required while PD is high; the datasheet warns of abnormal operation if they stop [DS p.9]. Strict emulation reports a clock fault. Software does not model excessive electrical current. Clock ratios alone must not create audible “clock color.”

**REQ-RATE-01:** The plugin path is host PCM -> input anti-alias SRC -> DAC-rate PCM quantization -> DAC model -> host-rate reconstruction/SRC -> host output. Both conversions require anti-alias treatment. Provide a same-rate path with unnecessary rate conversions bypassed while retaining the DAC response.

**REQ-RATE-02:** Preserve SRC phase, FIR/IIR state, event timing, and pending samples across blocks. Use a rational or high-precision accumulator with no long-term sample-count drift. Never reset SRC state per callback. Host buffers of arbitrary size must work, including 0, 1, odd sizes, and blocks split across fractional rate boundaries.

**REQ-RATE-03:** Initial host support: 44.1, 48, 88.2, 96, 176.4, and 192 kHz. Reject unsupported configurations before playback with a clear status. Add other rates only with tests. Low host Nyquist limits observable output bandwidth; do not claim that a 44.1 kHz host preserves a 48 kHz DAC's entire analog bandwidth.

**REQ-RATE-04:** Use an SRC whose measured added in-band ripple is <=0.005 dB and stopband rejection >=120 dB in the declared supported conversion bands, with a published transition band. These are P quality targets. If a near-Nyquist conversion cannot meet both bandwidth and cost goals, expose and document its passband limitation. Do not overwrite the DAC's intentionally modest 43 dB image-rejection target with claims about the SRC.

**REQ-CLOCK-02:** An opt-in extended DAC-rate range MAY accept rates above 48 kHz up to 50 400 Hz (48 kHz plus 5%), upper side only. This is P, not documented chip behavior: the datasheet gives no behavior outside 16 to 48 kHz [DS pp.1,7], and real parts often run somewhat beyond their rated clocks, but no particular part is guaranteed to. It is off by default; without it, out-of-range rates are rejected as before. When enabled, the model applies its normalized-frequency policy (section 4.2) unchanged, validation reports that the rate is outside the documented range, and `describe()` and latency reports state it. Documented-range gates apply only within 16 to 48 kHz. The option relaxes validation only; it adds no new behavior to the core.

Rate changes require off-thread preparation and an explicit reset/restart or a preallocated crossfade strategy. Transport stop/start alone must not arbitrarily re-seed noise or reset the chip. Treat host lifecycle reset separately from a PD transition.

## 7. DSP architecture

Suggested tree:

```text
include/ak4522/        Public framework-independent API
src/core/             PCM, events, reset, mute, de-emphasis
src/dsp/              Interpolation, reconstruction, SRC
src/analog/           Optional voltage/noise/nonlinearity models
src/serial/           Serial decoder and timing validator
tests/                Unit, DSP, integration tests
 tools/               Coefficient design, analysis, benchmarks
 reference/           Source manifest and permitted reference material
 docs/                Evidence, assumptions, validation, architecture
```

**REQ-ARCH-01:** Core abstractions SHOULD include `DacConfig`, `DacCore`, `Pcm20`, `SerialDecoder`, `Deemphasis`, `SoftMute`, `Reconstruction`, `AnalogProfile`, `HostRateAdapter`, and `LatencyInfo`. Names may follow existing conventions, but responsibilities must remain separable. Stereo channels have independent filter state, shared clock/control timing, and explicit optional crosstalk.

**REQ-ARCH-02:** Provide non-real-time `prepare(config, maxBlockSize)` returning validation status; `process(...) noexcept` with preallocated buffers and timestamped controls; deterministic `reset`; and read-only latency/diagnostic access. Specify frames, channels, stride, aliasing/in-place support, ownership, and buffer capacity. Use spans or equivalent bounded views. Separate DAC-frame processing from host-rate processing.

**REQ-ARCH-03:** Process signal path, initially: quantized PCM -> de-emphasis -> mute envelope -> interpolation/equivalent DAC response -> analog reconstruction -> optional analog imperfections -> host output. The exact internal ordering of mute and de-emphasis is undisclosed; this ordering is P and must be replaceable. Post-mute analog noise remains audible when enabled. Host safety fades and output trims belong outside the chip model.

**REQ-FILTER-01:** Start with a reproducibly designed 8x polyphase interpolator and an identified reconstruction model. An equivalent multirate implementation is acceptable. Save design script, constraints, coefficients, numeric precision, checksum, measured response, and latency. Do not select a generic steep “high quality” filter without matching the AK4522 targets.

Fit the documented ripple, transition, stopband, and nominal delay together. Do not assume brick-wall behavior. Do not arbitrarily require 120 dB rejection from the chip interpolator. Stopband details beyond the documented edge and bound remain uncertain; report measured response over the full implemented high-rate Nyquist interval and mark which interval is an engineering interpretation of the table.

**REQ-FILTER-02:** The second-order SCF and second-order CTF descriptions constrain topology class, not pole frequencies or damping. Use stable effective sections with labeled fitted parameters, or a combined transfer model, and avoid double-counting attenuation already included in a fitted combined response. A fourth-order Butterworth is not documented. Use double-precision design/reference calculations and preferably double internal state; float host buffers remain supported.

**REQ-MOD-01:** An explicit 128fs modulator is optional research work. Require bounded states, overload stability tests, idle-tone analysis, noise spectrum measurements, and adequate anti-alias filtering before returning to the host. Its topology is an approximation unless independently verified. Do not inject ultrasonic shaped noise and then allow it to alias into audible “character.”

## 8. De-emphasis

The DAC uses an IIR de-emphasis filter with 50/15 microsecond time constants [DS p.11].

| DEM1 | DEM0 | Selection |
| ---: | ---: | --------- |
|    0 |    0 | 44.1 kHz  |
|    0 |    1 | OFF       |
|    1 |    0 | 48 kHz    |
|    1 |    1 | 32 kHz    |

**REQ-DEEM-01:** Preserve this exact pin mapping. OFF must bypass de-emphasis magnitude/phase processing. The analog reference response is `H(s) = (1 + s*15e-6) / (1 + s*50e-6)`, unity at DC and asymptotic gain 0.3, approximately -10.4576 dB. This transfer is a reference inferred from the time constants, not the chip's unpublished coefficients.

**REQ-DEEM-02:** Generate three documented digital IIR coefficient sets with unity DC gain and stable poles. Choose a fitting method against the reference response; record its magnitude and phase error across the operating band. A plain bilinear transform is an approximation and must not be called exact. Project fitting target: <=0.1 dB magnitude error from 20 Hz to the lesser of 20 kHz and 0.45 times the selected nominal rate. If unmet, improve the fit or report the deviation; do not conceal it.

**REQ-DEEM-03:** The DEM pins alone select the coefficient set, as on the chip [DS p.11]. Selecting 44.1 kHz while running at another DAC rate retains the 44.1 kHz coefficient set evaluated at the actual DAC rate. This models fixed coefficients as P because detailed mismatch behavior is not supplied. The core has no automatic selection by DAC rate. The plugin adapter MAY offer one as host behavior that sets the DEM pin parameter when the rate changes, the way a system controller would. Do not silently design a new hardware mode.

De-emphasis switching transient behavior and state retention are undocumented. Baseline policy resets the newly selected section at the event boundary. The core implements only this chip behavior; any smoothing of DEM changes is host behavior and belongs to the plugin adapter, outside the core.

## 9. Soft mute and control timing

**REQ-MUTE-01:** SMUTE high initiates digital attenuation to silence over 1024 LRCK cycles. Low restores unity over 1024 cycles for a full-scale transition [DS p.11, Fig.5]. Durations: 32 ms at 32 kHz, approximately 23.21995 ms at 44.1 kHz, and 21.33333 ms at 48 kHz. Count DAC frames, not host samples or callback invocations.

**REQ-MUTE-02:** Reversing SMUTE before a transition finishes must return toward the new target from the current attenuation without a discontinuous gain jump. The exact law and partial reversal duration are not numerically specified. Baseline P is linear amplitude with a fixed step of 1/1024 per DAC frame: `gNext = clamp(g + direction/1024, 0, 1)`. Full travel is 1024 updates and partial reversal retraces the remaining distance at that slope. Use an integer 0..1024 ramp position to guarantee endpoints. The schematic line in the attenuation diagram is not proof of a linear-dB ramp to negative infinity.

Define an event at frame n to change direction before that frame's ramp update. Starting from unity, the 1024th updated frame reaches zero. Keep processing stream and filters while muted. Do not freeze the device, halt clocks, or gate residual analog noise. Propagate the mute through downstream filtering, accounting for group delay and settling. Full digital mute does not mean an instantaneous, numerically zero analog output.

**REQ-CTRL-01:** Apply controls on a deterministic DAC-frame timeline. Map a host event to the first DAC frame at or after its input-stream time, with input SRC latency explicitly accounted for. Split blocks at event boundaries without allocating. Define ordering for simultaneous events: reset first, configuration validation second, de-emphasis third, mute fourth. Tests must show identical event behavior across block partitions.

## 10. Reset and startup

**REQ-RESET-01:** PD is active low. Reset is required after power-up. The minimum low pulse is 150 ns in pin validation [DS pp.7,12]. While reset is asserted, clocks may stop. The DAC resumes normal operation on release; do not wait the ADC's 516 LRCK cycles.

**REQ-RESET-02:** Software startup performs a deterministic reset. Baseline P clears chip filter states, serial partial words, internal histories, and ramp state. While PD is low, output zero relative to VCOM in the clean model. On release, latch external DEM/SMUTE selections, initialize the envelope to zero if SMUTE is high or unity if low, and accept new frames. The detailed silicon state initialization and reset output trajectory are not specified. Document this policy and keep host SRC state handling separate.

The datasheet warns of clicks at PD edges and recommends external analog muting [DS p.12]. Do not synthesize an arbitrary click and call it accurate. Optional measured reset transients need hardware captures. Plugin startup/shutdown safety fades may suppress clicks, but must be labeled external and bypassable in conformance tests. Do not apply a 516-frame mute as a workaround.

## 11. Analog representation and imperfections

**REQ-ANALOG-01:** The normal plugin output is dimensionless, zero-centered audio. Diagnostic voltage output is separate: nominal `Vout = VCOM + 1.45*x` at VREFH = 5 V, where x is normalized signed DAC signal. General nominal peak scale is `0.29*VREFH`. The positive digital endpoint is one LSB below the negative endpoint's magnitude. Do not add the 2.5 V common-mode offset to a host audio buffer.

**REQ-ANALOG-02:** Physical output DC offsets are described only as a few millivolts [DS p.16]. Default offset is zero; any synthetic offset has a documented value and enable flag. Preserve DC through the chip path. Optional board AC coupling is a separate filter with stated component values or fitted response.

The external circuit in Figure 11 is inverting and described as an optional 6 dB-gain amplifier, while the drawing shows 10 kohm input/18 kohm feedback and approximately 5.2 Vpp output from 2.9 Vpp [DS p.16]. Do not bake its inversion, gain, op-amp, or coupling capacitors into the bare DAC. If implementing that example, calculate from the circuit and flag the nominal-gain discrepancy.

**REQ-NOISE-01:** Use a deterministic, locally seeded PRNG per instance/channel. Persist seed/profile in presets; resetting noise reproducibility is a documented policy. Never use global RNG state. Calibrate noise by integrating the resulting output spectrum through the actual measurement band and weighting, including the effects of reconstruction and host conversion. Do not equate 100 dB A-weighted S/N with an arbitrary unweighted RMS amplitude of 1e-5.

**REQ-NOISE-02:** A synthetic nominal model may use broadband noise shaped by a declared filter, adjusted to 100 dB A-weighted S/N. Unknown noise spectral density, idle tones, signal dependence, and interchannel correlation remain uncertainties. No injected noise is required in the clean profile. Keep analog noise after digital mute, and verify its statistics when the signal is muted.

**REQ-DIST-01:** A nominal distortion model may fit S/(N+D) near 90 dB under declared conditions. No polynomial order or harmonic distribution is specified by [DS]. Use conservative, stable, bounded modeling and measure combined noise plus distortion, not THD alone. Report drive level, harmonics, noise integration, and bandwidth. A single 1 kHz SINAD target is not proof of accurate nonlinear behavior.

**REQ-CHANNEL-01:** Optional mismatch/crosstalk models must specify amplitude and phase versus frequency. Typical mismatch 0.2 dB and isolation 110 dB are calibration targets, not instructions to add exactly that error to every instance. Clean mode uses matched independent channels. Apply no random stereo width, drift, or modulation without a named profile.

Supply noise, temperature, load effects, jitter, clipping recovery, and intersample overload behavior are uncharacterized beyond the listed metrics. Leave them disabled until a meaningful model and tests exist. Do not add saturation solely for subjective “warmth.” Preserve reconstruction overshoot unless a separately justified analog limit applies.

## 12. Latency, VST integration, and real-time safety

**REQ-LAT-01:** Track latency by component and time domain: input SRC, chip model, output SRC, optional alignment. Express total seconds and host frames, and distinguish group delay from causal buffering latency. At 44.1 kHz, 14.7/fs is about 333.333 microseconds; at 48 kHz it is 306.25 microseconds. These are nominal chip values, not whole-plugin latency.

**REQ-LAT-02:** Measure core delay from its declared input-register epoch using impulse response and passband phase slope. Separate analog/de-emphasis dispersion from fixed scheduling delay. Fit the nominal 14.7-frame reference within a P tolerance of +/-0.1 DAC frame with de-emphasis and imperfections off; if the chosen response cannot meet this causally, report the failure and revise the model. Do not hide extra chip delay by subtracting it in metadata.

**REQ-LAT-03:** The plugin reports an integer host latency consistent with its actual aligned output. For a fractional fixed delay L, an alignment design may report ceil(L) and add the remaining fractional delay using a validated filter, including that filter's own causal delay. A scalar delay cannot cancel frequency-dependent analog phase. Align dry/bypass paths to the same reference and test with impulses and phase sweeps. Never simply round metadata while leaving unmatched audio timing.

**REQ-VST-01:** Support stereo input/output and both float and double host processing. Reject unsupported bus layouts explicitly or document a mono-to-stereo policy. Version the serialized schema and document parameter IDs. Alpha changes may break compatibility; update all affected code and tests together. Save DAC rate, format policy, DEM state, mute, profile, optional analog flags, seed, and gains. Reject incompatible older state with a clear status rather than silently misinterpreting it. State migration is not required unless explicitly requested.

**REQ-VST-02:** Map host automation to sample offsets. Provide latency-compensated bypass and optional wet/dry mixing without comb filtering caused by mismatched fixed delay. Do not report zero tail if filters still ring; use a declared practical settling threshold or framework-supported infinite tail when continuous noise is enabled. Silent-input optimizations must not skip pending mute/reset events, filter tails, or enabled noise.

**REQ-API-01:** Provide three integration modes over the same core, each with the real-time rules of this section:

- `DacCore`: DAC-rate PCM in, DAC response out at the core's output rate. For callers that run their own rate conversion.
- `HostProcessor`: an audio effect. Host-rate audio in, host-rate audio out, with input and output SRC (REQ-RATE-01).
- `DacStream`: a source at the DAC rate, such as an instrument or emulator that produces samples at its own clock. DAC-rate PCM in, host-rate audio out, with only output SRC. It supports pull mode (the caller asks for N host frames; the stream requests the DAC frames it needs through a callback) and push mode (the caller supplies DAC frames as they are produced; the stream returns the host frames that become available, with bounded buffering and reported underrun and overrun counts).

`DacStream` MUST accept exact fractional DAC rates given as a rational number (numerator and denominator), such as 112 000 000 / 2304 Hz, and keep a drift-free position (REQ-RATE-02). It accepts 16-bit, 20-bit, and 24-bit integer input and float input. 16-bit maps per REQ-PCM-02. 24-bit input keeps its top 20 bits and drops the rest, as the serial interface ignores bits after the 20th (I, from REQ-SERIAL-01 and [DS p.9]). Float follows REQ-PCM-03. Host rates follow REQ-RATE-03.

**REQ-API-02:** Provide a C API over the library for use from C and other languages: opaque handles, explicit create/prepare/process/destroy, plain status codes, caller-owned buffers, and no exceptions or C++ types crossing the boundary. It exposes `HostProcessor` and `DacStream` and their configuration, events, latency, and diagnostics. Document ownership and thread rules for every function.

**REQ-API-03:** Package the library for reuse: CMake install rules and a versioned `find_package(ak4522)` config, usable also through `add_subdirectory` or FetchContent; an integration guide covering the three modes, threading, latency, and events; and small framework-free examples for each mode and for the C API. Examples MUST NOT depend on a plugin framework.

**REQ-RT-01:** No heap allocation/deallocation, locks, file/network I/O, logging, sleeps, blocking calls, coefficient design, or unbounded retry loops on the audio thread. Preallocate for worst supported rate/block and handle unexpectedly larger blocks by bounded chunking. Avoid shared mutable static state and denormal slowdowns. Cross-thread changes use bounded communication and off-thread object reclamation.

**REQ-RT-02:** Perform prepare/configuration off-thread. `process` must not throw. Invalid inputs produce bounded documented behavior and counters readable off-thread. All states remain finite under supported full-scale and overload tests. Offline and realtime output are deterministic for the same configuration, seed, input, and events, within the declared floating-point tolerance.

**REQ-PLATFORM-01:** Support macOS (Apple Silicon and x86_64) and Windows (x64 and ARM64; not 32-bit x86) with AppleClang, Clang, and MSVC; Linux with GCC or Clang SHOULD be supported. Use only standard C++20 in the library: no compiler extensions such as `__int128`, and no platform APIs. A platform counts as supported only when the full test suite passes there. Results are deterministic per platform; across platforms they may differ within the documented floating-point tolerances (the golden comparison allows 1e-12), and seeded noise may differ in the last bit because platform math libraries differ.

**REQ-PERF-01:** Establish a named reference CPU, compiler, build settings, sample rate, profile, block size, and plugin count. Initial P budget: one clean stereo instance at 48 kHz host/44.1 kHz DAC uses <10% of a callback period at the 99.9th percentile for blocks >=64 on that reference machine. Record maximum duration and underruns separately. Benchmarks are not portable hardware guarantees. Do not sacrifice documented response silently to meet a CPU budget.

## 13. Verification and acceptance

Use CMake, CTest, and a conventional C++ test framework already present or chosen explicitly. Include offline analysis tools that emit machine-readable results and response plots. Fix all random seeds. Store small independently derived golden vectors; never generate the expected result with the same implementation under test.

### 13.1 Required test matrix

| Suite           | Required checks                                                                                                                                            |
| --------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PCM             | All boundary codes, sign extension, round ties, saturation, NaN/Inf, 16-to-20-bit scaling; exhaustive 16-bit mapping                                       |
| Serial          | All four modes; asymmetric L/R words; 32/40/64/128fs where valid; arbitrary padding; negative values; I2S boundary bit; malformed clock/word rejection     |
| Clock/reset     | Three CMODE ratios; rate boundaries; 150 ns pulse validator; clock loss; deterministic startup; no DAC 516-frame delay                                     |
| Mute            | Full 1024-frame transitions; reversal at 1/511/1023; repeated same-state events; endpoint exactness; block invariance; downstream settling; residual noise |
| De-emphasis     | Four pin combinations; DC gain; stable poles; response and phase; rate mismatch; switching policy                                                          |
| Filter          | High-rate impulse/frequency response; scaled boundaries; stopband images; passband ripple; delay; stereo independence; DC and Nyquist-near signals         |
| Analog          | Voltage mapping; weighting; S/N; DR; SINAD; gain mismatch and isolation; profile flags; zero-input/muted noise                                             |
| Rate conversion | Every host/DAC rate pair in supported matrix; long-run counts; swept tones near each Nyquist; spectral images/aliasing; no per-block reset                 |
| Streaming       | Single block versus randomized partitions, including zero/one-frame blocks; events at boundaries; in-place behavior; output capacity                       |
| Plugin          | Save/load and incompatible-state rejection; two simultaneous instances; host lifecycle; sample-accurate events; bypass; reported latency; float/double     |
| Robustness      | Silence, DC, impulses, alternating extremes, full-scale tones, overload, rate reprepare, long runs; sanitizer and allocation instrumentation               |

### 13.2 Measurement procedures and gates

1. **Digital response:** De-emphasis/noise/nonlinearity off. Normalize magnitude to 1 kHz. Dense-grid plus local-extremum search must show passband ripple <=0.06 dB to `20/44.1*fs`. Target -0.06 dB at that edge within a P numerical tolerance of 0.01 dB; target -6 dB at 0.5fs within 0.1 dB. These edge-fitting tolerances are not source tolerances. Verify >=43 dB stopband attenuation from `24.1/44.1*fs` throughout the declared relevant high-rate band. Report beyond-band assumptions explicitly.
2. **Combined response:** At 44.1 kHz, target +/-0.2 dB over 0-20 kHz relative to 1 kHz, with analog reconstruction enabled and imperfections off. Treat this as a nominal-model gate derived from a typical specification. At other DAC rates report the chosen analog scaling assumption separately.
3. **Latency:** Measure input-register-to-core-output reference delay and every adapter's delay independently. Fit passband phase rather than using only the largest impulse sample. Validate plugin alignment to within 0.01 host sample at its declared reference region; disclose dispersion elsewhere.
4. **Noise:** At 44.1 kHz under p.5 conditions, measure settled zero-code output for at least 10 seconds using a validated A-weighting filter and 10 Hz-20 kHz integration. Define S/N as full-scale sine RMS divided by weighted noise RMS. NominalAnalog target is 100 +/-1 dB A-weighted (P tolerance), with 95 dB as the documented minimum comparator. Report unweighted spectra too.
5. **Dynamic range:** Use a -60 dBFS 1 kHz sine, remove the fundamental with a documented notch/fit, measure weighted residual, and reference it to full-scale RMS. Record residual treatment and bandwidth. Target 100 +/-1 dB for 20-bit nominal mode; separately evaluate the documented 98 dB typical 16-bit result. Input quantization and dither policies must be reported.
6. **SINAD:** Use a coherent 1 kHz tone, isolate the fundamental, and integrate noise plus distortion within 10 Hz-20 kHz. The DAC table does not give the ADC's -0.5 dB stimulus qualifier for the DAC row. Declare the chosen nominal full-scale test level, and sweep -60, -20, -6, -1, and 0 dBFS to reveal sensitivity. Nominal target 90 +/-1 dB at the declared nominal condition; documented minimum comparator 80 dB. Do not confuse THD+N in negative dB with positive SINAD.
7. **Stereo:** Drive one channel while the other receives zero. Measure coherent leakage at 1 kHz with averaging, because a 100 dB noise floor can obscure 110 dB crosstalk. Evaluate both directions. Report gain mismatch separately from absolute gain and noise.
8. **Numerics:** With deterministic effects, randomized block partitions must agree within 1e-12 absolute for the double reference path and 1e-6 for float output, unless a stricter bitwise implementation is available. Different SIMD/compiler paths may use documented tolerances. Noise stream positions must match exactly for equal seeds and sample histories.
9. **Alias containment:** Test high-frequency tones, two-tone excitation, impulses, and nonlinear profiles. Separate intended DAC images from accidental foldback due to processing/SRC. Target unintended in-band aliases below -110 dBFS in clean mode, with the test stimulus and excluded intended responses declared.
10. **Real time:** Instrument audio-thread allocation and lock use, run stress playback, and publish benchmark results. Run address/undefined-behavior sanitizers off-line and thread checks around adapter state transfer where supported.

Do not force the clean profile to reproduce noise or distortion numbers that belong to NominalAnalog. Do not claim that merely outperforming a minimum limit reproduces the chip's character. A measured profile requires comparison against captures, not only table limits.

## 14. Hardware characterization plan

When physical hardware is available, record chip marking, board schematic, supplies, VREFH, temperature, clock source, load, external output circuit, analyzer bandwidth, calibration, and firmware/pin states. Measure both channels at 32/44.1/48 kHz and representative input levels. Capture impulse/step response, magnitude/phase, de-emphasis, mute full and partial transitions, reset transients, zero-code noise, harmonic spectra, crosstalk, and overload recovery.

Measure or de-embed the capture ADC and external analog chain. Do not attribute a sound card's antialias response, op-amp distortion, or AC-coupling pole to the AK4522. Keep raw captures immutable and checksum them. Fit against one set and validate against held-out captures and additional devices where possible. Record uncertainty and chip-to-chip variability. Update a model version when fitted sound changes.

## 15. Living feature documentation and README synchronization

**REQ-DOC-01:** Create and maintain `docs/features.md` as the working record of emulator capabilities. Create it with the first implementation milestone and update it in the same feature branch as each implementation or status change. This is separate from the requirements in this file: AGENTS.md describes intended behavior; the feature record describes what actually exists and is available.

**REQ-DOC-02:** Each feature entry MUST include:

- Feature name, purpose, and relevant requirement IDs.
- Current implementation status: planned, in progress, implemented, or removed.
- Validation status, checks performed, results, and known limitations.
- Approval status recorded separately from implementation and validation, with an approval reference when explicitly granted by the project owner. Do not infer approval from passing tests or an agent's own assessment.
- Availability: development branch only, merged into the main development branch, or included in an identified release/build. Include the relevant branch, commit, PR, or release reference when available.
- Supported profiles, formats, sample rates, controls, and defaults relevant to the feature.
- Usage instructions or a link to separate feature documentation, plus the date of the latest substantive update.

A feature may be implemented without being approved or available in a release. Keep these distinctions visible. Do not describe planned work as usable functionality. Record approval when it occurs without introducing a new permission gate for implementation already authorized by the user.

**REQ-DOC-03:** For substantial features, add focused usage and behavior documentation under `docs/features/` and link it from `docs/features.md`. Explain configuration, supported behavior, limitations, and relevant examples. Keep internal design details separate from user instructions when that improves clarity. Update or retire these documents when behavior changes or a feature is removed.

**REQ-DOC-04:** Keep `README.md` synchronized with the current state of the branch or release it accompanies. In every feature branch, review the README and update affected status statements, available capabilities, defaults, supported rates/formats/platforms, build and installation instructions, usage examples, limitations, and roadmap progress. Clearly separate available features from planned features. Link to `docs/features.md` once that file exists. Do not claim release availability based only on a development-branch implementation.

**REQ-DOC-05:** Documentation review is part of the completion and merge gates. Before marking a feature complete, verify that its implementation, tests, feature record, detailed documentation, and README agree. Check commands and examples against the actual build where available and verify local links. Include the documentation changes in the implementation PR. If the README requires no changes, state why in the completion report; do not make cosmetic edits merely to show activity.

When approval, merge, or release status changes after implementation, update the feature record and any affected README statements as part of that transition. Never fabricate approval references, version numbers, test results, or completion dates. This requirement does not authorize pushing or merging changes.

## 16. Milestones and definition of done

### M0: Evidence and build foundation

Deliver source manifest, evidence/uncertainty ledger, CMake library/test targets, API proposal, and a specification matrix. Gate: every numeric hardware constant has a page reference; project policies are labeled; ADC-only behavior is excluded.

### M1: PCM, serial, and control state

Deliver exact PCM mapping, four-mode serial decoder, timing validator, clock configuration, reset, and mute state machine. Gate: boundary/serial/control tests pass, including I2S short-word framing and partial mute reversal. No plugin GUI yet.

### M2: Linear DAC response

Deliver reproducible coefficient generation, interpolation/reconstruction, three de-emphasis modes, and core delay accounting. Gate: frequency, stopband-image, delay, stability, and streaming tests pass. If filter fit and delay cannot simultaneously meet targets, document the conflict and revise before claiming completion.

### M3: Host-rate adaptation and reusable library

Deliver SRC, all supported rate pairs, event mapping, latency alignment, and public examples. Gate: alias/count/block-invariance tests and real-time allocation checks pass. Publish core versus wrapper response separately.

### M4: Optional nominal analog profile

Deliver calibrated noise, optional distortion/mismatch/crosstalk/voltage diagnostics, and profile metadata. Gate: weighted measurement methods are validated; synthetic assumptions are explicit; typical-target reports pass within project tolerances. Hardware fidelity claims remain limited.

### M5: Integration API and packaging

Deliver, in this order: `DacStream` with pull and push modes, exact fractional rates, and 16/20/24-bit and float input (REQ-API-01); the opt-in extended rate range (REQ-CLOCK-02); the C API (REQ-API-02); packaging, the integration guide, and generic examples (REQ-API-03).

Gate: library tests for each new API, including block-partition invariance, drift-free long runs at a fractional DAC rate, push and pull equivalence, and real-time allocation checks; a C API test built as C; a consumer project, outside the library's own build, built against the installed package and run; release benchmark meeting the stated budget.

### Demo plugin (sibling project, after M5)

A separate repository, with its own README, feature record, and instructions, consumes a tagged library release through `find_package` or FetchContent and never through private headers or this repository's build tree. It is an adapter outside the library (section 0) and may use JUCE; its framework licence applies to it, not to this library. It provides a stereo effect built on `HostProcessor` and an instrument-style demo built on `DacStream`, with documented parameters and versioned state, automation, compensated bypass, float/double processing, a tail policy, and minimal generic controls. A delta listen mode (processed output minus the latency-aligned dry input) MAY be offered as host behavior. Formats are VST3 and, on macOS, AU. REQ-VST-01, REQ-VST-02, and REQ-LAT-03 apply to it and to any other plugin adapter. Gate: pluginval (VST3 and AU) and auval (AU) pass; smoke tests in representative hosts (FL Studio and Ableton Live) pass; latency agrees with emitted audio. Library gaps found there are fixed here, in this repository's milestones.

### M6: Hardware-characterized refinement, conditional

Deliver calibrated captures, fitted profile, residual-error plots, held-out validation, and updated uncertainty ledger. Gate: claims identify device/circuit and measurement limits. This milestone requires hardware data and is not a prerequisite for shipping a clearly labeled specification-based model.

### M7: ADC section and loopback

Deliver the ADC evidence entries, `AdcCore`, SDTO serial encoder, ADC reset and initialization behavior, and the optional DAC-to-ADC loopback chain, exposed through the integration modes and C API. Plugin use of the ADC belongs to the sibling plugin project. Gate: decimation response, high-pass response, initialization timing, serial output framing, and streaming tests pass under the same rules as the DAC; ADC and DAC results are reported separately. M7 depends on M3 and on M5; it does not depend on M6.

For each milestone, report changes, tests run, measured results, known deviations, and the next unmet gate. Completion means reproducible builds, passing applicable gates, documented assumptions, an up-to-date feature record and README, no hidden real-time violations, and an honest fidelity statement. Never represent an unmeasured approximation as the recovered AKM implementation.

## 17. ADC section (planned, M7)

The ADC converts analog input to 20-bit PCM on SDTO. It shares clocks, serial format pins, and PD with the DAC, but its signal path is independent [DS pp.1,12]. Everything in this section applies to `AdcCore` only. Verify every value below against [DS] text, not page images, when building the M7 evidence entries.

### 17.1 Documented ADC facts

| ID     | Documented behavior                                                                                      | Source        |
| ------ | -------------------------------------------------------------------------------------------------------- | ------------- |
| AD-01  | Stereo 20-bit delta-sigma ADC, 64x oversampling, 16 to 48 kHz                                            | p.1           |
| AD-02  | Differential inputs, internally biased to VA/2 with 30 kohm typical; single-ended use is possible        | p.15          |
| AD-03  | Decimation passband: -0.005 dB at 19.76 kHz, -0.02 dB at 20.02 kHz, -0.06 dB at 20.20 kHz, -6.0 dB at 22.05 kHz (max column) | p.6 |
| AD-04  | Decimation stopband from 24.34 kHz; ripple +/-0.005 dB max; attenuation 80 dB min                        | p.6           |
| AD-05  | Group delay 29.3 / fs typical; group delay distortion 0 us                                               | p.6, note 11  |
| AD-06  | Digital HPF: -3 dB at 0.9 Hz, -0.5 dB at 2.7 Hz, -0.1 dB at 6.0 Hz, at 44.1 kHz; scales with fs          | p.6; p.10     |
| AD-07  | Frequencies scale with fs; response reference is 1 kHz                                                   | p.6 note 10   |
| AD-08  | Samples at 64fs; the digital filter does not reject noise near multiples of 64fs                         | p.15          |
| AD-09  | Over-range input clips to 7FFFFh or 80000h; ideal code with no input is 00000h; DC offset removed by HPF | p.15          |
| AD-10  | After PD release, an analog initialization cycle runs; SDTO is valid after 516 LRCK cycles; output is "0" during reset and initialization; a small click occurs at the end of initialization | p.7 note 14; p.12 Fig.6 |
| AD-11  | SDTO is 20-bit MSB justified in modes 0 to 2 and I2S in mode 3; SDTO changes on SCLK falling edges; at 32fs in mode 3 the low bits are truncated | p.9 Table 2; pp.9-10 Figs.1-4 |
| AD-12  | SDTO timing: LRCK to SDTO MSB 40 ns max; SCLK falling to SDTO 50 ns max                                  | p.7           |

### 17.2 ADC analog characteristics

Conditions as section 4.3, with differential inputs and 470 ohm source impedance [DS p.5].

| ID     | ADC quantity                              | Minimum  | Typical         | Maximum  |
| ------ | ----------------------------------------- | -------: | --------------: | -------: |
| AD-A01 | S/(N+D), -0.5 dB input                    | 84 dB    | 92 dB           |          |
| AD-A02 | Dynamic range, -60 dB input, A-weighted   | 94 dB    | 100 dB          |          |
| AD-A03 | S/N, A-weighted                           | 94 dB    | 100 dB          |          |
| AD-A04 | Interchannel isolation                    | 90 dB    | 110 dB          |          |
| AD-A05 | Interchannel gain mismatch                |          | 0.1 dB          | 0.3 dB   |
| AD-A06 | Gain drift                                |          | 20 ppm/degree C |          |
| AD-A07 | Input voltage (AIN = 0.6 x VREFH)         | 2.85 Vpp | 3.0 Vpp         | 3.15 Vpp |
| AD-A08 | Input resistance                          | 20 kohm  | 30 kohm         |          |
| AD-A09 | Power-supply rejection                    |          | 50 dB           |          |

Footnotes [DS p.5]: single-ended input S/(N+D) is 80 dB typical (note 4); 16-bit ADC DR and S/N are 98 dB (note 5); CCIR-ARM S/N is 96 dB per converter and 94 dB for ADC-to-DAC loopback (note 6); full-scale input per AIN pin is 1.5 Vpp in differential mode (note 7). Single-ended range is nominally 0.6 x VREFH Vpp from one input and differential range 0.3 x VREFH Vpp per input [DS p.15].

### 17.3 Requirements

**REQ-ADC-01:** Provide `AdcCore` as a separate framework-independent core: normalized analog input at a declared input rate in, 20-bit stereo PCM at the ADC rate out. It follows the same real-time, streaming, determinism, and evidence rules as `DacCore`. It shares `CmodePin`, `SerialFormat`, and PD semantics but no signal state with the DAC.

**REQ-ADC-02:** Model the decimation response (AD-03 to AD-05) and the HPF (AD-06) as fitted, documented filters, under the same rules as REQ-FILTER-01: record design scripts, coefficients, and measured response, and do not claim recovered AKM coefficients. Preserve DC removal; the ADC path is not DC-coupled.

**REQ-ADC-03:** Model reset and initialization: SDTO outputs zero while PD is low and for 516 LRCK cycles after release (AD-10). The end-of-initialization click is undocumented in shape; do not synthesize it without captures.

**REQ-ADC-04:** Provide an SDTO serial encoder for all four modes, including I2S at 32fs with truncated low bits (AD-11), as the counterpart of `SerialDecoder`. Test it against Figures 1 to 4 and by round trip through `SerialDecoder` where the formats correspond.

**REQ-ADC-05:** Model input range and clipping (AD-09, AD-A07) with differential and single-ended input options. The single-ended option is a separately labeled configuration because its distortion differs (note 4).

**REQ-ADC-06:** Optional loopback: DAC output, an optional declared analog chain, then ADC input. Report the loopback separately from either converter; the 94 dB CCIR-ARM loopback S/N (note 6) is a NominalAnalog target, not a property of either converter alone.

**REQ-ADC-07:** Noise, distortion, mismatch, and crosstalk options follow the DAC rules in section 11, calibrated to the ADC values in 17.2.

