#pragma once

// The ADC's digital high-pass filter (REQ-ADC-02, AD-06): first order, -3 dB
// at 0.9 Hz at 44.1 kHz, scaling with fs (I-AD03, P-107). Bilinear transform
// with the corner prewarped. After the decimator (AD-13). Internal.

#include <cstddef>
#include <span>

namespace ak4522::detail {

class AdcHighPass {
public:
    static constexpr double cornerAt44100 = 0.9; // Hz (AD-06)

    void prepare(double adcRate) noexcept;
    void reset() noexcept;
    void process(std::span<double> left, std::span<double> right) noexcept;

    [[nodiscard]] double cornerHz() const noexcept { return corner_; }

private:
    double corner_ = cornerAt44100;
    double b0_ = 1.0; // y = b0 (x - x1) - a1 y1
    double a1_ = 0.0;
    double xl_ = 0.0, yl_ = 0.0, xr_ = 0.0, yr_ = 0.0;
};

} // namespace ak4522::detail
