#pragma once

#include <cstdint>

namespace ak4522 {

// A sign-extended 20-bit two's complement DAC sample (REQ-PCM-01, D-P01, D-P02).
struct Pcm20 {
    static constexpr std::int32_t minCode = -524288; // 80000h, negative full scale
    static constexpr std::int32_t maxCode = 524287;  // 7FFFFh, positive full scale

    std::int32_t code = 0; // 0 produces VCOM (D-P03)

    friend constexpr bool operator==(Pcm20, Pcm20) = default;
};

struct Pcm20Frame {
    Pcm20 left;
    Pcm20 right;

    friend constexpr bool operator==(const Pcm20Frame&, const Pcm20Frame&) = default;
};

// Counters for float-to-PCM conversion (P-10).
struct QuantizeCounters {
    std::uint64_t nonFinite = 0; // NaN or infinity replaced by zero
    std::uint64_t clipped = 0;   // saturated to the representable range
};

// Sign-extends the low 20 bits of a raw serial word. Upper bits are ignored.
[[nodiscard]] Pcm20 pcm20FromRaw(std::uint32_t word) noexcept;

// Scales a 16-bit sample to 20-bit weight by multiplication by 16 (REQ-PCM-02).
[[nodiscard]] Pcm20 pcm20FromInt16(std::int16_t sample) noexcept;

// Quantizes normalized audio: non-finite to zero, multiply by 2^19, round to
// nearest with ties to even, saturate (REQ-PCM-03, P-10).
[[nodiscard]] Pcm20 quantizeToPcm20(double sample, QuantizeCounters& counters) noexcept;

// code / 524288.0; negative full scale is exactly -1, positive is 1 - 2^-19.
[[nodiscard]] double toNormalized(Pcm20 sample) noexcept;

} // namespace ak4522
