// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "dsp/adc_highpass.hpp"

#include <algorithm>
#include <cmath>

namespace ak4522::detail {

namespace {

constexpr double pi = 3.14159265358979323846;

// Decaying states below this are set to zero to avoid subnormal arithmetic;
// far below one 20-bit step (as in the reconstruction sections).
constexpr double flushThreshold = 1e-15;

double flushed(double v) noexcept
{
    return std::abs(v) < flushThreshold ? 0.0 : v;
}

} // namespace

void AdcHighPass::prepare(double adcRate) noexcept
{
    corner_ = cornerAt44100 * adcRate / 44100.0; // scales with fs (AD-06)
    const double k = std::tan(pi * corner_ / adcRate);
    b0_ = 1.0 / (1.0 + k);
    a1_ = (k - 1.0) / (k + 1.0);
    reset();
}

void AdcHighPass::reset() noexcept
{
    xl_ = yl_ = xr_ = yr_ = 0.0;
}

void AdcHighPass::process(std::span<double> left, std::span<double> right) noexcept
{
    const std::size_t n = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < n; ++i) {
        const double l = left[i];
        const double r = right[i];
        yl_ = flushed(b0_ * (l - xl_) - a1_ * yl_);
        yr_ = flushed(b0_ * (r - xr_) - a1_ * yr_);
        xl_ = l;
        xr_ = r;
        left[i] = yl_;
        right[i] = yr_;
    }
}

} // namespace ak4522::detail
