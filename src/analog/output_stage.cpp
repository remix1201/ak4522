#include "analog/output_stage.hpp"

#include "analog/analog_noise.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ak4522::detail {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double offsetVolts = 0.003;        // D-A15: "a few mV" (P-99)
constexpr double peakVolts = 1.45;           // I-O01, VREFH = 5 V

std::uint64_t splitmix(std::uint64_t& state) noexcept
{
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double unit(std::uint64_t& state) noexcept
{
    return (static_cast<double>(splitmix(state) >> 11) + 0.5) * 0x1.0p-53; // (0, 1)
}

} // namespace

OutputStage::Coefficients OutputStage::distortion(DistortionShape shape, const Targets& targets) noexcept
{
    // Distortion power relative to the test tone: the S/(N+D) target minus
    // the unweighted noise in 10 Hz to 20 kHz (P-94, P-96).
    const double a = targets.toneAmplitude;
    const double tonePower = a * a / 2.0;
    const double noisePower = AnalogNoise::density() * (20000.0 - 10.0);
    const double distortionPower = std::pow(10.0, -targets.sinadDb / 10.0) - noisePower / tonePower;
    // For x = a sin: 2nd harmonic amplitude |a2| a^2 / 2, 3rd harmonic
    // amplitude |a3| a^3 / 4, here relative to a. The cubic term is
    // negative, compressive like soft saturation.
    Coefficients c;
    switch (shape) {
    case DistortionShape::equal2nd3rd: {
        const double each = std::sqrt(distortionPower / 2.0);
        c.quadratic = 2.0 * each / a;
        c.cubic = -4.0 * each / (a * a);
        break;
    }
    case DistortionShape::mostly3rd:
        c.cubic = -4.0 * std::sqrt(distortionPower) / (a * a);
        break;
    case DistortionShape::mostly2nd:
        c.quadratic = 2.0 * std::sqrt(distortionPower) / a;
        break;
    }
    return c;
}

std::pair<double, double> OutputStage::mismatchDb(MismatchMode mode, std::uint64_t seed, const Targets& targets) noexcept
{
    const double fixedMismatchDb = targets.fixedMismatchDb;
    const double maxMismatchDb = targets.maxMismatchDb;
    if (mode == MismatchMode::fixed) {
        return {fixedMismatchDb / 2.0, -fixedMismatchDb / 2.0};
    }
    // Half-normal total mismatch with the typical as median, capped at the maximum, and
    // a random sign; from a stream separate from the noise (P-98).
    std::uint64_t s = seed ^ 0x4D49534D41544348ull;
    const double sigma = fixedMismatchDb / 0.6744897501960817;
    const double gaussian = std::sqrt(-2.0 * std::log(unit(s))) * std::cos(2.0 * pi * unit(s));
    const double total = std::min(std::abs(gaussian) * sigma, maxMismatchDb);
    const double sign = (splitmix(s) & 1u) != 0 ? 1.0 : -1.0;
    return {sign * total / 2.0, -sign * total / 2.0};
}

void OutputStage::prepare(const AnalogOptions& options, double sampleRate, const Targets& targets) noexcept
{
    c_ = Coefficients{};
    if (options.distortion) {
        const Coefficients d = distortion(options.distortionShape, targets);
        c_.quadratic = d.quadratic;
        c_.cubic = d.cubic;
    }
    if (options.channelMismatch) {
        const auto [l, r] = mismatchDb(options.mismatchMode, options.noiseSeed, targets);
        c_.gainLeft = std::pow(10.0, l / 20.0);
        c_.gainRight = std::pow(10.0, r / 20.0);
    }
    if (options.crosstalk) {
        const double level = std::pow(10.0, -targets.isolationDb / 20.0);
        c_.crosstalkDifferentiates = options.crosstalkShape == CrosstalkShape::capacitive;
        // A first difference has gain 2 sin(pi f / fs); scale it to the level at 1 kHz.
        c_.crosstalk = c_.crosstalkDifferentiates ? level / (2.0 * std::sin(pi * 1000.0 / sampleRate)) : level;
    }
    if (options.dcOffset) {
        c_.offset = offsetVolts / peakVolts;
    }
    active_ = options.distortion || options.channelMismatch || options.crosstalk || options.dcOffset;
    reset();
}

void OutputStage::reset() noexcept
{
    previousLeft_ = 0.0;
    previousRight_ = 0.0;
}

void OutputStage::process(std::span<double> left, std::span<double> right) noexcept
{
    if (!active_) {
        return;
    }
    const std::size_t n = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < n; ++i) {
        double l = left[i] * c_.gainLeft;
        double r = right[i] * c_.gainRight;
        l += l * l * (c_.quadratic + c_.cubic * l);
        r += r * r * (c_.quadratic + c_.cubic * r);
        // Coupling uses both channels before coupling.
        const double fromRight = c_.crosstalkDifferentiates ? r - previousRight_ : r;
        const double fromLeft = c_.crosstalkDifferentiates ? l - previousLeft_ : l;
        previousLeft_ = l;
        previousRight_ = r;
        left[i] = l + c_.crosstalk * fromRight + c_.offset;
        right[i] = r + c_.crosstalk * fromLeft + c_.offset;
    }
}

} // namespace ak4522::detail
