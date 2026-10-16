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
    incompleteFrame, // channel boundary arrived before a full word; frame discarded
    sclkRatioTooLow, // fewer bit clocks per channel than the mode requires
};

struct SerialEdgeResult {
    SerialEvent event = SerialEvent::none;
    std::optional<Pcm20Frame> frame;
};

// Bit-level SDTI decoder for conformance tests and device-style use
// (REQ-SCOPE-02). Call once per SCLK rising edge with the pin levels latched on
// that edge (D-S05). Real-time safe after prepare.
class SerialDecoder {
public:
    // The format is fixed until the next prepare (P-23).
    [[nodiscard]] Status prepare(SerialFormat format);
    void reset() noexcept;
    [[nodiscard]] SerialEdgeResult sclkRisingEdge(bool lrck, bool sdti) noexcept;

private:
    // Decoder state is fixed-size and defined with the M1 implementation.
    SerialFormat format_ = SerialFormat::mode2LeftJustified20;
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
