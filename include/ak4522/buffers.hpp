// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

#include <cstddef>
#include <span>

namespace ak4522 {

// Non-owning planar stereo view. Both channels must have the same length,
// which is the frame count. Each channel is contiguous (stride 1).
//
// Aliasing: an input and output view may refer to exactly the same memory
// (in-place processing). Partial overlap between input and output, or between
// the two channels of one view, is not supported.
template <typename Sample>
struct StereoView {
    std::span<Sample> left;
    std::span<Sample> right;

    [[nodiscard]] constexpr std::size_t frames() const noexcept { return left.size(); }
    [[nodiscard]] constexpr bool consistent() const noexcept { return left.size() == right.size(); }
};

} // namespace ak4522
