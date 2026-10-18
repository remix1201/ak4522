// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "ak4522/pcm.hpp"

#include <cmath>

namespace ak4522 {

namespace {

constexpr std::uint32_t wordMask = 0xFFFFFu;
constexpr std::uint32_t signBit = 0x80000u;
constexpr std::int32_t wordSpan = 0x100000; // 2^20
constexpr double fullScale = 524288.0;       // 2^19

// Round to nearest, ties to even, without relying on the floating-point
// environment's rounding mode (P-10). Valid for |x| well below 2^52, where
// x - floor(x) is exact.
double roundHalfToEven(double x) noexcept
{
    const double lower = std::floor(x);
    const double fraction = x - lower;
    if (fraction > 0.5) {
        return lower + 1.0;
    }
    if (fraction < 0.5) {
        return lower;
    }
    const bool lowerIsEven = std::fmod(lower, 2.0) == 0.0;
    return lowerIsEven ? lower : lower + 1.0;
}

} // namespace

Pcm20 pcm20FromRaw(std::uint32_t word) noexcept
{
    const std::uint32_t bits = word & wordMask;
    const auto magnitude = static_cast<std::int32_t>(bits);
    return Pcm20{(bits & signBit) != 0 ? magnitude - wordSpan : magnitude};
}

Pcm20 pcm20FromInt16(std::int16_t sample) noexcept
{
    return Pcm20{static_cast<std::int32_t>(sample) * 16};
}

Pcm20 quantizeToPcm20(double sample, QuantizeCounters& counters) noexcept
{
    if (!std::isfinite(sample)) {
        ++counters.nonFinite;
        return Pcm20{};
    }

    // Limit the range before rounding so huge inputs stay exact and
    // convertible; anything beyond one code outside the range saturates anyway.
    constexpr double limit = fullScale + 1.0;
    double scaled = sample * fullScale;
    if (scaled > limit) {
        scaled = limit;
    } else if (scaled < -limit) {
        scaled = -limit;
    }

    const double rounded = roundHalfToEven(scaled);
    if (rounded > static_cast<double>(Pcm20::maxCode)) {
        ++counters.clipped;
        return Pcm20{Pcm20::maxCode};
    }
    if (rounded < static_cast<double>(Pcm20::minCode)) {
        ++counters.clipped;
        return Pcm20{Pcm20::minCode};
    }
    return Pcm20{static_cast<std::int32_t>(rounded)};
}

Pcm20 quantizeTo16Bit(double sample, QuantizeCounters& counters) noexcept
{
    if (!std::isfinite(sample)) {
        ++counters.nonFinite;
        return Pcm20{};
    }
    constexpr double scale = 32768.0;
    constexpr double limit = scale + 1.0;
    double scaled = sample * scale;
    if (scaled > limit) {
        scaled = limit;
    } else if (scaled < -limit) {
        scaled = -limit;
    }
    const double rounded = roundHalfToEven(scaled);
    if (rounded > 32767.0) {
        ++counters.clipped;
        return pcm20FromInt16(32767);
    }
    if (rounded < -32768.0) {
        ++counters.clipped;
        return pcm20FromInt16(-32768);
    }
    return pcm20FromInt16(static_cast<std::int16_t>(rounded));
}

double toNormalized(Pcm20 sample) noexcept
{
    return static_cast<double>(sample.code) / fullScale;
}

} // namespace ak4522
