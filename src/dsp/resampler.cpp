#include "dsp/resampler.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ak4522::detail {

namespace {

constexpr double pi = 3.14159265358979323846;

// Rates are represented exactly in micro-hertz (P-37).
std::int64_t microhertz(double hz) noexcept
{
    return static_cast<std::int64_t>(std::llround(hz * 1e6));
}

// Exact-row mode is used while the table stays within this many coefficients.
constexpr std::size_t maxExactCoefficients = std::size_t{1} << 22;

double besselI0(double x) noexcept
{
    double sum = 1.0;
    double term = 1.0;
    const double quarter = x * x / 4.0;
    for (int k = 1; k < 200; ++k) {
        term *= quarter / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < sum * 1e-17) {
            break;
        }
    }
    return sum;
}

double kaiserBeta(double attenuationDb) noexcept
{
    if (attenuationDb > 50.0) {
        return 0.1102 * (attenuationDb - 8.7);
    }
    if (attenuationDb >= 21.0) {
        return 0.5842 * std::pow(attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
    }
    return 0.0;
}

// Windowed sinc at offset d input samples from the output time.
double kernel(double d, double cutoffCycles, double halfWidth, double beta, double i0Beta) noexcept
{
    const double x = d / halfWidth;
    if (x <= -1.0 || x >= 1.0) {
        return 0.0;
    }
    const double arg = 2.0 * cutoffCycles * d;
    const double sinc = arg == 0.0 ? 1.0 : std::sin(pi * arg) / (pi * arg);
    return 2.0 * cutoffCycles * sinc * besselI0(beta * std::sqrt(1.0 - x * x)) / i0Beta;
}

} // namespace

void Resampler::Channel::push(double x, std::size_t taps) noexcept
{
    // Window order: oldest first. head points at the oldest of the last
    // `taps` samples; the mirror keeps the window contiguous.
    ring[head] = x;
    ring[head + taps] = x;
    head = head + 1 == taps ? 0 : head + 1;
}

Status Resampler::prepare(const ResamplerSpec& spec)
{
    if (!(spec.inputRate > 0.0) || !(spec.outputRate > 0.0) || !std::isfinite(spec.inputRate)
        || !std::isfinite(spec.outputRate) || !(spec.attenuationDb > 0.0)
        || !(spec.startOffset >= 0.0) || !std::isfinite(spec.startOffset)) {
        return Status::invalidConfiguration;
    }
    const double lower = std::min(spec.inputRate, spec.outputRate);
    passbandHz_ = spec.passbandHz > 0.0 ? spec.passbandHz : 20.0 / 44.1 * lower;
    stopbandHz_ = spec.stopbandHz > 0.0 ? spec.stopbandHz : lower - passbandHz_;
    if (!(passbandHz_ < stopbandHz_) || stopbandHz_ > lower) {
        return Status::invalidConfiguration;
    }

    const std::int64_t in = microhertz(spec.inputRate);
    const std::int64_t out = microhertz(spec.outputRate);
    const std::int64_t g = std::gcd(in, out);
    p_ = in / g;
    q_ = out / g;
    pWhole_ = p_ / q_;
    pRemainder_ = p_ % q_;

    // Kaiser length estimate in input samples (P-38).
    const double transition = (stopbandHz_ - passbandHz_) / spec.inputRate;
    const double length = (spec.attenuationDb - 7.95) / (2.285 * 2.0 * pi * transition);
    halfWidth_ = static_cast<std::size_t>(std::ceil(length / 2.0)) + 1;
    taps_ = 2 * halfWidth_;

    const double cutoff = 0.5 * (passbandHz_ + stopbandHz_) / spec.inputRate;
    const double beta = kaiserBeta(spec.attenuationDb);
    const double i0Beta = besselI0(beta);
    const double w = static_cast<double>(halfWidth_);

    offsetWhole_ = static_cast<std::int64_t>(std::floor(spec.startOffset));
    offsetFraction_ = spec.startOffset - static_cast<double>(offsetWhole_);

    exact_ = static_cast<std::size_t>(q_) * taps_ <= maxExactCoefficients;
    const std::size_t rows = exact_ ? static_cast<std::size_t>(q_) : interpolatedPhases + 1;
    rows_.assign(rows * taps_, 0.0);
    rowCarry_.assign(exact_ ? rows : 0, 0);
    for (std::size_t r = 0; r < rows; ++r) {
        double frac = exact_ ? static_cast<double>(r) / static_cast<double>(q_) + offsetFraction_
                             : static_cast<double>(r) / static_cast<double>(interpolatedPhases);
        if (exact_ && frac >= 1.0) {
            frac -= 1.0;
            rowCarry_[r] = 1;
        }
        double* row = rows_.data() + r * taps_;
        double sum = 0.0;
        for (std::size_t j = 0; j < taps_; ++j) {
            // Tap j multiplies input index (i - W + 1 + j); offset from the
            // output time i + frac is frac + W - 1 - j.
            row[j] = kernel(frac + w - 1.0 - static_cast<double>(j), cutoff, w, beta, i0Beta);
            sum += row[j];
        }
        for (std::size_t j = 0; j < taps_; ++j) {
            row[j] /= sum; // exact unity DC gain in every phase (P-38)
        }
    }
    blended_.assign(taps_, 0.0);
    left_.ring.assign(2 * taps_, 0.0);
    right_.ring.assign(2 * taps_, 0.0);
    reset();
    return Status::ok;
}

void Resampler::reset() noexcept
{
    std::fill(left_.ring.begin(), left_.ring.end(), 0.0);
    std::fill(right_.ring.begin(), right_.ring.end(), 0.0);
    left_.head = 0;
    right_.head = 0;
    nextIndex_ = offsetWhole_;
    nextRemainder_ = 0;
    received_ = 0;
}

std::int64_t Resampler::effectiveIndex() const noexcept
{
    if (exact_) {
        return nextIndex_ + rowCarry_[static_cast<std::size_t>(nextRemainder_)];
    }
    const double phase = static_cast<double>(nextRemainder_) / static_cast<double>(q_) + offsetFraction_;
    return nextIndex_ + (phase >= 1.0 ? 1 : 0);
}

std::size_t Resampler::maxOutputsFor(std::size_t inputFrames) const noexcept
{
    // Outputs advance by p_/q_ input samples each.
    const auto n = static_cast<std::int64_t>(inputFrames);
    return static_cast<std::size_t>((n * q_ + p_ - 1) / p_ + 1);
}

double Resampler::dot(const double* row, const double* x) const noexcept
{
    double sum = 0.0;
    for (std::size_t j = 0; j < taps_; ++j) {
        sum += row[j] * x[j];
    }
    return sum;
}

double Resampler::output(const Channel& channel) const noexcept
{
    if (exact_) {
        return dot(rows_.data() + static_cast<std::size_t>(nextRemainder_) * taps_, channel.window());
    }
    return dot(blended_.data(), channel.window());
}

std::size_t Resampler::process(std::span<const double> inLeft, std::span<const double> inRight,
                               std::span<double> outLeft, std::span<double> outRight) noexcept
{
    const std::size_t frames = std::min(inLeft.size(), inRight.size());
    const std::size_t capacity = std::min(outLeft.size(), outRight.size());
    const auto w = static_cast<std::int64_t>(halfWidth_);
    std::size_t written = 0;

    for (std::size_t n = 0; n < frames; ++n) {
        left_.push(inLeft[n], taps_);
        right_.push(inRight[n], taps_);
        ++received_;
        // Output at index i is ready once input i + W has arrived.
        while (effectiveIndex() + w + 1 <= received_ && written < capacity) {
            if (!exact_) {
                double fraction = static_cast<double>(nextRemainder_) / static_cast<double>(q_)
                    + offsetFraction_;
                if (fraction >= 1.0) {
                    fraction -= 1.0;
                }
                const double phase = fraction * static_cast<double>(interpolatedPhases);
                const auto lower = static_cast<std::size_t>(phase);
                const double alpha = phase - static_cast<double>(lower);
                const double* a = rows_.data() + lower * taps_;
                const double* b = a + taps_;
                for (std::size_t j = 0; j < taps_; ++j) {
                    blended_[j] = a[j] + alpha * (b[j] - a[j]);
                }
            }
            outLeft[written] = output(left_);
            outRight[written] = output(right_);
            ++written;
            nextIndex_ += pWhole_;
            nextRemainder_ += pRemainder_;
            if (nextRemainder_ >= q_) {
                nextRemainder_ -= q_;
                ++nextIndex_;
            }
        }
    }
    return written;
}

} // namespace ak4522::detail
