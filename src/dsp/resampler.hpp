#pragma once

// Arbitrary-ratio sample-rate converter for host-rate adaptation
// (REQ-RATE-01, REQ-RATE-02, REQ-RATE-04). Host behavior, not part of the chip
// model. Internal: not part of the public API.
//
// Output sample m represents input time m * P / Q + startOffset exactly,
// where P / Q is the input-to-output rate ratio in lowest terms (P-37). The
// offset is constant, so each phase row is still exact. It is a Kaiser-windowed
// sinc interpolation of the 2W input samples around that time (P-38), and is
// written as soon as the last of them has arrived, so the converter delays
// the signal by W input samples.

#include "ak4522/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ak4522::detail {

struct ResamplerSpec {
    double inputRate = 0.0;    // Hz
    double outputRate = 0.0;   // Hz
    // Band edges in Hz. Zero selects the defaults: passband 20/44.1 of the
    // lower rate, stopband at the lower rate minus the passband edge (P-38).
    double passbandHz = 0.0;
    double stopbandHz = 0.0;
    double attenuationDb = 125.0; // Kaiser design target (P-38)
    // Time of output 0 in input samples, >= 0. Used to align the plugin's
    // total latency to whole host frames (P-39).
    double startOffset = 0.0;
    // Exact input-to-output ratio, input samples per output sample. When both
    // are nonzero they replace the micro-hertz ratio of the two rates, for
    // rates that are not whole micro-hertz (P-100). The rates above still set
    // the kernel's band edges.
    std::int64_t ratioNumerator = 0;
    std::int64_t ratioDenominator = 0;
};

class Resampler {
public:
    // Designs the kernel and allocates every buffer. Off the audio thread.
    [[nodiscard]] Status prepare(const ResamplerSpec& spec);

    // Clears history and restarts output timing at input sample zero.
    void reset() noexcept;

    // Consumes all input frames and writes every output that becomes
    // available. Output spans must hold maxOutputsFor(input size) frames.
    // Returns the number of output frames written. Real-time safe.
    std::size_t process(std::span<const double> inLeft, std::span<const double> inRight,
                        std::span<double> outLeft, std::span<double> outRight) noexcept;

    // Upper bound on outputs produced by the next `inputFrames` inputs.
    [[nodiscard]] std::size_t maxOutputsFor(std::size_t inputFrames) const noexcept;

    // Kernel half-width W in input samples; the converter's delay.
    [[nodiscard]] std::size_t halfWidth() const noexcept { return halfWidth_; }
    // True when every output phase has its own exact kernel row.
    [[nodiscard]] bool exactPhases() const noexcept { return exact_; }
    [[nodiscard]] std::int64_t ratioNumerator() const noexcept { return p_; }
    [[nodiscard]] std::int64_t ratioDenominator() const noexcept { return q_; }
    [[nodiscard]] double passbandHz() const noexcept { return passbandHz_; }
    [[nodiscard]] double stopbandHz() const noexcept { return stopbandHz_; }

    // Phase rows used when the ratio denominator is too large for exact rows.
    static constexpr std::size_t interpolatedPhases = 4096;

private:
    struct Channel {
        std::vector<double> ring; // 2 * taps, mirrored
        std::size_t head = 0;
        void push(double x, std::size_t taps) noexcept;
        [[nodiscard]] const double* window() const noexcept { return ring.data() + head; }
    };

    [[nodiscard]] double dot(const double* row, const double* x) const noexcept;
    [[nodiscard]] double output(const Channel& channel) const noexcept;

    std::int64_t p_ = 1;          // input samples per output, numerator
    std::int64_t q_ = 1;          // denominator
    std::int64_t pWhole_ = 0;     // p_ / q_
    std::int64_t pRemainder_ = 0; // p_ % q_
    std::size_t halfWidth_ = 0;
    std::size_t taps_ = 0;        // 2 W
    bool exact_ = true;
    double passbandHz_ = 0.0;
    double stopbandHz_ = 0.0;

    // Kernel rows: exact mode has q_ rows; interpolated mode has
    // interpolatedPhases + 1 rows. Each row holds taps_ coefficients, applied
    // to the window oldest sample first.
    std::vector<double> rows_;
    std::vector<double> blended_; // scratch row for interpolated mode

    // Integer part of the start offset; its fractional part is folded into
    // the rows, and rowCarry_ marks phases whose fraction passed one.
    std::int64_t offsetWhole_ = 0;
    double offsetFraction_ = 0.0;
    std::vector<std::uint8_t> rowCarry_; // exact mode, per row

    [[nodiscard]] std::int64_t effectiveIndex() const noexcept;

    // Next output position: integer input index and remainder over q_.
    std::int64_t nextIndex_ = 0;
    std::int64_t nextRemainder_ = 0;
    std::int64_t received_ = 0;   // input samples consumed since reset

    Channel left_;
    Channel right_;
};

} // namespace ak4522::detail
