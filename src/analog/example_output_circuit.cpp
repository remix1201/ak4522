#include "ak4522/output_circuit.hpp"

#include <algorithm>
#include <cmath>

namespace ak4522 {

namespace {

constexpr double pi = 3.14159265358979323846;

// Decaying states below this are set to zero to avoid subnormal arithmetic;
// it is far below one 20-bit step (as in the reconstruction sections).
constexpr double flushThreshold = 1e-15;

double flushed(double v) noexcept
{
    return std::abs(v) < flushThreshold ? 0.0 : v;
}

} // namespace

// Bilinear transform of s / (s + wc) with the corner prewarped (P-104).
void ExampleOutputCircuit::HighPass::design(double cornerHz, double sampleRate) noexcept
{
    const double k = std::tan(pi * cornerHz / sampleRate);
    b0 = 1.0 / (1.0 + k);
    a1 = (k - 1.0) / (k + 1.0);
}

Status ExampleOutputCircuit::prepare(double sampleRate, const ExampleCircuitOptions& options)
{
    if (!(sampleRate > 0.0) || !std::isfinite(sampleRate) || !(options.loadOhms >= 0.0)
        || !std::isfinite(options.loadOhms)) {
        return Status::invalidConfiguration;
    }
    // 27 uF sees the 10 kohm resistor into the virtual ground.
    inputCornerHz_ = 1.0 / (2.0 * pi * inputResistance * inputCapacitance);
    // 10 uF sees 27 kohm, in parallel with any external load.
    const double load = options.loadOhms > 0.0
        ? outputResistance * options.loadOhms / (outputResistance + options.loadOhms)
        : outputResistance;
    outputCornerHz_ = 1.0 / (2.0 * pi * load * outputCapacitance);
    if (!(outputCornerHz_ < 0.45 * sampleRate)) {
        return Status::invalidConfiguration; // a load so low the corner leaves the band
    }
    input_.design(inputCornerHz_, sampleRate);
    output_.design(outputCornerHz_, sampleRate);
    const double magnitude = options.includeGain ? feedbackResistance / inputResistance : 1.0;
    gain_ = options.invert ? -magnitude : magnitude;
    reset();
    return Status::ok;
}

void ExampleOutputCircuit::reset() noexcept
{
    left_ = {};
    right_ = {};
}

double ExampleOutputCircuit::step(State& s, double x) const noexcept
{
    const double y = flushed(input_.b0 * (x - s.x1) - input_.a1 * s.y1);
    s.x1 = x;
    s.y1 = y;
    const double u = gain_ * y;
    const double v = flushed(output_.b0 * (u - s.u1) - output_.a1 * s.v1);
    s.u1 = u;
    s.v1 = v;
    return v;
}

void ExampleOutputCircuit::processFrame(double& left, double& right) noexcept
{
    left = step(left_, left);
    right = step(right_, right);
}

void ExampleOutputCircuit::process(StereoView<double> audio) noexcept
{
    const std::size_t n = std::min(audio.left.size(), audio.right.size());
    for (std::size_t i = 0; i < n; ++i) {
        processFrame(audio.left[i], audio.right[i]);
    }
}

void ExampleOutputCircuit::process(StereoView<float> audio) noexcept
{
    const std::size_t n = std::min(audio.left.size(), audio.right.size());
    for (std::size_t i = 0; i < n; ++i) {
        double l = static_cast<double>(audio.left[i]);
        double r = static_cast<double>(audio.right[i]);
        processFrame(l, r);
        audio.left[i] = static_cast<float>(l);
        audio.right[i] = static_cast<float>(r);
    }
}

} // namespace ak4522
