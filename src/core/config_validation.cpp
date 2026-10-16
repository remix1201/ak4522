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
        && enumAtMost(config.initialDeemphasis, DeemphasisSelect::fs32000)
        && enumAtMost(config.analog.distortionShape, DistortionShape::mostly2nd)
        && enumAtMost(config.analog.crosstalkShape, CrosstalkShape::flat)
        && enumAtMost(config.analog.mismatchMode, MismatchMode::perInstance);
}

} // namespace

double derivedMasterClockHz(CmodePin cmode, double dacRate) noexcept
{
    return static_cast<double>(masterClockRatio(cmode)) * dacRate;
}

Status validate(const DacConfig& config) noexcept
{
    // Written so that NaN fails the range test. The extension is upper side
    // only (P-103).
    const double upper = config.allowExtendedRate ? extendedMaxDacRate : maxDacRate;
    if (!(config.dacRate >= minDacRate && config.dacRate <= upper)) {
        return Status::invalidDacRate;
    }
    if (!enumsValid(config)) {
        return Status::invalidConfiguration;
    }
    if (config.masterClockHz) {
        // MCLK must equal ratio x fs (D-C05, P-35). With fs inside 16 to
        // 48 kHz, that also keeps MCLK inside the datasheet range for each
        // ratio (D-C02 to D-C04, I-C02), so no separate range check exists.
        // In the extended range MCLK exceeds it in proportion (P-103).
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

bool hostRateSupported(double rate) noexcept
{
    return std::find(supportedHostRates.begin(), supportedHostRates.end(), rate) != supportedHostRates.end();
}

Status validate(const HostConfig& config) noexcept
{
    if (!hostRateSupported(config.hostRate)) {
        return Status::unsupportedHostRate;
    }
    if (config.maxBlockFrames == 0) {
        return Status::invalidBlockSize;
    }
    if (config.maxPendingEvents == 0
        || !enumAtMost(config.inputResolution, InputResolution::bits16)
        || !enumAtMost(config.path, HostPath::adc)
        || !enumAtMost(config.adcInputMode, AdcInputMode::singleEnded)) {
        return Status::invalidConfiguration;
    }
    return validate(config.dac);
}

Status validate(const DacStreamConfig& config) noexcept
{
    if (!hostRateSupported(config.hostRate)) {
        return Status::unsupportedHostRate;
    }
    if (config.maxBlockFrames == 0) {
        return Status::invalidBlockSize;
    }
    const ExactRate rate = config.dacRate;
    if (rate.numerator == 0 || rate.denominator == 0 || rate.denominator > maxRateDenominator) {
        return Status::invalidDacRate;
    }
    if (!enumAtMost(config.format, DacSampleFormat::float64) || !enumAtMost(config.mode, DacStreamMode::push)) {
        return Status::invalidConfiguration;
    }
    if (config.mode == DacStreamMode::pull && (config.pull == nullptr || config.maxEventsPerBlock == 0)) {
        return Status::invalidConfiguration;
    }
    if (config.mode == DacStreamMode::push && (config.pushCapacityFrames == 0 || config.pushCapacityEvents == 0)) {
        return Status::invalidConfiguration;
    }
    DacConfig dac = config.dac;
    dac.dacRate = rate.hz();
    return validate(dac);
}

Status validate(const AdcConfig& config) noexcept
{
    DacConfig clocks; // the rate and clock rules are shared with the DAC
    clocks.dacRate = config.adcRate;
    clocks.allowExtendedRate = config.allowExtendedRate;
    clocks.cmode = config.cmode;
    clocks.masterClockHz = config.masterClockHz;
    if (const Status s = validate(clocks); s != Status::ok) {
        return s;
    }
    if (static_cast<std::uint8_t>(config.inputMode) > static_cast<std::uint8_t>(AdcInputMode::singleEnded)) {
        return Status::invalidConfiguration;
    }
    // The analog enums are checked through the DAC rules.
    DacConfig analog;
    analog.profile = config.profile;
    analog.analog = config.analog;
    if (const Status s = validate(analog); s != Status::ok) {
        return s; // includes the measured profile, which does not exist yet
    }
    const AnalogOptions& o = config.analog;
    const bool any = o.noise || o.distortion || o.channelMismatch || o.crosstalk || o.dcOffset;
    if (o.dcOffset || (config.profile == ModelProfile::specification && any)) {
        return Status::invalidConfiguration; // P-110; Specification has no imperfections (REQ-SCOPE-03)
    }
    return Status::ok;
}

} // namespace ak4522::detail
