#pragma once

// Analog output scaling and profile descriptions (REQ-ANALOG-01, M4 profile
// metadata).

#include "ak4522/config.hpp"

#include <string_view>

namespace ak4522 {

// Nominal output voltage for a normalized DAC sample x (full scale 1.0):
// VCOM + 0.29 * VREFH * x, with VCOM = VREFH / 2 when VREFH is tied to VA.
// At VREFH = 5 V: 2.5 V +/- 1.45 V, 2.9 Vpp (D-O01, I-O01, P-04). A
// diagnostic conversion; host audio stays normalized and zero-centered.
[[nodiscard]] constexpr double outputVolts(double x, double vrefh = 5.0) noexcept
{
    return 0.5 * vrefh + 0.29 * vrefh * x;
}

struct ProfileInfo {
    std::string_view name;
    bool synthetic;          // not derived from measurements of a real device
    bool available;          // implemented in this build
    std::string_view summary;
};

[[nodiscard]] constexpr ProfileInfo describe(ModelProfile profile) noexcept
{
    switch (profile) {
    case ModelProfile::specification:
        return {"Specification", false, true,
                "Fitted linear response meeting the datasheet filter table; no noise or "
                "imperfections."};
    case ModelProfile::nominalAnalog:
        return {"NominalAnalog", true, true,
                "Specification plus optional noise, distortion, mismatch, crosstalk, and DC "
                "offset calibrated to datasheet typical values; synthetic."};
    case ModelProfile::measured:
        return {"Measured", false, false,
                "Fitted to captures of an identified device; requires hardware data (M6)."};
    }
    return {"", false, false, ""};
}

} // namespace ak4522
