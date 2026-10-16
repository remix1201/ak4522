#pragma once

#include "ak4522/buffers.hpp"
#include "ak4522/config.hpp"
#include "ak4522/diagnostics.hpp"
#include "ak4522/events.hpp"
#include "ak4522/latency.hpp"
#include "ak4522/status.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace ak4522 {

// Word length applied when host audio is quantized for the DAC.
enum class InputResolution : std::uint8_t {
    bits20, // quantize to 20 bits (default, P-01)
    bits16, // quantize to 16 bits, then scale to 20-bit weight (REQ-PCM-02)
};

struct HostConfig {
    double hostRate = 48000.0;   // Hz; 44.1, 48, 88.2, 96, 176.4, or 192 kHz (P-32)
    std::size_t maxBlockFrames = 512;
    InputResolution inputResolution = InputResolution::bits20;
    bool inputDither = false;    // TPDF before quantization; host feature (P-12)
    std::size_t maxPendingEvents = 256; // events deferred past the block by SRC latency
    DacConfig dac{};
};

// Host-rate wrapper for plugins: input SRC, quantization, DacCore, output SRC,
// and latency alignment (REQ-RATE-01). Produces exactly as many output frames
// as it consumes; its fixed latency is reported by latency().
//
// Threading rules match DacCore.
class HostProcessor {
public:
    HostProcessor();
    ~HostProcessor();
    HostProcessor(HostProcessor&&) noexcept;
    HostProcessor& operator=(HostProcessor&&) noexcept;
    HostProcessor(const HostProcessor&) = delete;
    HostProcessor& operator=(const HostProcessor&) = delete;

    [[nodiscard]] Status prepare(const HostConfig& config);

    // Host lifecycle reset: clears SRC and chip state deterministically. It is
    // not a PD transition and does not re-seed noise (P-74). Use a powerDown
    // event to emulate the PD pin.
    void reset() noexcept;

    // Events are in host frames relative to the block and are mapped to the
    // first DAC frame at or after their input-stream time (P-41). Input and
    // output may alias exactly.
    [[nodiscard]] Status process(StereoView<const float> input,
                                 StereoView<float> output,
                                 std::span<const ControlEvent> events) noexcept;
    [[nodiscard]] Status process(StereoView<const double> input,
                                 StereoView<double> output,
                                 std::span<const ControlEvent> events) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] LatencyInfo latency() const noexcept;
    [[nodiscard]] Diagnostics diagnostics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ak4522
