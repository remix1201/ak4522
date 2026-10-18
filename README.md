# AK4522VF Emulator

An independent C++20 library that models the AKM AK4522VF stereo audio codec, its 20-bit DAC and ADC, for use in audio effects, instruments, and emulators of hardware that used the chip, offline tools, and other audio software. It has a C API and no dependencies, and does not use any plugin framework.

The model reproduces the behavior and limits the datasheet documents, and labels everything else it has to assume. It is a specification-based model, not a recovered copy of AKM's silicon.

## Status

**Alpha.** Milestones M0 to M5 and M7 are complete and approved; see the [roadmap](#development-roadmap).

| You want to                                                                                          | Use                                             | Example                                                                                                                        |
| ---------------------------------------------------------------------------------------------------- | ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| Run host audio through the emulated DAC, as an effect                                                | `HostProcessor`                                 | [host_processor_example.cpp](examples/host_processor_example.cpp)                                                              |
| Play a synth or emulated system that runs at its own clock, such as 112 MHz / 2304, at any host rate | `DacStream`, pull or push                       | [dac_stream_example.cpp](examples/dac_stream_example.cpp), [dac_stream_push_example.cpp](examples/dac_stream_push_example.cpp) |
| Use the chip model alone at its own rate                                                             | `DacCore`                                       | [dac_core_example.cpp](examples/dac_core_example.cpp)                                                                          |
| Run host audio through the DAC and back through the ADC, or through the ADC alone                    | `HostProcessor` with `path = loopback` or `adc` | [host_adc_example.cpp](examples/host_adc_example.cpp)                                                                          |
| Digitize an analog-like signal at the ADC rate                                                       | `AdcCore`                                       | [adc_core_example.cpp](examples/adc_core_example.cpp)                                                                          |
| Use any of it from C or another language                                                             | `ak4522/ak4522.h`                               | [c_api_example.c](examples/c_api_example.c)                                                                                    |

What is available:

- **DAC:** the datasheet's filter response and 14.7-frame delay, de-emphasis (32, 44.1, 48 kHz), the 1024-frame soft mute, power-down and reset, and 16-bit and 20-bit input.
- **ADC:** decimation filter, 0.9 Hz high-pass filter, 29.3-frame delay (29.3125 modeled), the 516-frame initialization after reset, saturation at full scale, and differential or single-ended input.
- **Rates:** DAC and ADC rates from 16 to 48 kHz, or up to 50.4 kHz with an opt-in extended range; host rates 44.1, 48, 88.2, 96, 176.4, and 192 kHz; exact fractional source rates without drift; exactly reported latency.
- **Profiles:** the clean, deterministic Specification profile (the default), and an optional NominalAnalog profile that adds synthetic noise, distortion, channel mismatch, crosstalk, and (DAC only) a DC offset, calibrated to the datasheet's typical figures for each converter ([analog_profile.md](docs/analog_profile.md)).
- **Serial interfaces:** an SDTI decoder and an SDTO encoder for all four modes, and a pin-timing validator, for conformance testing and device-style use.
- **Extras:** the datasheet's example output circuit (Fig.11), off by default; a CPU benchmark; CMake and pkg-config packaging.

Not available: a profile measured from real hardware (M6, deferred), and the demo plugin, which is planned as a separate project. See [docs/features.md](docs/features.md) for the status, validation, and limits of every feature.

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

### Using it in another project

`cmake --install build/release --prefix <prefix>` installs the library and a CMake package; then `find_package(ak4522)` and link `ak4522::ak4522`. `add_subdirectory` and FetchContent work too, and `BUILD_SHARED_LIBS=ON` builds a shared library. See the [integration guide](docs/integration.md).

Continuous integration (`.github/workflows/ci.yml`) defines the same steps for macOS, Windows, and Linux. The test suite has also been run by hand on all three; see [docs/validation.md](docs/validation.md).

## Using the library

Start with the [integration guide](docs/integration.md); the full reference is [docs/api.md](docs/api.md). An effect:

```cpp
ak4522::HostConfig config;                // host and DAC rates, block size
ak4522::HostProcessor host;
host.prepare(config);                     // off the audio thread
host.process(input, output, events);      // float or double, N frames in, N out
host.latency().reportedHostFrames;        // report to the host for compensation
```

A source at its own clock, under any host rate:

```cpp
ak4522::DacStreamConfig config;
config.hostRate = 96000.0;                // whatever the DAW runs at
config.dacRate = {112000000, 2304};       // the source's exact rate in Hz
config.dac.allowExtendedRate = true;      // 48.6 kHz is above the datasheet's 48 kHz
config.format = ak4522::DacSampleFormat::int16;
config.pull = &mySource;                  // void(void*, DacPullBlock&) noexcept
ak4522::DacStream stream;
stream.prepare(config);                   // off the audio thread
stream.render(output);                    // N host frames out
```

From C or another language, include `ak4522/ak4522.h`: handles, plain structs, and status codes over every entry point ([docs/api.md](docs/api.md#c-api)).

Measured responses are in [docs/responses.md](docs/responses.md). Offline coefficient design and analysis tools live in [tools/](tools/) and use [uv](https://docs.astral.sh/uv/) with a locked Python 3.12 environment; they are not needed to build the library.

## Fidelity and limitations

The datasheet describes observable behavior and performance limits, but does not disclose all interpolation coefficients, modulator internals, analog filter parameters, or nonlinear characteristics. Meeting its published limits does not establish bit-exact silicon reproduction.

Model profiles:

| Profile       | Purpose                                                                            | Evidence required                                                                |
| ------------- | ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| Specification | Deterministic controls and a fitted linear response constrained by the datasheet   | Documented requirements, explicit approximation policies, and automated tests    |
| NominalAnalog | Optional synthetic noise and imperfections targeting typical published performance | Calibrated software measurements and clearly labeled modeling assumptions        |
| Measured      | Refinement against a particular physical device and circuit                        | Calibrated hardware captures, measurement conditions, and independent validation |

Specification and NominalAnalog are implemented; Measured is not (M6 is deferred) and is rejected if requested. The default is the Specification profile with de-emphasis, mute, noise, and analog imperfections off, a 44.1 kHz rate, and 20-bit input.

NominalAnalog is synthetic: it meets the datasheet's typical numbers (S/N, dynamic range, S/(N+D), isolation, mismatch), but the character of its noise and distortion is assumed, not measured. One disclosed deviation: with flat noise, the CCIR-ARM-weighted S/N is 97.3 dB against the datasheet's 96 dB typical, for each converter (DISC-08); the DAC-to-ADC loopback measures 94.2 dB against 94 dB typical. Every assumption, deviation, and uncertainty is recorded in [docs/assumptions.md](docs/assumptions.md) and [docs/evidence.md](docs/evidence.md).

An explicit delta-sigma simulation is optional research work. A validated equivalent signal-processing model is acceptable for the baseline. Unverified internal circuitry must not be presented as the recovered AKM implementation.

## Architecture

```text
host audio                              a source at its own clock
    |                                             |
HostProcessor                                 DacStream
input SRC, 20-bit quantization                pull, or a push queue
    |                                             |
    +----------------------+----------------------+
                           v
DacCore: de-emphasis, soft mute, 8x interpolation, reconstruction,
         optional NominalAnalog imperfections
                           |
                           +---------------------------+
                           |                           v
                           |      HostProcessor with path = loopback only:
                           |      AdcCore: decimation, high-pass, 20-bit codes
                           |                           |
                           +-------------+-------------+
                                         v
           output SRC and latency alignment, optional Fig.11 circuit
                                         |
                                     host audio
```

`HostProcessor` can also feed host audio straight to `AdcCore` (`path = adc`). `DacCore` and `AdcCore` can be used alone at the chip's rate, and `SerialDecoder` and `SerialEncoder` convert between frames and the chip's serial pins. ADC behavior never enters the DAC model.

Processing continues across arbitrary block sizes without allocations, locks, blocking, file I/O, or coefficient design on the audio thread. Noise is reproducible from a stored seed. Plugin adapters, which handle host automation, state, and bypass, live in separate projects.

## Development roadmap

| Milestone | Deliverable                                                                             |
| --------- | --------------------------------------------------------------------------------------- |
| M0        | Evidence ledger, build foundation, public API proposal, and requirement-to-test mapping |
| M1        | PCM mapping, serial decoder, clock validation, reset, and mute state machine            |
| M2        | Linear DAC response, de-emphasis, coefficient generation, and core latency accounting   |
| M3        | Host-rate adaptation, event timing, reusable library, and streaming verification        |
| M4        | Optional calibrated nominal analog profile                                              |
| M5        | `DacStream` for sources at their own rate, C API, packaging, and integration guide      |
| M6        | Hardware-characterized refinement when suitable measurements are available (deferred)   |
| M7        | ADC section, SDTO serial output, and optional DAC-to-ADC loopback                       |
| Next      | Demo plugin (VST3, AU) as a separate project using the packaged library                 |

Detailed requirements and acceptance gates are maintained in [AGENTS.md](AGENTS.md).

Progress: M0 (evidence ledger, build foundation, API proposal, requirement-to-test mapping) is complete and approved (tag `m0-approved`). M1 (PCM mapping, serial decoder, timing validator, clock configuration, reset, and mute state machine) is complete and approved (tag `m1-approved`). M2 (linear DAC response) is complete and approved (tag `m2-approved`). M3 (host-rate adaptation) is complete and approved (tag `m3-approved`). M4 (NominalAnalog profile) is complete and approved (tag `m4-approved`). M5 (integration API and packaging) is complete and approved (tag `m5-approved`). M7 (the ADC section, its serial output, and the DAC-to-ADC loopback) is complete and approved (tag `m7-approved`). M6 (hardware-characterized refinement) is deferred for lack of suitable hardware access; the model remains specification-based, with a synthetic NominalAnalog profile. The demo plugin follows M5 as a separate project.

## Verification

More than 300 automated tests run through CTest on macOS, Windows (ARM64 and x64), and Linux, plus a slower host-rate matrix and measurement gates that run the Python analysis tools on captured output. They cover:

- PCM boundaries and quantization, serial framing in all four modes, pin timing, clock validation, and reset.
- Mute transitions including interrupted ones, de-emphasis response, filter response, stopband images, and group delay, against gates taken from the datasheet's filter tables.
- A golden-output lock on the clean signal path.
- Weighted noise, dynamic range, S/(N+D), mismatch, and crosstalk for the NominalAnalog profile of each converter.
- Sample-rate conversion at every supported host rate against DAC rates from 16 to 48 kHz: alias containment, exact long-run frame counts, timing within 0.01 frame of the reported latency, and bitwise block-size independence.
- Real-time safety (an allocation counter around the audio-thread calls of every entry point), a sanitizer preset (address and undefined behavior), ThreadSanitizer on the push queue, and a C API test compiled as C.
- A consumer project built against the installed package, through `add_subdirectory`, as a shared library, and from pkg-config.

The tests themselves were checked by mutation: deliberate faults were inserted into the code, and wherever a fault went unnoticed a test was added. Datasheet values, typical targets, and project tolerances are kept distinct, and every result in [docs/validation.md](docs/validation.md) states its profile, rates, stimulus, bandwidth, and method. One clean stereo instance uses about 2% of a core on the reference machine (Apple M1 Max).

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

The primary hardware reference is the **AK4522 datasheet, Asahi Kasei, M0020-E-01, October 1998**. It is not distributed with this project. Page-level citations, numerical requirements, source discrepancies, and the checksum of the copy used are recorded in [AGENTS.md](AGENTS.md) and [docs/evidence.md](docs/evidence.md).

To work with the datasheet locally, place a copy at `reference/AK4522.PDF`; the repository ignores everything in `reference/` except its [README](reference/README.md), which records the expected checksum. The original project documentation, source citations, and engineering decisions belong in version control.

Engineering records:

- [docs/evidence.md](docs/evidence.md): source-to-requirement and requirement-to-test traceability, and datasheet discrepancies.
- [docs/assumptions.md](docs/assumptions.md): project policies and open uncertainties.
- [docs/validation.md](docs/validation.md): measurement methods, results, and known deviations.
- [docs/api.md](docs/api.md): public API design and decisions.
- [docs/features.md](docs/features.md): implementation, validation, approval, and availability status of each feature.

## License and attribution

Copyright (C) 2026 Ross Baker.

This project is licensed under the GNU General Public License, version 3 or (at your option) any later version (SPDX: `GPL-3.0-or-later`). See [LICENSE](LICENSE). Contributions are welcome under the same terms; see [CONTRIBUTING.md](CONTRIBUTING.md). Review the licensing requirements of any plugin SDK or framework before distributing binaries built with it.

The AK4522 datasheet is the manufacturer's copyrighted document and is not covered by this license or distributed with the source.

AKM and AK4522VF identify the hardware being modeled. This is an independent project; it does not claim endorsement by or affiliation with the manufacturer.
