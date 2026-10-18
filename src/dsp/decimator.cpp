// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "dsp/decimator.hpp"

namespace ak4522::detail {

namespace {

constexpr std::array<double, Decimator8x::taps> designed{
#include "dsp/decimator_coefficients.inc"
};

} // namespace

std::span<const double, Decimator8x::taps> Decimator8x::coefficients() noexcept
{
    return designed;
}

void Decimator8x::History::push(double x) noexcept
{
    head = head == 0 ? span_ - 1 : head - 1;
    samples[head] = x;
    samples[head + span_] = x;
}

void Decimator8x::reset() noexcept
{
    left_ = History{};
    right_ = History{};
}

void Decimator8x::process(std::span<const double> inLeft, std::span<const double> inRight,
                          std::span<double> outLeft, std::span<double> outRight) noexcept
{
    const std::size_t frames = (inLeft.size() < inRight.size() ? inLeft.size() : inRight.size()) / factor;
    for (std::size_t j = 0; j < frames; ++j) {
        // The output is taken at the first sample of each group of eight.
        left_.push(inLeft[j * factor]);
        right_.push(inRight[j * factor]);
        // window()[k] is the input k samples ago; skip the pipeline.
        const double* xl = left_.window() + pipelineSamples;
        const double* xr = right_.window() + pipelineSamples;
        double l0 = 0.0, l1 = 0.0, r0 = 0.0, r1 = 0.0;
        for (std::size_t k = 0; k < taps; k += 2) { // taps is even
            l0 += designed[k] * xl[k];
            l1 += designed[k + 1] * xl[k + 1];
            r0 += designed[k] * xr[k];
            r1 += designed[k + 1] * xr[k + 1];
        }
        outLeft[j] = l0 + l1;
        outRight[j] = r0 + r1;
        for (std::size_t p = 1; p < factor; ++p) {
            left_.push(inLeft[j * factor + p]);
            right_.push(inRight[j * factor + p]);
        }
    }
}

} // namespace ak4522::detail
