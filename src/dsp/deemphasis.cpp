#include "dsp/deemphasis.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace ak4522::detail {

namespace {

// b0 b1 a1 for 32, 44.1, and 48 kHz, in that order.
constexpr std::array<double, 9> designed{
#include "dsp/deemphasis_coefficients.inc"
};

constexpr double flushThreshold = 1e-15; // as in the reconstruction sections (P-89)

Deemphasis::Coefficients designedSet(std::size_t index) noexcept
{
    return {designed[3 * index], designed[3 * index + 1], designed[3 * index + 2]};
}

double tick(const Deemphasis::Coefficients& c, double x, double& state) noexcept
{
    const double y = c.b0 * x + state;
    const double next = c.b1 * x - c.a1 * y;
    state = std::abs(next) < flushThreshold ? 0.0 : next;
    return y;
}

} // namespace

Deemphasis::Coefficients Deemphasis::coefficients(DeemphasisSelect select) noexcept
{
    switch (select) {
    case DeemphasisSelect::fs32000: return designedSet(0);
    case DeemphasisSelect::fs44100: return designedSet(1);
    case DeemphasisSelect::fs48000: return designedSet(2);
    case DeemphasisSelect::off: break;
    }
    return {1.0, 0.0, 0.0};
}

void Deemphasis::reset() noexcept
{
    stateLeft_ = 0.0;
    stateRight_ = 0.0;
}

void Deemphasis::select(DeemphasisSelect pins) noexcept
{
    if (pins == set_) {
        return;
    }
    set_ = pins;
    c_ = coefficients(pins);
    reset();
}

void Deemphasis::process(std::span<double> left, std::span<double> right) noexcept
{
    if (set_ == DeemphasisSelect::off) {
        return; // exact bypass (REQ-DEEM-01)
    }
    const std::size_t n = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = tick(c_, left[i], stateLeft_);
        right[i] = tick(c_, right[i], stateRight_);
    }
}

} // namespace ak4522::detail
