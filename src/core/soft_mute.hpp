#pragma once

// Digital soft mute driven by the SMUTE pin (REQ-MUTE-01, REQ-MUTE-02).
// Internal: DacCore applies it after de-emphasis (P-40). Not part of the
// public API.

#include <cstdint>
#include <span>

namespace ak4522::detail {

// Linear amplitude ramp on an integer position 0..1024 (P-50). Each DAC frame
// moves the position one step toward the target, so a full transition takes
// 1024 frames (D-M02, D-M03), and a reversal retraces from the current
// position at the same slope (D-M04, P-51). Both channels share one ramp.
class SoftMute {
public:
    static constexpr std::int32_t rampLength = 1024;

    // State after PD release: muted if SMUTE is high, unity if low (P-72).
    void reset(bool pinHigh) noexcept;

    // Applies an SMUTE level change before the next frame's update (P-52).
    // Returns false, changing nothing, if the level is already current (P-53).
    bool setPin(bool pinHigh) noexcept;

    // Advances one DAC frame and returns that frame's gain.
    [[nodiscard]] double nextGain() noexcept;

    // Applies the ramp to one block with no SMUTE change inside it. The
    // caller splits blocks at events. Channels must have equal length and may
    // alias the caller's buffers.
    void process(std::span<double> left, std::span<double> right) noexcept;

    [[nodiscard]] std::int32_t position() const noexcept { return position_; }
    [[nodiscard]] bool pinHigh() const noexcept { return pinHigh_; }
    [[nodiscard]] bool settled() const noexcept { return position_ == target(); }

private:
    [[nodiscard]] std::int32_t target() const noexcept { return pinHigh_ ? 0 : rampLength; }

    std::int32_t position_ = rampLength;
    bool pinHigh_ = false;
};

} // namespace ak4522::detail
