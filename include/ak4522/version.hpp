// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

#include <string_view>

namespace ak4522 {

// Library version string, for example "0.0.1". Alpha releases make no
// compatibility guarantees.
[[nodiscard]] std::string_view versionString() noexcept;

} // namespace ak4522
