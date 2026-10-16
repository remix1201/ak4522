#include "core/soft_mute.hpp"

#include <cstddef>

namespace ak4522::detail {

namespace {

constexpr double gainForPosition(std::int32_t position) noexcept
{
    return static_cast<double>(position) / static_cast<double>(SoftMute::rampLength);
}

} // namespace

void SoftMute::reset(bool pinHigh) noexcept
{
    pinHigh_ = pinHigh;
    position_ = target();
}

bool SoftMute::setPin(bool pinHigh) noexcept
{
    if (pinHigh == pinHigh_) {
        return false;
    }
    pinHigh_ = pinHigh;
    return true;
}

double SoftMute::nextGain() noexcept
{
    const std::int32_t goal = target();
    if (position_ < goal) {
        ++position_;
    } else if (position_ > goal) {
        --position_;
    }
    return gainForPosition(position_);
}

void SoftMute::process(std::span<double> left, std::span<double> right) noexcept
{
    const std::size_t frames = left.size() < right.size() ? left.size() : right.size();

    // Settled at unity: nothing to do. Settled at zero: the ramp holds, but the
    // samples are still written so muted output is exactly zero (P-54 keeps
    // downstream processing and noise running; that happens after this stage).
    if (settled() && position_ == rampLength) {
        return;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        const double gain = nextGain();
        left[i] *= gain;
        right[i] *= gain;
    }
}

} // namespace ak4522::detail
