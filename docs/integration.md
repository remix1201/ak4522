# Integration guide

How to add the AK4522 DAC emulation library to your project and use it (REQ-API-03). The reference for every type is [api.md](api.md); the behavior and its limits are in [features.md](features.md).

The library is plain C++20 with a C API. It has no dependencies and does not use any plugin framework, so it fits an audio plugin, an instrument or emulator, an offline tool, or a program in another language.

## Getting the library into your build

**Installed package.** Build and install once:

```sh
cmake -S ak4522 -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /path/to/prefix
```

Then in your project:

```cmake
find_package(ak4522 0.0 REQUIRED CONFIG)   # with CMAKE_PREFIX_PATH=/path/to/prefix
target_link_libraries(my_app PRIVATE ak4522::ak4522)
```

**Inside your build.** With a checkout next to your sources, or fetched by CMake:

```cmake
add_subdirectory(third_party/ak4522)       # or:

include(FetchContent)
FetchContent_Declare(ak4522 GIT_REPOSITORY <repository> GIT_TAG <tag or commit>)
FetchContent_MakeAvailable(ak4522)

target_link_libraries(my_app PRIVATE ak4522::ak4522)
```

As a subproject the library builds no tests or examples and installs nothing unless you set `AK4522_INSTALL=ON`.

**Static or shared.** The library is static by default. Set `BUILD_SHARED_LIBS=ON` for a shared library, for example to load the C API from Python, Rust, or another language. On Windows, keep the DLL next to your executable.

**Versions.** During alpha only the same minor version is compatible (`find_package(ak4522 0.0)`), and any release may change the API.

`tests/package/consumer` is a complete outside project, built by the test suite in all three ways.

## Choosing an entry point

| You have | Use | In | Out |
| -------- | --- | -- | --- |
| Host audio to process, as an effect | `HostProcessor` | host-rate float or double | host-rate audio, fixed latency |
| A source that runs at its own rate: an instrument, an emulated system, a decoder | `DacStream` | DAC frames at the source's exact rate | host-rate audio |
| Your own rate conversion, or analysis at the DAC rate | `DacCore` | 20-bit frames at the DAC rate | 8x the DAC rate |
| A C program or another language | `ak4522/ak4522.h` | as above | as above |

Each has an example: [examples/host_processor_example.cpp](../examples/host_processor_example.cpp), [examples/dac_stream_example.cpp](../examples/dac_stream_example.cpp) (pull), [examples/dac_stream_push_example.cpp](../examples/dac_stream_push_example.cpp) (push), [examples/dac_core_example.cpp](../examples/dac_core_example.cpp), and [examples/c_api_example.c](../examples/c_api_example.c).

## Lifecycle and threads

1. Construct and `prepare(config)` off the audio thread. `prepare` validates everything and allocates every buffer; check its `Status`.
2. Call `process` (`HostProcessor`, `DacCore`) or `render` (`DacStream`) on the audio thread. They never allocate, lock, block, or throw.
3. `reset()` returns to the state after `prepare` without allocating. It is a host lifecycle reset, not the chip's PD pin.
4. To change rates or configuration, stop processing and call `prepare` again.

`prepare`, `reset`, and destruction must not run while processing. `diagnostics()` may be called from any thread.

## Rates

- **Host rates:** 44.1, 48, 88.2, 96, 176.4, and 192 kHz.
- **DAC rates:** 16 to 48 kHz, the datasheet range. `DacConfig::allowExtendedRate` accepts up to 50.4 kHz; that is not documented chip behavior and `latency().outsideDocumentedRate` reports it.
- **Exact rates:** `DacStream` takes the source's rate as a fraction, `ExactRate{numerator, denominator}`, so a clock division such as 112 MHz / 2304 is exact and never drifts.

## Feeding a DacStream

**Pull** suits a source you can run on demand, such as an emulator stepped from the audio callback. Set `config.pull` to a function and `config.pullContext` to your object. Each `render(n)` asks for exactly the DAC frames that n host frames need; after T host frames, ceil(T fs / H) in total. Write them in the configured format, and add pin events if the source changes DEM, SMUTE, or PD:

```cpp
static void pull(void* context, ak4522::DacPullBlock& block) noexcept
{
    auto& source = *static_cast<MySource*>(context);
    for (std::size_t j = 0; j < block.frames; ++j) {
        block.integers.left[j] = source.nextLeft();   // int16 format: 16-bit values
        block.integers.right[j] = source.nextRight();
    }
    // block.events[block.eventCount++] = ak4522::ControlEvent::softMute(offset, true);
}
```

**Push** suits a source on its own thread. Call `push(frames, events)` from that thread and `render` from the audio thread. The queue holds `pushCapacityFrames`. Prime it before rendering, and keep it ahead using `queuedFrames()`, `freeFrames()`, and `dacFramesFor(n)`. If it runs dry, the missing frames play as silence and `diagnostics().sourceUnderrunFrames` counts them. If it is full, the extra frames are dropped and `sourceOverrunFrames` counts them.

**Formats.** `int16` (scaled to 20-bit weight), `int20`, `int24` (the top 20 bits are used, as the chip's serial input does), `float32`, and `float64`. Float is quantized to 20 bits with rounding to nearest, ties to even.

## Events

`ControlEvent` changes a chip pin: `powerDown` (PD), `deemphasis` (DEM1:DEM0), or `softMute` (SMUTE). Offsets are relative to the block, in host frames for `HostProcessor` and in DAC frames for `DacStream` and `DacCore`. They must be below the block length and must not decrease. Events on the same frame apply in the order PD, DEM, SMUTE.

The emulation contains only the chip. Host conveniences, such as choosing de-emphasis automatically from the rate or fading on start and stop, belong in your code, acting through these events.

## Latency

`latency().reportedHostFrames` is the whole-frame delay to report to a host for compensation. Output is aligned to it exactly, so a dry signal delayed by that many frames lines up with the processed one. For `DacStream`, DAC frame j appears at host frame j H / fs plus the reported latency. `LatencyInfo` also breaks the latency down by component.

## Profiles and options

`DacConfig::profile` selects `specification` (the default: the datasheet's filter response, deterministic, no noise) or `nominalAnalog`, which adds optional synthetic noise, distortion, channel mismatch, crosstalk, and DC offset at the datasheet's typical values, each with its own flag in `DacConfig::analog`. Noise is seeded per instance (`analog.noiseSeed`), so output is reproducible. See [analog_profile.md](analog_profile.md).

## Output

Output is normalized: full scale is 1.0 and the chip's VCOM is 0.0. `outputVolts(x)` converts a sample to the nominal analog voltage for diagnostics. Nothing clips the output: reconstruction overshoot is preserved, as on the chip.

## From C and other languages

Include `ak4522/ak4522.h`. Handles replace the classes, `*_default` functions fill configuration structs, and every call returns a status code:

```c
ak4522_host_config config;
ak4522_host_config_default(&config);
ak4522_host* host = ak4522_host_create();
if (ak4522_host_prepare(host, &config) != AK4522_OK) { /* ... */ }
ak4522_host_process_f32(host, in_l, in_r, out_l, out_r, frames, NULL, 0);
ak4522_host_destroy(host);
```

Link with a C++ toolchain, or use the shared library. Details are in [api.md](api.md#c-api).

## Licence

The library is GPL-3.0-or-later. Software that includes it and is distributed must be offered under compatible terms; check the licence of any plugin framework you combine it with.
