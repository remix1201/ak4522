// Portable 128-bit multiply-divide (P-37, P-41). Checked against hand-computed
// values everywhere, and against the compiler's 128-bit integers where they exist.

#include "core/wide_math.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <random>

using namespace ak4522::detail;

TEST_CASE("Multiply-divide matches hand-computed values", "[unit]")
{
    constexpr std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    static_assert(mulDivFloor(max, max, max) == max);
    static_assert(mulDivFloor(1ull << 40, 1ull << 40, 1ull << 30) == 1ull << 50);
    static_assert(mulDivFloor(147, 160, 10) == 2352);
    static_assert(mulDivCeil(7, 3, 2) == 11);  // 10.5 rounds up
    static_assert(mulDivFloor(7, 3, 2) == 10);
    static_assert(mulDivCeil(8, 3, 2) == 12);  // exact
    // 10^12 host frames at 48 / 44.1 kHz: 10^12 * 147 / 160, beyond 64-bit products.
    CHECK(mulDivCeil(1'000'000'000'000ull, 147, 160) == 918'750'000'000ull);
    CHECK(mulDivFloor(3'000'000'000'000ull, 11'200'000'000ull, 48'000'000'000ull) == 700'000'000'000ull);
    const U128 p = multiply(max, max); // (2^64 - 1)^2 = 2^128 - 2^65 + 1
    CHECK(p.high == max - 1);
    CHECK(p.low == 1);
}

#if defined(__SIZEOF_INT128__)
TEST_CASE("Multiply-divide matches 128-bit integers", "[unit]")
{
    std::mt19937_64 rng(31);
    for (int i = 0; i < 200000; ++i) {
        const std::uint64_t a = rng() >> (rng() % 64);
        const std::uint64_t b = rng() >> (rng() % 64);
        std::uint64_t d = (rng() >> (rng() % 64)) | 1u;
        const unsigned __int128 product = static_cast<unsigned __int128>(a) * b;
        if ((product >> 64) >= d) {
            continue; // quotient would not fit 64 bits
        }
        const auto quotient = static_cast<std::uint64_t>(product / d);
        const bool exact = product % d == 0;
        REQUIRE(mulDivFloor(a, b, d) == quotient);
        REQUIRE(mulDivCeil(a, b, d) == (exact ? quotient : quotient + 1));
    }
}
#endif
