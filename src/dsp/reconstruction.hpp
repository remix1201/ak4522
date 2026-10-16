#pragma once

// Pipeline delay and analog reconstruction model at the 8x rate
// (REQ-FILTER-02, REQ-LAT-02, D-03, D-F06). Coefficients come from
// tools/design_reconstruction.py and are fitted (P-87, P-89). Internal: not
// part of the public API.

#include <array>
#include <cstddef>
#include <span>

namespace ak4522::detail {

// Fixed delay standing in for undocumented digital stages between the 8x
// interpolator and the analog filters (P-87).
class PipelineDelay {
public:
    static constexpr std::size_t samples = 21; // 2.625 DAC frames at 8x

    void reset() noexcept;
    // Delays both channels in place. Spans must have equal length.
    void process(std::span<double> left, std::span<double> right) noexcept;

private:
    struct Line {
        std::array<double, samples> buffer{};
        std::size_t position = 0;
        double tick(double x) noexcept;
    };
    Line left_;
    Line right_;
};

// Two second-order sections modeling the switched-capacitor and
// continuous-time filters together (D-03, P-89).
class Reconstruction {
public:
    static constexpr std::size_t sections = 2;

    struct Section {
        double b0, b1, b2, a1, a2;
    };
    [[nodiscard]] static std::span<const Section, sections> coefficients() noexcept;

    void reset() noexcept;
    // Filters both channels in place. Spans must have equal length.
    void process(std::span<double> left, std::span<double> right) noexcept;

private:
    struct Channel {
        // Transposed direct form II state per section.
        std::array<std::array<double, 2>, sections> state{};
        double tick(double x) noexcept;
    };
    Channel left_;
    Channel right_;
};

} // namespace ak4522::detail
