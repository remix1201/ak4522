// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "core/control_timeline.hpp"

namespace ak4522::detail {

void ControlTimeline::reset(DeemphasisSelect demPins, bool smutePin) noexcept
{
    pdHigh_ = true;
    smutePin_ = smutePin;
    demPins_ = demPins;
    deemphasis_ = demPins;
    mute_.reset(smutePin);
    clearPending_ = true;
    deemphasisChanged_ = true;
}

Status ControlTimeline::validate(std::size_t frames, std::span<const ControlEvent> events) noexcept
{
    std::size_t previous = 0;
    for (const ControlEvent& event : events) {
        if (event.offset >= frames) {
            return Status::eventOutOfRange;
        }
        if (event.offset < previous) {
            return Status::eventsNotSorted;
        }
        previous = event.offset;

        const bool valid = event.type == ControlType::deemphasis
            ? event.value <= static_cast<std::uint8_t>(DeemphasisSelect::fs32000)
            : (event.type == ControlType::powerDown || event.type == ControlType::softMute)
                && event.value <= 1;
        if (!valid) {
            return Status::invalidConfiguration;
        }
    }
    return Status::ok;
}

void ControlTimeline::applyEventsAt(std::span<const ControlEvent> sameFrame) noexcept
{
    // Priority order regardless of array order (P-42); stable within a type.
    for (const ControlType type : {ControlType::powerDown, ControlType::deemphasis,
                                   ControlType::softMute}) {
        for (const ControlEvent& event : sameFrame) {
            if (event.type == type) {
                apply(event);
            }
        }
    }
}

void ControlTimeline::apply(const ControlEvent& event) noexcept
{
    switch (event.type) {
    case ControlType::powerDown: {
        const bool high = event.value != 0;
        if (high == pdHigh_) {
            ++ignoredEvents_;
            return;
        }
        pdHigh_ = high;
        clearPending_ = true;
        if (high) {
            // Release: latch the current pin levels (P-72). No initialization
            // delay applies to the DAC (D-R05).
            mute_.reset(smutePin_);
            deemphasis_ = demPins_;
            deemphasisChanged_ = true;
        }
        return;
    }
    case ControlType::deemphasis: {
        const auto select = static_cast<DeemphasisSelect>(event.value);
        if (select == demPins_) {
            ++ignoredEvents_;
            return;
        }
        demPins_ = select;
        if (pdHigh_) {
            deemphasis_ = select;
            deemphasisChanged_ = true;
        }
        return;
    }
    case ControlType::softMute: {
        const bool high = event.value != 0;
        if (high == smutePin_) {
            ++ignoredEvents_;
            return;
        }
        // The ramp tracks the pin even during reset; it does not advance
        // there, and release re-latches it (P-72).
        smutePin_ = high;
        static_cast<void>(mute_.setPin(high));
        return;
    }
    }
}

} // namespace ak4522::detail
