// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// 8x linear-phase FIR decimator for the ADC (REQ-ADC-02, AD-03 to AD-05).
// Coefficients come from tools/design_decimator.py and are fitted to the
// datasheet filter table (P-105). A pipeline delay brings the total to
// 29.3125 ADC frames. Internal: not part of the public API.

#include <array>
#include <cstddef>
#include <span>

namespace ak4522::detail {

class Decimator8x {
public:
    static constexpr std::size_t factor = 8;
    static constexpr std::size_t taps = 360;
    // Whole 8x samples of delay before the filter (P-105).
    static constexpr std::size_t pipelineSamples = 55;
    // Total delay in 8x samples: (taps - 1) / 2 + pipeline = 234.5.
    static constexpr double delaySamples = (static_cast<double>(taps) - 1.0) / 2.0 + pipelineSamples;

    [[nodiscard]] static std::span<const double, taps> coefficients() noexcept;

    void reset() noexcept;

    // Consumes factor * frames input samples per channel at 8x the ADC rate
    // and writes `frames` outputs per channel. Output frame j is the filtered
    // signal at the input's sample 8j of that block's stream position, so an
    // input at sample 8j appears in output frame j delayed by delaySamples.
    // Input sizes must be a multiple of factor; block boundaries do not
    // affect the result.
    void process(std::span<const double> inLeft, std::span<const double> inRight,
                 std::span<double> outLeft, std::span<double> outRight) noexcept;

private:
    static constexpr std::size_t span_ = taps + pipelineSamples; // history needed

    // Input history, newest first, stored twice so every window is contiguous.
    struct History {
        std::array<double, 2 * span_> samples{};
        std::size_t head = 0;
        void push(double x) noexcept;
        [[nodiscard]] const double* window() const noexcept { return samples.data() + head; }
    };

    History left_;
    History right_;
};

} // namespace ak4522::detail
