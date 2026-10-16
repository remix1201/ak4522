#pragma once

#include "ak4522/config.hpp"
#include "ak4522/pcm.hpp"
#include "ak4522/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ak4522 {

// Outcome of one SCLK rising edge in the serial decoder (P-21, P-22).
enum class SerialEvent : std::uint8_t {
    none,            // bit consumed; no frame completed
    frameComplete,   // a stereo frame is available
    incompleteFrame, // right word ended without a valid left word before it; nothing output
    sclkRatioTooLow, // a channel had fewer bit clocks than the mode requires; word discarded
};

struct SerialEdgeResult {
    SerialEvent event = SerialEvent::none;
    std::optional<Pcm20Frame> frame;
};

// Bit-level SDTI decoder for conformance tests and device-style use
// (REQ-SCOPE-02). Call once per SCLK rising edge with the pin levels latched on
// that edge (D-S05). Real-time safe after prepare.
//
// A channel word is decoded when LRCK changes, from the bit clocks of the
// half-frame that just ended (D-S01 to D-S11):
//   mode 0: last 16 bits, scaled to 20-bit weight (REQ-PCM-02)
//   mode 1: last 20 bits
//   mode 2: first 20 bits
//   mode 3: from the second bit clock of the half, continuing into the first
//           bit clock after the next LRCK edge; with fewer than 20 bit clocks
//           per channel the missing low bits are zero (D-S06, D-S07)
// Bits before the first LRCK edge after reset are discarded, because that
// half-frame may be partial (D-C06). A frame is output when its right word is
// decoded and a valid left word immediately precedes it (P-21).
class SerialDecoder {
public:
    // The format is fixed until the next prepare (P-23).
    [[nodiscard]] Status prepare(SerialFormat format);
    void reset() noexcept;
    [[nodiscard]] SerialEdgeResult sclkRisingEdge(bool lrck, bool sdti) noexcept;

private:
    void appendBit(bool bit) noexcept;
    void startHalf(bool lrck) noexcept;
    [[nodiscard]] SerialEdgeResult finishHalf() noexcept;

    SerialFormat format_ = SerialFormat::mode2LeftJustified20;
    bool started_ = false;      // at least one bit clock seen since reset
    bool synchronized_ = false; // an LRCK edge has been seen since reset
    bool lrck_ = false;         // LRCK level of the current half-frame
    std::uint32_t slots_ = 0;   // bit clocks in the current half-frame
    std::uint32_t tail_ = 0;    // last bits received, newest in bit 0
    std::uint32_t head_ = 0;    // first data bits received, MSB first
    std::uint32_t headBits_ = 0;
    Pcm20 left_{};
    bool leftValid_ = false;
};

enum class Pin : std::uint8_t { mclk, lrck, sclk, sdti, pd };

// A pin transition with an absolute timestamp in nanoseconds.
struct PinEdge {
    double timeNs = 0.0;
    Pin pin = Pin::sclk;
    bool level = false;
};

// Timing checks from D-T01 to D-T08, D-C02 to D-C04, and D-R03.
enum class TimingViolation : std::uint8_t {
    sclkPeriod,
    sclkPulseWidth,
    lrckToSclkRise,
    sclkRiseToLrck,
    sclkRiseCoincidesWithLrck,
    sdtiSetup,
    sdtiHold,
    lrckDutyCycle,
    mclkFrequency,
    mclkPulseWidth,
    pdPulseWidth,
    count_,
};

// Reports violations; does not model metastability (P-24).
class TimingValidator {
public:
    [[nodiscard]] Status prepare(CmodePin cmode, double dacRate);
    void reset() noexcept;
    void observe(const PinEdge& edge) noexcept;
    [[nodiscard]] std::uint64_t violations(TimingViolation kind) const noexcept;

private:
    // Edge history is fixed-size and defined with the M1 implementation.
    std::array<std::uint64_t, static_cast<std::size_t>(TimingViolation::count_)> counts_{};
};

} // namespace ak4522
