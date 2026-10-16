#include "core/config_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ak4522::detail {

namespace {

// Supported host rates (P-32).
constexpr std::array<double, 6> supportedHostRates{44100.0, 48000.0, 88200.0,
                                                   96000.0, 176400.0, 192000.0};

template <typename Enum>
bool enumAtMost(Enum value, Enum last) noexcept
{
    return static_cast<std::uint8_t>(value) <= static_cast<std::uint8_t>(last);
}

bool enumsValid(const DacConfig& config) noexcept
{
    return enumAtMost(config.cmode, CmodePin::open)
        && enumAtMost(config.profile, ModelProfile::measured)
        && enumAtMost(config.deemphasisSwitching, DeemphasisSwitching::hostCrossfade)
        && enumAtMost(config.initialDeemphasis, DeemphasisSelect::fs32000);
}

} // namespace

double derivedMasterClockHz(CmodePin cmode, double dacRate) noexcept
{
    return static_cast<double>(masterClockRatio(cmode)) * dacRate;
}

Status validate(const DacConfig& config) noexcept
{
    // Written so that NaN fails the range test.
    if (!(config.dacRate >= minDacRate && config.dacRate <= maxDacRate)) {
        return Status::invalidDacRate;
    }
    if (!enumsValid(config)) {
        return Status::invalidConfiguration;
    }
    if (config.masterClockHz) {
        // MCLK must equal ratio x fs (D-C05, P-35). With fs already inside
        // 16 to 48 kHz, that also keeps MCLK inside the datasheet range for
        // each ratio (D-C02 to D-C04, I-C02), so no separate range check exists.
        const double mclk = *config.masterClockHz;
        const double expected = derivedMasterClockHz(config.cmode, config.dacRate);
        if (!std::isfinite(mclk) || std::abs(mclk - expected) > masterClockTolerance * expected) {
            return Status::invalidMasterClock;
        }
    }
    if (config.profile == ModelProfile::measured) {
        return Status::invalidConfiguration; // no measured profile exists yet (O-03)
    }
    return Status::ok;
}

Status validate(const HostConfig& config) noexcept
{
    if (std::find(supportedHostRates.begin(), supportedHostRates.end(), config.hostRate)
        == supportedHostRates.end()) {
        return Status::unsupportedHostRate;
    }
    if (config.maxBlockFrames == 0) {
        return Status::invalidBlockSize;
    }
    if (config.maxPendingEvents == 0
        || !enumAtMost(config.inputResolution, InputResolution::bits16)) {
        return Status::invalidConfiguration;
    }
    return validate(config.dac);
}

} // namespace ak4522::detail
