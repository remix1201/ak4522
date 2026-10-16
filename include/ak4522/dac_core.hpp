#pragma once

#include "ak4522/buffers.hpp"
#include "ak4522/config.hpp"
#include "ak4522/diagnostics.hpp"
#include "ak4522/events.hpp"
#include "ak4522/latency.hpp"
#include "ak4522/pcm.hpp"
#include "ak4522/status.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace ak4522 {

struct ProcessResult {
    Status status = Status::ok;
    std::size_t framesWritten = 0;
};

// The chip model at the DAC rate. Input is one Pcm20Frame per LRCK cycle;
// output is the reconstructed analog-equivalent signal at outputRate(),
// including images above the DAC Nyquist frequency, normalized so that full
// scale is 1.0 and VCOM is 0.0. Band-limiting to the host happens only in
// HostProcessor's output SRC.
//
// Threading: prepare() and the destructor run off the audio thread and never
// concurrently with process() or reset(). latency() is stable between
// prepares. diagnostics() may be called from any thread.
class DacCore {
public:
    // Output samples per input frame (D-02).
    static constexpr std::size_t oversampling = 8;

    DacCore();
    ~DacCore();
    DacCore(DacCore&&) noexcept;
    DacCore& operator=(DacCore&&) noexcept;
    DacCore(const DacCore&) = delete;
    DacCore& operator=(const DacCore&) = delete;

    // Validates the configuration, designs or loads coefficients, and
    // allocates every buffer. Ends in the reset state (P-70). On failure the
    // core is unprepared.
    [[nodiscard]] Status prepare(const DacConfig& config, std::size_t maxBlockFrames);

    // Deterministic reset, equal to the state after prepare (P-71, P-72).
    void reset() noexcept;

    // Processes input.size() DAC frames and writes input.size() * oversampling
    // output frames. Events are in DAC frames relative to the block. Blocks
    // larger than maxBlockFrames are processed in bounded chunks. Any frame
    // count, including zero, is valid.
    [[nodiscard]] ProcessResult process(std::span<const Pcm20Frame> input,
                                        std::span<const ControlEvent> events,
                                        StereoView<double> output) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double outputRate() const noexcept; // dacRate * oversampling
    [[nodiscard]] LatencyInfo latency() const noexcept;
    [[nodiscard]] Diagnostics diagnostics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ak4522
