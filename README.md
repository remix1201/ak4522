# AK4522VF DAC Emulator

An independent C++20 project to model the DAC section of the AKM AK4522VF for use in VST3 audio effects and other audio software.

The goal is to reproduce documented signal processing and control behavior, then refine the model through hardware measurements where available. The reusable DSP core will remain independent of plugin frameworks.

## Status

**Alpha, milestones M0 to M4 complete.** The DAC core produces audio through the public `DacCore` API: 20-bit PCM frames and DEM, SMUTE, and PD events in, the reconstructed signal out at 8x the DAC rate, with the datasheet's filter response and 14.7-frame delay. `HostProcessor` wraps it for plugin use: host audio at 44.1 to 192 kHz in and out, with exactly reported latency. An optional NominalAnalog profile adds synthetic noise, distortion, channel mismatch, crosstalk, and DC offset calibrated to the datasheet's typical values ([docs/analog_profile.md](docs/analog_profile.md)); the clean Specification profile remains the default. The plugin and hardware validation do not exist yet.

See [docs/features.md](docs/features.md) for the current status of each feature.

APIs, parameters, presets, serialized state, and internal formats may change without backward compatibility during alpha development.

## Building

Requirements: CMake 3.24 or later and a C++20 compiler (AppleClang, Clang, MSVC, or GCC). The test framework (Catch2) is downloaded at configure time. The full test suite passes on macOS (Apple Silicon), Windows ARM64 and x64 (MSVC), and Linux (GCC, aarch64); see [docs/validation.md](docs/validation.md) (REQ-PLATFORM-01).

```sh
cmake --preset dev                # Debug, warnings as errors
cmake --build --preset dev
ctest --preset dev                # all tests; use --preset unit for fast tests only
```

The `asan` preset adds address and undefined-behavior sanitizers; `release` builds optimized. The host-rate matrix tests are a separate executable, best run in a release build:

```sh
cmake --preset release && cmake --build --preset release
ctest --test-dir build/release -L matrix
```

### Windows

Install Visual Studio 2022 or later, or just its Build Tools (Build Tools 2026 works), with the "Desktop development with C++" workload, which includes CMake; on Windows on ARM also add the "MSVC ARM64 build tools" component. Then install Git and Python 3.9 or later. CMake picks the newest installed Visual Studio automatically. [uv](https://docs.astral.sh/uv/) is optional and enables the analysis-tool and response tests. In a Developer PowerShell (or any PowerShell; CMake finds Visual Studio itself):

```powershell
git clone <repository> ak4522
cd ak4522
cmake --preset dev
cmake --build --preset dev
ctest --preset dev -LE matrix
```

The default presets build for the machine's own architecture. On Windows, `dev-x64` and `release-x64` build for x64 explicitly; on Windows on ARM they cross-compile and the tests run under Windows' x64 emulation:

```powershell
cmake --preset dev-x64
cmake --build --preset dev-x64
ctest --preset dev-x64 -LE matrix
```

32-bit Windows (x86) is not supported (REQ-PLATFORM-01).

In a virtual machine, clone into a local folder rather than building from a shared host folder; shared folders are slow and can report file times that confuse incremental builds.

Continuous integration (`.github/workflows/ci.yml`) defines the same steps for macOS, Windows, and Linux; it has not run yet because the repository is not on GitHub.

## Using the library

`HostProcessor` is the plugin-facing entry point. A complete program is in [examples/host_processor_example.cpp](examples/host_processor_example.cpp):

```cpp
ak4522::HostConfig config;                // host and DAC rates, block size
ak4522::HostProcessor host;
host.prepare(config);                     // off the audio thread
host.process(input, output, events);      // float or double, N frames in, N out
host.latency().reportedHostFrames;        // report to the host for compensation
```

Measured responses are in [docs/responses.md](docs/responses.md).

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

The ADC section is planned as a separate core in milestone M7, including its decimation filter, high-pass filter, initialization delay, serial output, and an optional DAC-to-ADC loopback. ADC behavior never enters the DAC model. External output amplifiers and coupling circuits are separate optional models.

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
| M7        | ADC section, SDTO serial output, and optional DAC-to-ADC loopback                       |

Detailed requirements and acceptance gates are maintained in [AGENTS.md](AGENTS.md).

Progress: M0 (evidence ledger, build foundation, API proposal, requirement-to-test mapping) is complete and approved (tag `m0-approved`). M1 (PCM mapping, serial decoder, timing validator, clock configuration, reset, and mute state machine) is complete and approved (tag `m1-approved`). M2 (linear DAC response) is complete and approved (tag `m2-approved`). M3 (host-rate adaptation) is complete and approved (tag `m3-approved`). M4 (NominalAnalog profile) is complete and approved (tag `m4-approved`). M5 (VST3 plugin) is next.

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
