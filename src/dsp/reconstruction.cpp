// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "dsp/reconstruction.hpp"

#include <cmath>

namespace ak4522::detail {

namespace {

constexpr std::array<double, 5 * Reconstruction::sections> designed{
#include "dsp/reconstruction_coefficients.inc"
};

constexpr std::array<Reconstruction::Section, Reconstruction::sections> makeSections()
{
    std::array<Reconstruction::Section, Reconstruction::sections> out{};
    for (std::size_t s = 0; s < Reconstruction::sections; ++s) {
        out[s] = {designed[5 * s], designed[5 * s + 1], designed[5 * s + 2],
                  designed[5 * s + 3], designed[5 * s + 4]};
    }
    return out;
}

constexpr auto sectionTable = makeSections();

// Filter states below this magnitude are set to zero so decaying tails never
// reach subnormal numbers, which are slow on some processors (REQ-RT-01,
// P-89). It is about 300 dB below full scale.
constexpr double flushThreshold = 1e-15;

double flushed(double v) noexcept
{
    return std::abs(v) < flushThreshold ? 0.0 : v;
}

} // namespace

double PipelineDelay::Line::tick(double x) noexcept
{
    const double y = buffer[position];
    buffer[position] = x;
    position = position + 1 == samples ? 0 : position + 1;
    return y;
}

void PipelineDelay::reset() noexcept
{
    left_ = Line{};
    right_ = Line{};
}

void PipelineDelay::process(std::span<double> left, std::span<double> right) noexcept
{
    const std::size_t n = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = left_.tick(left[i]);
        right[i] = right_.tick(right[i]);
    }
}

std::span<const Reconstruction::Section, Reconstruction::sections> Reconstruction::coefficients() noexcept
{
    return sectionTable;
}

double Reconstruction::Channel::tick(double x) noexcept
{
    double v = x;
    for (std::size_t s = 0; s < sections; ++s) {
        const Section& c = sectionTable[s];
        auto& z = state[s];
        const double y = c.b0 * v + z[0];
        z[0] = flushed(c.b1 * v - c.a1 * y + z[1]);
        z[1] = flushed(c.b2 * v - c.a2 * y);
        v = y;
    }
    return v;
}

void Reconstruction::reset() noexcept
{
    left_ = Channel{};
    right_ = Channel{};
}

void Reconstruction::process(std::span<double> left, std::span<double> right) noexcept
{
    const std::size_t n = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = left_.tick(left[i]);
        right[i] = right_.tick(right[i]);
    }
}

} // namespace ak4522::detail
