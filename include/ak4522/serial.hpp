#pragma once

#include "ak4522/config.hpp"
#include "ak4522/pcm.hpp"
#include "ak4522/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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

// A pin level with an absolute timestamp in nanoseconds. An edge whose level
// equals the pin's current level is not a transition and is ignored.
struct PinEdge {
    double timeNs = 0.0;
    Pin pin = Pin::sclk;
    bool level = false;
};

// Timing checks ([DS] p.7 unless noted). Limits are inclusive: a value equal
// to a datasheet minimum or maximum passes.
enum class TimingViolation : std::uint8_t {
    sclkPeriod,                // rise to rise < 160 ns (D-T01)
    sclkPulseWidth,            // high or low < 65 ns (D-T02)
    lrckToSclkRise,            // LRCK edge to next SCLK rise < 45 ns (D-T03)
    sclkRiseToLrck,            // SCLK rise to next LRCK edge < 45 ns (D-T04)
    sclkRiseCoincidesWithLrck, // equal timestamps (D-T08); counted instead of D-T03/D-T04
    sdtiSetup,                 // SDTI change to next SCLK rise < 25 ns (D-T06)
    sdtiHold,                  // SCLK rise to next SDTI change < 40 ns (D-T05)
    lrckDutyCycle,             // high time outside 45 to 55 % of the period (D-T07)
    lrckFrequency,             // outside 16 to 48 kHz (D-04)
    mclkFrequency,             // outside the CMODE range (D-C02 to D-C04)
    mclkPulseWidth,            // high or low below 27, 20, or 15 ns for 256, 384, 512fs
    mclkNotSynchronized,       // MCLK rises per LRCK period differ from the CMODE ratio (D-C05)
    pdPulseWidth,              // PD low pulse < 150 ns (D-R03)
    count_,
};

// Checks timestamped pin transitions against the datasheet timing limits.
// Reports violations; does not model metastability or recovery (P-24).
// Timestamps must not decrease; an edge earlier than the previous one is
// ignored. A period, width, or spacing is checked only when both of its edges
// have been observed since reset. Real-time safe.
class TimingValidator {
public:
    [[nodiscard]] Status prepare(CmodePin cmode);
    void reset() noexcept;
    void observe(const PinEdge& edge) noexcept;
    [[nodiscard]] std::uint64_t violations(TimingViolation kind) const noexcept;
    [[nodiscard]] std::uint64_t totalViolations() const noexcept;

private:
    static constexpr std::size_t pinCount = 5;

    void count(TimingViolation kind) noexcept;
    void onSclk(double t, bool rising) noexcept;
    void onLrck(double t, bool rising) noexcept;
    void onSdti(double t) noexcept;
    void onMclk(double t, bool rising) noexcept;
    void onPd(double t, bool rising) noexcept;

    std::array<std::uint64_t, static_cast<std::size_t>(TimingViolation::count_)> counts_{};
    CmodePin cmode_ = CmodePin::low;
    double lastTimeNs_ = 0.0;
    bool anyEdge_ = false;
    std::array<bool, pinCount> known_{};  // pin level observed since reset
    std::array<bool, pinCount> level_{};

    // Times of the latest edges; NaN until observed.
    static constexpr double unseen_ = std::numeric_limits<double>::quiet_NaN();
    double sclkRise_ = unseen_, sclkFall_ = unseen_;
    double lrckRise_ = unseen_, lrckFall_ = unseen_, lrckEdge_ = unseen_;
    double sdtiEdge_ = unseen_;
    double mclkRise_ = unseen_, mclkFall_ = unseen_;
    double pdFall_ = unseen_;
    std::uint32_t mclkRisesThisLrck_ = 0;
    bool mclkCounting_ = false; // the current LRCK period started with MCLK running
};

} // namespace ak4522
