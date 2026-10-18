// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// NominalAnalog output noise (REQ-NOISE-01, REQ-NOISE-02, P-94). Synthetic:
// white Gaussian noise at a fixed physical density, calibrated to the
// datasheet's 100 dB A-weighted S/N. Internal: not part of the public API.

#include <cstdint>
#include <span>

namespace ak4522::detail {

class AnalogNoise {
public:
    // One-sided density in full-scale units squared per hertz.
    [[nodiscard]] static double density() noexcept;

    // Seeds independent generators for the two channels and sets the level
    // for the given sample rate (the 8x rate). Real-time safe.
    void prepare(std::uint64_t seed, double sampleRate) noexcept;

    // Adds noise to both channels in place. The generators are not reset by
    // the host or by PD (P-74), only by prepare.
    void add(std::span<double> left, std::span<double> right) noexcept;

    [[nodiscard]] double sigma() const noexcept { return sigma_; }

private:
    struct Generator {
        std::uint64_t state = 0;
        double spare = 0.0;
        bool hasSpare = false;
        [[nodiscard]] double gaussian() noexcept;
    };

    double sigma_ = 0.0;
    Generator left_;
    Generator right_;
};

} // namespace ak4522::detail
