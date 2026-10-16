#pragma once

// NominalAnalog analog imperfections at the 8x rate: after the DAC's
// reconstruction sections (REQ-DIST-01, REQ-CHANNEL-01, REQ-ANALOG-02;
// P-96 to P-99), or at the ADC's input (REQ-ADC-07, P-110). Synthetic.
// Internal: not part of the public API.

#include "ak4522/config.hpp"

#include <cstdint>
#include <span>
#include <utility>

namespace ak4522::detail {

// Calibration targets; the defaults are the DAC's typical values.
struct AnalogTargets {
    double sinadDb = 90.0;        // S/(N+D) at toneAmplitude, 1 kHz (D-A01)
    double toneAmplitude = 1.0;   // test level, 1.0 = 0 dBFS
    double isolationDb = 110.0;   // crosstalk at 1 kHz (D-A04)
    double fixedMismatchDb = 0.2; // typical mismatch (D-A05)
    double maxMismatchDb = 0.5;   // maximum mismatch (D-A05)
};

class OutputStage {
public:
    using Targets = AnalogTargets;

    struct Coefficients {
        double gainLeft = 1.0;
        double gainRight = 1.0;
        double quadratic = 0.0; // y = x + a2 x^2 + a3 x^3
        double cubic = 0.0;     // negative: compressive (P-96)
        double crosstalk = 0.0; // coupling coefficient, see P-97
        bool crosstalkDifferentiates = false;
        double offset = 0.0;    // normalized DC offset
    };

    // Distortion polynomial for a shape, calibrated so S/(N+D) reaches the
    // target at the test level, 1 kHz, with the NominalAnalog noise floor (P-96).
    [[nodiscard]] static Coefficients distortion(DistortionShape shape, const Targets& targets = {}) noexcept;

    // Channel gains in dB for a mismatch mode; perInstance draws from the seed (P-98).
    [[nodiscard]] static std::pair<double, double> mismatchDb(MismatchMode mode, std::uint64_t seed,
                                                              const Targets& targets = {}) noexcept;

    // Sets the coefficients for the enabled options at the 8x rate.
    void prepare(const AnalogOptions& options, double sampleRate, const Targets& targets = {}) noexcept;
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
