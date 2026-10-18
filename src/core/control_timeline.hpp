// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// Applies timestamped pin events on the DAC-frame timeline and splits each
// block into segments of constant control state (REQ-CTRL-01, REQ-RESET-01,
// REQ-RESET-02). Internal: DacCore drives its signal path from the segments.

#include "ak4522/config.hpp"
#include "ak4522/events.hpp"
#include "ak4522/status.hpp"
#include "core/soft_mute.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ak4522::detail {

// A run of frames with no pin change inside it.
struct ControlSegment {
    std::size_t begin = 0;
    std::size_t length = 0;
    bool powerDown = false;         // PD low: output zero relative to VCOM (P-71)
    bool clearState = false;        // first segment after a PD edge: clear filter and history state
    DeemphasisSelect deemphasis = DeemphasisSelect::off; // selection in force
    bool deemphasisChanged = false; // selection changed at begin: reset that section (P-64)
};

class ControlTimeline {
public:
    // Software startup: a PD pulse followed by release with the given pin
    // levels latched (P-70, P-72).
    void reset(DeemphasisSelect demPins, bool smutePin) noexcept;

    // Checks offsets (< frames, non-decreasing) and event types and values.
    [[nodiscard]] static Status validate(std::size_t frames,
                                         std::span<const ControlEvent> events) noexcept;

    // Applies the events and calls visit(const ControlSegment&) for each
    // segment in order. Events on the same frame apply in ControlType order
    // (PD, DEM, SMUTE, P-42); events of the same type on the same frame apply
    // in array order. On a validation failure nothing is applied or visited.
    template <typename Visitor>
    [[nodiscard]] Status process(std::size_t frames, std::span<const ControlEvent> events,
                                 Visitor&& visit) noexcept;

    // The SMUTE ramp; advance it only for frames outside power-down.
    [[nodiscard]] SoftMute& mute() noexcept { return mute_; }

    [[nodiscard]] bool powerDown() const noexcept { return !pdHigh_; }
    [[nodiscard]] DeemphasisSelect deemphasis() const noexcept { return deemphasis_; }
    [[nodiscard]] std::uint64_t ignoredEvents() const noexcept { return ignoredEvents_; }

private:
    void applyEventsAt(std::span<const ControlEvent> sameFrame) noexcept;
    void apply(const ControlEvent& event) noexcept;

    SoftMute mute_;
    bool pdHigh_ = true;
    bool smutePin_ = false;
    DeemphasisSelect demPins_ = DeemphasisSelect::off; // pin levels, tracked during reset
    DeemphasisSelect deemphasis_ = DeemphasisSelect::off; // selection in force
    bool clearPending_ = true;
    bool deemphasisChanged_ = true;
    std::uint64_t ignoredEvents_ = 0;
};

template <typename Visitor>
Status ControlTimeline::process(std::size_t frames, std::span<const ControlEvent> events,
                                Visitor&& visit) noexcept
{
    if (const Status status = validate(frames, events); status != Status::ok) {
        return status;
    }

    std::size_t next = 0;
    std::size_t frame = 0;
    while (frame < frames) {
        std::size_t sameFrameEnd = next;
        while (sameFrameEnd < events.size() && events[sameFrameEnd].offset == frame) {
            ++sameFrameEnd;
        }
        applyEventsAt(events.subspan(next, sameFrameEnd - next));
        next = sameFrameEnd;

        const std::size_t end = next < events.size() ? events[next].offset : frames;
        visit(ControlSegment{frame, end - frame, !pdHigh_, clearPending_, deemphasis_,
                             deemphasisChanged_});
        clearPending_ = false;
        deemphasisChanged_ = false;
        frame = end;
    }
    return Status::ok;
}

} // namespace ak4522::detail
