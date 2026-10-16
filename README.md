# AK4522VF DAC Emulator

An independent C++20 project to model the DAC section of the AKM AK4522VF for use in VST3 audio effects and other audio software.

The goal is to reproduce documented signal processing and control behavior, then refine the model through hardware measurements where available. The reusable DSP core will remain independent of plugin frameworks.

## Status

**Alpha, milestone M0 complete, M1 in progress.** The project has an engineering specification ([AGENTS.md](AGENTS.md)), a C++20 build and test foundation, a public API proposal, and evidence and assumptions ledgers. **No signal processing is implemented yet**: there is no usable DAC model, compiled plugin, or hardware validation result.

See [docs/features.md](docs/features.md) for the current status of each feature.

APIs, parameters, presets, serialized state, and internal formats may change without backward compatibility during alpha development.

## Building

Requirements: CMake 3.24 or later and a C++20 compiler. The test framework (Catch2) is downloaded at configure time. Only macOS with Apple clang has been tested so far.

```sh
cmake --preset dev                # Debug, warnings as errors
cmake --build --preset dev
ctest --preset dev                # all tests; use --preset unit for fast tests only
```

The `asan` preset adds address and undefined-behavior sanitizers; `release` builds optimized.

Offline coefficient design and analysis tools live in [tools/](tools/) and use [uv](https://docs.astral.sh/uv/) with a locked Python 3.12 environment. They are not needed to build the library.

## What this project models

The AK4522 is a stereo ADC/DAC device. This project focuses on its **20-bit DAC section**, including:

- Signed PCM conversion and stereo channel handling.
- Digital interpolation and analog reconstruction response.
- De-emphasis selections for 32, 44.1, and 48 kHz, plus OFF.
- Digital soft mute with a documented 1024-DAC-frame full transition.
- Active-low reset and clock configuration behavior.
- Four serial input formats, through a separate conformance adapter.
- Explicit conversion between the host sample rate and the emulated DAC rate.
- Optional noise, distortion, channel mismatch, and voltage-output diagnostics.

The emulated DAC operates from 16 to 48 kHz. Planned host-rate support includes 44.1, 48, 88.2, 96, 176.4, and 192 kHz through sample-rate conversion. A higher host rate does not change the hardware's documented operating range.

The ADC, its high-pass filter, and its startup initialization delay are outside the scope of the DAC model. External output amplifiers and coupling circuits are separate optional models.

## Fidelity and limitations

The datasheet describes observable behavior and performance limits, but does not disclose all interpolation coefficients, modulator internals, analog filter parameters, or nonlinear characteristics. Meeting its published limits does not establish bit-exact silicon reproduction.

The specification defines three model profiles:

| Profile       | Purpose                                                                            | Evidence required                                                                |
| ------------- | ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| Specification | Deterministic controls and a fitted linear response constrained by the datasheet   | Documented requirements, explicit approximation policies, and automated tests    |
| NominalAnalog | Optional synthetic noise and imperfections targeting typical published performance | Calibrated software measurements and clearly labeled modeling assumptions        |
| Measured      | Refinement against a particular physical device and circuit                        | Calibrated hardware captures, measurement conditions, and independent validation |

These profiles are planned capabilities, not completed features. The initial default is the Specification profile with de-emphasis, mute, noise, and analog imperfections off, a 44.1 kHz DAC rate, and 20-bit input.

An explicit delta-sigma simulation is optional research work. A validated equivalent signal-processing model is acceptable for the baseline. Unverified internal circuitry must not be presented as the recovered AKM implementation.

## Planned architecture

```text
Host audio
    |
    v
Input sample-rate conversion
    |
    v
DAC-rate PCM quantization
    |
    v
De-emphasis and digital soft mute
    |
    v
DAC interpolation and reconstruction model
    |
    v
Optional analog imperfections
    |
    v
Output sample-rate conversion and latency alignment
    |
    v
Host audio
```

Decoded PCM and serial input adapters will feed the same DAC core. Plugin integration will handle host automation, state, bypass, and latency reporting separately from chip behavior.

The core must support continuous processing across arbitrary block boundaries without allocations, locks, blocking operations, file I/O, or coefficient generation on the audio thread. Noise generation, when enabled, must be reproducible from a stored seed.

The intended toolchain is C++20 with CMake and CTest. The plugin adapter may use JUCE or the VST3 SDK; the framework choice is not yet established by this documentation.

## Development roadmap

| Milestone | Deliverable                                                                             |
| --------- | --------------------------------------------------------------------------------------- |
| M0        | Evidence ledger, build foundation, public API proposal, and requirement-to-test mapping |
| M1        | PCM mapping, serial decoder, clock validation, reset, and mute state machine            |
| M2        | Linear DAC response, de-emphasis, coefficient generation, and core latency accounting   |
| M3        | Host-rate adaptation, event timing, reusable library, and streaming verification        |
| M4        | Optional calibrated nominal analog profile                                              |
| M5        | VST3 adapter, automation, state handling, bypass, and host validation                   |
| M6        | Hardware-characterized refinement when suitable measurements are available              |

Detailed requirements and acceptance gates are maintained in [AGENTS.md](AGENTS.md).

Progress: M0 (evidence ledger, build foundation, API proposal, requirement-to-test mapping) is complete and approved (tag `m0-approved`). M1 is next.

## Verification

Validation will cover both signal behavior and software integration:

- PCM boundaries, serial framing, channel polarity, and clock timing.
- Reset behavior and exact mute transition counts, including interrupted transitions.
- De-emphasis response, filter stability, passband ripple, stopband images, and group delay.
- Weighted noise, dynamic range, SINAD, gain mismatch, and crosstalk for applicable profiles.
- Sample-rate conversion, alias containment, long-run frame counts, and block-size independence.
- Plugin latency, automation, bypass alignment, state handling, and multiple instances.
- Real-time allocation checks, numerical robustness, and measured CPU performance.

Published hardware values, typical performance targets, and project-defined tolerances must remain distinguishable. Test results must state the model profile, rates, stimulus, bandwidth, weighting, and measurement method. Hardware fidelity claims require hardware evidence.

## Working on the project

Read [AGENTS.md](AGENTS.md) before changing the implementation.

1. Create a dedicated feature branch before implementing a new feature or improvement.
2. Keep changes focused and update affected code, tests, and documentation together.
3. Label behavior as documented, inferred, a project policy, or measured.
4. Run the applicable build, tests, and DSP checks before merging. Report unresolved deviations rather than weakening criteria.
5. Push or merge only when explicitly requested by the project owner.

Backward compatibility is not required during alpha. Incompatible saved state should be rejected clearly rather than silently interpreted with different behavior. Compatibility layers and migration code are unnecessary unless explicitly requested.

Keep credentials, build output, temporary analysis artifacts, and local recordings out of version control.

## Reference material

The primary hardware reference is the **AK4522 datasheet, Asahi Kasei, M0020-E-01, October 1998**. Page-level citations, numerical requirements, source discrepancies, and the source-file checksum are recorded in [AGENTS.md](AGENTS.md).

Keep the PDF and any extracted text or page images local and excluded from version control. Place a local copy at `reference/AK4522.PDF`; the repository ignores everything in `reference/` except its [README](reference/README.md), which records the expected checksum. The original project documentation, source citations, and engineering decisions belong in version control.

Engineering records:

- [docs/evidence.md](docs/evidence.md): source-to-requirement and requirement-to-test traceability, and datasheet discrepancies.
- [docs/assumptions.md](docs/assumptions.md): project policies and open uncertainties.
- [docs/validation.md](docs/validation.md): measurement methods, results, and known deviations.
- [docs/api.md](docs/api.md): public API design and decisions.
- [docs/features.md](docs/features.md): implementation, validation, approval, and availability status of each feature.

## License and attribution

This project is licensed under the GNU General Public License, version 3 or (at your option) any later version (SPDX: `GPL-3.0-or-later`). See [LICENSE](LICENSE). Review the licensing requirements of any plugin SDK or framework before distributing binaries built with it.

The AK4522 datasheet is the manufacturer's copyrighted document and is not covered by this license or distributed with the source.

AKM and AK4522VF identify the hardware being modeled. This is an independent project; it does not claim endorsement by or affiliation with the manufacturer.
