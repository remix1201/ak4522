#pragma once

// NominalAnalog output-stage imperfections at the 8x rate, after the
// reconstruction sections (REQ-DIST-01, REQ-CHANNEL-01, REQ-ANALOG-02;
// P-96 to P-99). Synthetic. Internal: not part of the public API.

#include "ak4522/config.hpp"

#include <cstdint>
#include <span>
#include <utility>

namespace ak4522::detail {

class OutputStage {
public:
    struct Coefficients {
        double gainLeft = 1.0;
        double gainRight = 1.0;
        double quadratic = 0.0; // y = x + a2 x^2 + a3 x^3
        double cubic = 0.0;     // negative: compressive (P-96)
        double crosstalk = 0.0; // coupling coefficient, see P-97
        bool crosstalkDifferentiates = false;
        double offset = 0.0;    // normalized DC offset
    };

    // Distortion polynomial for a shape, calibrated so S/(N+D) is 90 dB at
    // 0 dBFS, 1 kHz with the NominalAnalog noise floor (P-96).
    [[nodiscard]] static Coefficients distortion(DistortionShape shape) noexcept;

    // Channel gains in dB for a mismatch mode; perInstance draws from the seed (P-98).
    [[nodiscard]] static std::pair<double, double> mismatchDb(MismatchMode mode, std::uint64_t seed) noexcept;

    // Sets the coefficients for the enabled options at the 8x rate.
    void prepare(const AnalogOptions& options, double sampleRate) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const Coefficients& coefficients() const noexcept { return c_; }

    // Processes both channels in place.
    void process(std::span<double> left, std::span<double> right) noexcept;

private:
    Coefficients c_{};
    bool active_ = false;
    double previousLeft_ = 0.0;  // for the capacitive coupling difference
    double previousRight_ = 0.0;
};

} // namespace ak4522::detail
