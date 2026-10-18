// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "analog/analog_noise.hpp"

#include <cmath>
#include <cstddef>

namespace ak4522::detail {

namespace {

constexpr double designedDensity =
#include "analog/noise_density.inc"
    ;

std::uint64_t splitmix(std::uint64_t& state) noexcept
{
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

} // namespace

double AnalogNoise::density() noexcept
{
    return designedDensity;
}

double AnalogNoise::Generator::gaussian() noexcept
{
    // Box-Muller; both outputs are used.
    if (hasSpare) {
        hasSpare = false;
        return spare;
    }
    constexpr double scale = 0x1.0p-53;
    const double u1 = (static_cast<double>(splitmix(state) >> 11) + 0.5) * scale; // (0, 1)
    const double u2 = static_cast<double>(splitmix(state) >> 11) * scale;
    const double radius = std::sqrt(-2.0 * std::log(u1));
    const double angle = 6.283185307179586476925 * u2;
    spare = radius * std::sin(angle);
    hasSpare = true;
    return radius * std::cos(angle);
}

void AnalogNoise::prepare(std::uint64_t seed, double sampleRate) noexcept
{
    // White noise sampled at rate fs carries its power up to fs / 2.
    sigma_ = std::sqrt(designedDensity * sampleRate / 2.0);
    std::uint64_t s = seed;
    left_ = Generator{splitmix(s)};
    right_ = Generator{splitmix(s)};
}

void AnalogNoise::add(std::span<double> left, std::span<double> right) noexcept
{
    const std::size_t n = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t i = 0; i < n; ++i) {
        left[i] += sigma_ * left_.gaussian();
        right[i] += sigma_ * right_.gaussian();
    }
}

} // namespace ak4522::detail
