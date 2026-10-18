// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "dsp/interpolator.hpp"

namespace ak4522::detail {

namespace {

constexpr std::array<double, Interpolator8x::taps> designed{
#include "dsp/interpolator_coefficients.inc"
};

// Polyphase table: phase p, tap k holds h[k * factor + p], zero beyond the
// designed length.
constexpr auto makePhases()
{
    std::array<std::array<double, Interpolator8x::tapsPerPhase>, Interpolator8x::factor> phases{};
    for (std::size_t p = 0; p < Interpolator8x::factor; ++p) {
        for (std::size_t k = 0; k < Interpolator8x::tapsPerPhase; ++k) {
            const std::size_t n = k * Interpolator8x::factor + p;
            phases[p][k] = n < Interpolator8x::taps ? designed[n] : 0.0;
        }
    }
    return phases;
}

constexpr auto phases = makePhases();

} // namespace

std::span<const double, Interpolator8x::taps> Interpolator8x::coefficients() noexcept
{
    return designed;
}

void Interpolator8x::History::push(double x) noexcept
{
    head = head == 0 ? tapsPerPhase - 1 : head - 1;
    samples[head] = x;
    samples[head + tapsPerPhase] = x;
}

void Interpolator8x::interpolateFrame(const History& history, double* out) noexcept
{
    const double* x = history.window(); // x[k] is the input k frames ago
    for (std::size_t p = 0; p < factor; ++p) {
        double sum = 0.0;
        for (std::size_t k = 0; k < tapsPerPhase; ++k) {
            sum += phases[p][k] * x[k];
        }
        out[p] = sum;
    }
}

void Interpolator8x::reset() noexcept
{
    left_ = History{};
    right_ = History{};
}

void Interpolator8x::process(std::span<const double> inLeft, std::span<const double> inRight,
                             std::span<double> outLeft, std::span<double> outRight) noexcept
{
    const std::size_t frames = inLeft.size() < inRight.size() ? inLeft.size() : inRight.size();
    for (std::size_t i = 0; i < frames; ++i) {
        left_.push(inLeft[i]);
        right_.push(inRight[i]);
        interpolateFrame(left_, outLeft.data() + i * factor);
        interpolateFrame(right_, outRight.data() + i * factor);
    }
}

} // namespace ak4522::detail
