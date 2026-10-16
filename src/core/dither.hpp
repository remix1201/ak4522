#pragma once

// TPDF dither for host input before 20-bit or 16-bit quantization (P-12,
// P-91). A host-input feature, not chip behavior. Internal.

#include <cstdint>

namespace ak4522::detail {

// splitmix64: seeds and steps the generators (P-91).
inline std::uint64_t splitmix(std::uint64_t& state) noexcept
{
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Uniform in [0, 1).
inline double uniform(std::uint64_t& state) noexcept
{
    return static_cast<double>(splitmix(state) >> 11) * 0x1.0p-53;
}

// One independent stream per channel, both derived from one seed.
struct TpdfDither {
    std::uint64_t left = 0;
    std::uint64_t right = 0;

    void seed(std::uint64_t seed) noexcept
    {
        left = splitmix(seed);
        right = splitmix(seed);
    }

    // Triangular noise of +/-1 LSB, where lsb is the target step.
    [[nodiscard]] static double next(std::uint64_t& state, double lsb) noexcept
    {
        return (uniform(state) + uniform(state) - 1.0) * lsb;
    }
};

} // namespace ak4522::detail
