// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// Exact a * b / d for 64-bit unsigned operands with a 128-bit intermediate,
// in portable C++ (MSVC has no __int128, and its _udiv128 is x64-only).
// Used for exact rate arithmetic (P-37, P-41). Internal.

#include <cstdint>

namespace ak4522::detail {

struct U128 {
    std::uint64_t high;
    std::uint64_t low;
};

[[nodiscard]] constexpr U128 multiply(std::uint64_t a, std::uint64_t b) noexcept
{
    constexpr std::uint64_t mask = 0xFFFFFFFFull;
    const std::uint64_t aLow = a & mask, aHigh = a >> 32;
    const std::uint64_t bLow = b & mask, bHigh = b >> 32;
    const std::uint64_t lowLow = aLow * bLow;
    const std::uint64_t lowHigh = aLow * bHigh;
    const std::uint64_t highLow = aHigh * bLow;
    const std::uint64_t highHigh = aHigh * bHigh;
    const std::uint64_t middle = (lowLow >> 32) + (lowHigh & mask) + (highLow & mask);
    return {highHigh + (lowHigh >> 32) + (highLow >> 32) + (middle >> 32), (lowLow & mask) | (middle << 32)};
}

// floor(x / d) with the remainder. Requires d > 0 and x.high < d, so the
// quotient fits in 64 bits.
[[nodiscard]] constexpr std::uint64_t divide(U128 x, std::uint64_t d, std::uint64_t& remainder) noexcept
{
    std::uint64_t r = x.high;
    std::uint64_t q = 0;
    for (int bit = 63; bit >= 0; --bit) {
        const bool carry = (r >> 63) != 0;
        r = (r << 1) | ((x.low >> bit) & 1u);
        q <<= 1;
        if (carry || r >= d) {
            r -= d;
            q |= 1u;
        }
    }
    remainder = r;
    return q;
}

[[nodiscard]] constexpr std::uint64_t mulDivFloor(std::uint64_t a, std::uint64_t b, std::uint64_t d) noexcept
{
    std::uint64_t remainder = 0;
    return divide(multiply(a, b), d, remainder);
}

[[nodiscard]] constexpr std::uint64_t mulDivCeil(std::uint64_t a, std::uint64_t b, std::uint64_t d) noexcept
{
    std::uint64_t remainder = 0;
    const std::uint64_t q = divide(multiply(a, b), d, remainder);
    return remainder != 0 ? q + 1 : q;
}

} // namespace ak4522::detail
