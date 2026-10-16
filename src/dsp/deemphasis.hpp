#pragma once

// Digital de-emphasis at the DAC rate (REQ-DEEM-01 to REQ-DEEM-03, D-E01 to
// D-E03). Coefficients come from tools/design_deemphasis.py and are fitted
// (P-60, P-61). Internal: not part of the public API.

#include "ak4522/config.hpp"

#include <cstddef>
#include <span>

namespace ak4522::detail {

class Deemphasis {
public:
    struct Coefficients {
        double b0, b1, a1; // y = b0 x + b1 x[-1] - a1 y[-1]
    };

    // Coefficients designed for a selection; OFF has none (bypass).
    [[nodiscard]] static Coefficients coefficients(DeemphasisSelect select) noexcept;

    // Crossfade length for DeemphasisSwitching::hostCrossfade: 5 ms rounded
    // to the nearest frame, halves away from zero (P-64).
    [[nodiscard]] static std::size_t crossfadeFrames(double dacRate) noexcept;

    // Sets the rate and policies, clears all state, and selects OFF.
    void prepare(double dacRate, DeemphasisRatePolicy ratePolicy,
                 DeemphasisSwitching switching) noexcept;

    // Clears filter state and ends any crossfade; keeps the selection.
    void reset() noexcept;

    // Applies a DEM pin selection at the next frame. The coefficient set in
    // use follows the rate policy (P-62, P-63). If the set changes, the new
    // section starts from zero state (P-64); with hostCrossfade and
    // allowCrossfade, the old section keeps running and the output fades
    // linearly to the new one over crossfadeFrames().
    void select(DeemphasisSelect pins, bool allowCrossfade) noexcept;

    // Filters both channels in place. Spans must have equal length.
    void process(std::span<double> left, std::span<double> right) noexcept;

    // The coefficient set in use: off, or the nominal rate of the set.
    [[nodiscard]] DeemphasisSelect active() const noexcept { return current_.set; }
    [[nodiscard]] bool crossfading() const noexcept { return fadeRemaining_ > 0; }

private:
    struct Section {
        DeemphasisSelect set = DeemphasisSelect::off;
        Coefficients c{1.0, 0.0, 0.0};
        double stateLeft = 0.0;
        double stateRight = 0.0;

        void clear() noexcept { stateLeft = stateRight = 0.0; }
        [[nodiscard]] double tick(double x, double& state) const noexcept;
    };

    [[nodiscard]] DeemphasisSelect effectiveSet(DeemphasisSelect pins) const noexcept;
    [[nodiscard]] static Section makeSection(DeemphasisSelect set) noexcept;

    double dacRate_ = 44100.0;
    DeemphasisRatePolicy ratePolicy_ = DeemphasisRatePolicy::pinFaithful;
    DeemphasisSwitching switching_ = DeemphasisSwitching::resetSection;
    Section current_;
    Section previous_;
    std::size_t fadeLength_ = 0;
    std::size_t fadeRemaining_ = 0;
};

} // namespace ak4522::detail
