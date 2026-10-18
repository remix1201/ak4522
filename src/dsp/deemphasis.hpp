// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// Digital de-emphasis at the DAC rate (REQ-DEEM-01 to REQ-DEEM-03, D-E01 to
// D-E03). Coefficients come from tools/design_deemphasis.py and are fitted
// (P-60, P-61). Internal: not part of the public API.

#include "ak4522/config.hpp"

#include <span>

namespace ak4522::detail {

class Deemphasis {
public:
    struct Coefficients {
        double b0, b1, a1; // y = b0 x + b1 x[-1] - a1 y[-1]
    };

    // Coefficients designed for a selection; OFF has none (bypass).
    [[nodiscard]] static Coefficients coefficients(DeemphasisSelect select) noexcept;

    // Clears filter state; keeps the selection.
    void reset() noexcept;

    // Applies a DEM pin selection at the next frame. The selected set runs at
    // any DAC rate, as fixed chip coefficients would (P-62). If the set
    // changes, the new section starts from zero state (P-64); selecting the
    // set already in use changes nothing.
    void select(DeemphasisSelect pins) noexcept;

    // Filters both channels in place. Spans must have equal length.
    void process(std::span<double> left, std::span<double> right) noexcept;

    // The coefficient set in use.
    [[nodiscard]] DeemphasisSelect active() const noexcept { return set_; }

private:
    DeemphasisSelect set_ = DeemphasisSelect::off;
    Coefficients c_{1.0, 0.0, 0.0};
    double stateLeft_ = 0.0;
    double stateRight_ = 0.0;
};

} // namespace ak4522::detail
