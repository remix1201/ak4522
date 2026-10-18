// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// PCM mapping and quantization (REQ-PCM-01 to REQ-PCM-03; D-P01, D-P02,
// I-P01, I-P02; P-10). Expected values come from integer arithmetic or from
// the C library's own rounding, never from the code under test.

#include "ak4522/pcm.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

using namespace ak4522;

namespace {

constexpr double lsb = 1.0 / 524288.0; // 2^-19

Pcm20 quantize(double x)
{
    QuantizeCounters counters;
    return quantizeToPcm20(x, counters);
}

} // namespace

TEST_CASE("Raw 20-bit words sign-extend at the documented boundaries", "[unit][pcm]")
{
    CHECK(pcm20FromRaw(0x80000u).code == -524288); // negative full scale (D-P02)
    CHECK(pcm20FromRaw(0x7FFFFu).code == 524287);  // positive full scale (D-P02)
    CHECK(pcm20FromRaw(0x00000u).code == 0);
    CHECK(pcm20FromRaw(0x00001u).code == 1);
    CHECK(pcm20FromRaw(0xFFFFFu).code == -1);
    CHECK(pcm20FromRaw(0x80001u).code == -524287);
}

TEST_CASE("Raw words ignore bits above bit 19", "[unit][pcm]")
{
    CHECK(pcm20FromRaw(0xFFF80000u).code == -524288);
    CHECK(pcm20FromRaw(0x0017FFFFu).code == 524287);
    CHECK(pcm20FromRaw(0xFFFFFFFFu).code == -1);
    CHECK(pcm20FromRaw(0xABC00000u).code == 0);
}

TEST_CASE("Every 20-bit word maps to its two's complement value", "[unit][pcm]")
{
    std::uint64_t failures = 0;
    for (std::int64_t word = 0; word < (1 << 20); ++word) {
        const std::int64_t expected = word < (1 << 19) ? word : word - (1 << 20);
        if (pcm20FromRaw(static_cast<std::uint32_t>(word)).code != expected) {
            ++failures;
        }
    }
    CHECK(failures == 0);
}

TEST_CASE("Normalization endpoints are exact", "[unit][pcm]")
{
    CHECK(toNormalized(Pcm20{Pcm20::minCode}) == -1.0);
    CHECK(toNormalized(Pcm20{Pcm20::maxCode}) == 1.0 - lsb);
    CHECK(toNormalized(Pcm20{0}) == 0.0);
    CHECK(toNormalized(Pcm20{1}) == lsb);
}

TEST_CASE("Every 20-bit code survives normalize then quantize unchanged", "[unit][pcm]")
{
    QuantizeCounters counters;
    std::uint64_t failures = 0;
    for (std::int32_t code = Pcm20::minCode; code <= Pcm20::maxCode; ++code) {
        if (quantizeToPcm20(toNormalized(Pcm20{code}), counters).code != code) {
            ++failures;
        }
    }
    CHECK(failures == 0);
    CHECK(counters.clipped == 0);
    CHECK(counters.nonFinite == 0);
}

TEST_CASE("Every 16-bit sample scales by 16 with four zero low bits", "[unit][pcm]")
{
    std::uint64_t failures = 0;
    for (std::int32_t sample = -32768; sample <= 32767; ++sample) {
        const Pcm20 pcm = pcm20FromInt16(static_cast<std::int16_t>(sample));
        const bool scaled = pcm.code == sample * 16;
        const bool lowBitsZero = (static_cast<std::uint32_t>(pcm.code) & 0xFu) == 0;
        const bool normalized = toNormalized(pcm) == static_cast<double>(sample) / 32768.0;
        if (!scaled || !lowBitsZero || !normalized) {
            ++failures;
        }
    }
    CHECK(failures == 0);
    CHECK(pcm20FromInt16(-32768).code == Pcm20::minCode);
    CHECK(pcm20FromInt16(32767).code == 524272);
}

TEST_CASE("Quantization rounds half-LSB ties to even", "[unit][pcm]")
{
    const auto [input, expected] = GENERATE(table<double, std::int32_t>({
        {0.5, 0},
        {1.5, 2},
        {2.5, 2},
        {3.5, 4},
        {-0.5, 0},
        {-1.5, -2},
        {-2.5, -2},
        {524286.5, 524286},
        {-524287.5, -524288},
    }));
    CAPTURE(input);
    CHECK(quantize(input * lsb).code == expected);
}

TEST_CASE("Quantization rounds values beside a tie to the nearer code", "[unit][pcm]")
{
    const double half = 0.5 * lsb;
    CHECK(quantize(std::nextafter(half, 1.0)).code == 1);
    CHECK(quantize(std::nextafter(half, 0.0)).code == 0);
    CHECK(quantize(-std::nextafter(half, 1.0)).code == -1);
    CHECK(quantize(-std::nextafter(half, 0.0)).code == 0);
}

TEST_CASE("Quantization saturates and counts clipping", "[unit][pcm]")
{
    const auto [input, expected, clips] = GENERATE(table<double, std::int32_t, std::uint64_t>({
        {1.0, Pcm20::maxCode, 1},                   // +1.0 is not representable
        {1.0 - lsb, Pcm20::maxCode, 0},
        {524287.5 * lsb, Pcm20::maxCode, 1},        // tie rounds to even 524288, then saturates
        {-1.0, Pcm20::minCode, 0},                  // -1.0 is exactly representable
        {-524288.5 * lsb, Pcm20::minCode, 0},       // tie rounds to even -524288, in range
        {-524288.5 * lsb - lsb, Pcm20::minCode, 1},
        {2.0, Pcm20::maxCode, 1},
        {-2.0, Pcm20::minCode, 1},
        {1e300, Pcm20::maxCode, 1},
        {-std::numeric_limits<double>::max(), Pcm20::minCode, 1},
    }));
    CAPTURE(input);
    QuantizeCounters counters;
    CHECK(quantizeToPcm20(input, counters).code == expected);
    CHECK(counters.clipped == clips);
    CHECK(counters.nonFinite == 0);
}

TEST_CASE("Non-finite input becomes zero and is counted", "[unit][pcm]")
{
    QuantizeCounters counters;
    CHECK(quantizeToPcm20(std::numeric_limits<double>::quiet_NaN(), counters).code == 0);
    CHECK(quantizeToPcm20(std::numeric_limits<double>::infinity(), counters).code == 0);
    CHECK(quantizeToPcm20(-std::numeric_limits<double>::infinity(), counters).code == 0);
    CHECK(counters.nonFinite == 3);
    CHECK(counters.clipped == 0);
}

TEST_CASE("Signed zero and subnormal input quantize to zero", "[unit][pcm]")
{
    CHECK(quantize(-0.0).code == 0);
    CHECK(quantize(std::numeric_limits<double>::denorm_min()).code == 0);
    CHECK(quantize(-std::numeric_limits<double>::denorm_min()).code == 0);
}

TEST_CASE("Quantization matches the C library's ties-to-even rounding", "[unit][pcm]")
{
    REQUIRE(std::fegetround() == FE_TONEAREST);

    std::mt19937_64 rng(0x4B4135323200u);
    std::uniform_real_distribution<double> wide(-1.25, 1.25);
    std::uniform_int_distribution<std::int32_t> code(Pcm20::minCode - 2, Pcm20::maxCode + 2);

    std::uint64_t failures = 0;
    for (int i = 0; i < 200000; ++i) {
        // Alternate arbitrary values with exact half-LSB ties.
        const double x = (i % 2 == 0) ? wide(rng) : (code(rng) + 0.5) * lsb;
        double reference = std::nearbyint(x * 524288.0);
        reference = std::fmin(std::fmax(reference, Pcm20::minCode), Pcm20::maxCode);
        if (quantize(x).code != static_cast<std::int32_t>(reference)) {
            ++failures;
        }
    }
    CHECK(failures == 0);
}
