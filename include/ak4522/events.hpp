// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

#include "ak4522/config.hpp"

#include <cstdint>

namespace ak4522 {

// Control pin changes. Enumerator order is the order in which events on the
// same frame are applied, regardless of their order in the array (P-42).
enum class ControlType : std::uint8_t {
    powerDown,  // PD pin; value 0 = low (reset asserted), 1 = high (released)
    deemphasis, // DEM1:DEM0; value is a DeemphasisSelect
    softMute,   // SMUTE pin; value 0 = low, 1 = high
};

// A timestamped pin change. The offset unit depends on the receiver: DAC
// frames for DacCore, host frames for HostProcessor. Offsets are relative to
// the start of the block, must be less than the block length, and must not
// decrease. Repeated events at the same level are accepted and ignored (P-53).
struct ControlEvent {
    std::uint32_t offset = 0;
    ControlType type = ControlType::softMute;
    std::uint8_t value = 0;

    [[nodiscard]] static constexpr ControlEvent powerDown(std::uint32_t offset, bool pinHigh) noexcept
    {
        return {offset, ControlType::powerDown, static_cast<std::uint8_t>(pinHigh ? 1 : 0)};
    }

    [[nodiscard]] static constexpr ControlEvent deemphasis(std::uint32_t offset, DeemphasisSelect select) noexcept
    {
        return {offset, ControlType::deemphasis, static_cast<std::uint8_t>(select)};
    }

    [[nodiscard]] static constexpr ControlEvent softMute(std::uint32_t offset, bool pinHigh) noexcept
    {
        return {offset, ControlType::softMute, static_cast<std::uint8_t>(pinHigh ? 1 : 0)};
    }

    friend constexpr bool operator==(const ControlEvent&, const ControlEvent&) = default;
};

} // namespace ak4522
