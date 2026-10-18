// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// 8x linear-phase FIR interpolator (REQ-FILTER-01, D-02). Coefficients come
// from tools/design_interpolator.py and are fitted to the datasheet filter
// table (P-87, P-88). Internal: not part of the public API.

#include <array>
#include <cstddef>
#include <span>

namespace ak4522::detail {

class Interpolator8x {
public:
    static constexpr std::size_t factor = 8;
    static constexpr std::size_t taps = 191;
    static constexpr std::size_t tapsPerPhase = (taps + factor - 1) / factor;
    // Delay in 8x samples; (taps - 1) / 2 for a symmetric FIR.
    static constexpr std::size_t delaySamples = (taps - 1) / 2;

    // The designed impulse response, in 8x samples.
    [[nodiscard]] static std::span<const double, taps> coefficients() noexcept;

    // Clears the input history of both channels.
    void reset() noexcept;

    // Interpolates frames.size() DAC frames per channel into 8 output samples
    // each. Output spans must hold factor * input size; input and output must
    // not overlap. Block boundaries do not affect the result.
    void process(std::span<const double> inLeft, std::span<const double> inRight,
                 std::span<double> outLeft, std::span<double> outRight) noexcept;

private:
    // Input history, newest first, stored twice so every dot product reads a
    // contiguous window without wrapping.
    struct History {
        std::array<double, 2 * tapsPerPhase> samples{};
        std::size_t head = 0;

        void push(double x) noexcept;
        [[nodiscard]] const double* window() const noexcept { return samples.data() + head; }
    };

    static void interpolateFrame(const History& history, double* out) noexcept;

    History left_;
    History right_;
};

} // namespace ak4522::detail
