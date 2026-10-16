#include "dsp/deemphasis.hpp"

#include <array>
#include <cmath>

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

std::size_t Deemphasis::crossfadeFrames(double dacRate) noexcept
{
    return static_cast<std::size_t>(std::lround(0.005 * dacRate));
}

double Deemphasis::Section::tick(double x, double& state) const noexcept
{
    if (set == DeemphasisSelect::off) {
        return x; // exact bypass (REQ-DEEM-01)
    }
    const double y = c.b0 * x + state;
    const double next = c.b1 * x - c.a1 * y;
    state = std::abs(next) < flushThreshold ? 0.0 : next;
    return y;
}

Deemphasis::Section Deemphasis::makeSection(DeemphasisSelect set) noexcept
{
    Section section;
    section.set = set;
    section.c = coefficients(set);
    return section;
}

void Deemphasis::prepare(double dacRate, DeemphasisSwitching switching) noexcept
{
    switching_ = switching;
    fadeLength_ = crossfadeFrames(dacRate);
    current_ = makeSection(DeemphasisSelect::off);
    previous_ = current_;
    fadeRemaining_ = 0;
}

void Deemphasis::reset() noexcept
{
    current_.clear();
    previous_.clear();
    fadeRemaining_ = 0;
}

void Deemphasis::select(DeemphasisSelect pins, bool allowCrossfade) noexcept
{
    const DeemphasisSelect set = pins; // P-62
    if (set == current_.set) {
        return;
    }
    const bool fade = allowCrossfade && switching_ == DeemphasisSwitching::hostCrossfade
        && fadeLength_ > 0;
    if (fade) {
        // A change during a crossfade restarts the fade from the current
        // section; the section being faded out is dropped.
        previous_ = current_;
        fadeRemaining_ = fadeLength_;
    } else {
        fadeRemaining_ = 0;
    }
    current_ = makeSection(set);
}

void Deemphasis::process(std::span<double> left, std::span<double> right) noexcept
{
    const std::size_t n = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t i = 0; i < n; ++i) {
        const double xl = left[i];
        const double xr = right[i];
        double yl = current_.tick(xl, current_.stateLeft);
        double yr = current_.tick(xr, current_.stateRight);
        if (fadeRemaining_ > 0) {
            const double oldL = previous_.tick(xl, previous_.stateLeft);
            const double oldR = previous_.tick(xr, previous_.stateRight);
            // Gain of the new section after this frame: 1/N, 2/N, ..., 1.
            const double g = static_cast<double>(fadeLength_ - fadeRemaining_ + 1)
                / static_cast<double>(fadeLength_);
            yl = g * yl + (1.0 - g) * oldL;
            yr = g * yr + (1.0 - g) * oldR;
            --fadeRemaining_;
        }
        left[i] = yl;
        right[i] = yr;
    }
}

} // namespace ak4522::detail
