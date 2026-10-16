#pragma once

// Output-side latency alignment and buffering shared by HostProcessor and
// DacStream (P-39). Internal: not part of the public API.

#include "core/wide_math.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ak4522::detail {

// The core's nominal delay in DAC frames, 14.7 = 147 / 10 (D-F06).
inline constexpr double coreDelayFrames = 14.7;
inline constexpr std::uint64_t coreDelayNum = 147;
inline constexpr std::uint64_t coreDelayDen = 10;

// Alignment for R = hostPerDacNum / hostPerDacDen host frames per DAC frame
// (P-39): the core delay is 14.7 R host frames; K is its whole part, and the
// output converter starts phi = 8 (14.7 - K / R) samples late at 8x.
struct OutputAlignment {
    std::int64_t latencyWhole = 0; // K
    double outputOffset = 0.0;     // phi, in 8x samples
};

// The core delay is delayNum / delayDen frames: 14.7 for the DAC path by
// default; the ADC paths pass their own (P-111). oversampling is the output
// converter's input samples per frame: 8 after DacCore, 1 after AdcCore.
[[nodiscard]] inline OutputAlignment alignOutput(std::uint64_t hostPerDacNum, std::uint64_t hostPerDacDen,
                                                 std::size_t oversampling, std::uint64_t delayNum = coreDelayNum,
                                                 std::uint64_t delayDen = coreDelayDen) noexcept
{
    OutputAlignment a;
    a.latencyWhole = static_cast<std::int64_t>(mulDivFloor(delayNum, hostPerDacNum, delayDen * hostPerDacDen));
    const double hostPerDac = static_cast<double>(hostPerDacNum) / static_cast<double>(hostPerDacDen);
    const double delay = static_cast<double>(delayNum) / static_cast<double>(delayDen);
    a.outputOffset = static_cast<double>(oversampling)
        * (delay - static_cast<double>(a.latencyWhole) / hostPerDac);
    if (a.outputOffset < 0.0) {
        a.outputOffset = 0.0; // rounding guard; at most a few ulp
    }
    return a;
}

// Prefill D: enough queued output that every host block can be served.
// Output m needs 8x input floor(m R2 + phi) + W2, which needs DAC frame
// ceil of that over 8, which needs host input floor(j R1) + W1 (P-39).
// w1 is zero when there is no input converter.
[[nodiscard]] inline std::size_t outputPrefill(double w1, double w2, double outputOffset, double hostPerDac,
                                               std::size_t oversampling) noexcept
{
    const auto os = static_cast<double>(oversampling);
    return static_cast<std::size_t>(std::ceil(w1 + (outputOffset + w2 + 2.0 * os) * hostPerDac / os)) + 2;
}

// Fixed-capacity stereo FIFO for host-rate output.
struct StereoFifo {
    std::vector<double> left;
    std::vector<double> right;
    std::size_t read = 0;
    std::size_t count = 0;

    void allocate(std::size_t capacity)
    {
        left.assign(capacity, 0.0);
        right.assign(capacity, 0.0);
        clear();
    }
    void clear() noexcept
    {
        read = 0;
        count = 0;
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return left.size(); }
    void push(double l, double r) noexcept
    {
        const std::size_t at = (read + count) % capacity();
        left[at] = l;
        right[at] = r;
        ++count;
    }
    void pop(double& l, double& r) noexcept
    {
        l = left[read];
        r = right[read];
        read = (read + 1) % capacity();
        --count;
    }
};

} // namespace ak4522::detail
