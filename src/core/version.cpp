// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "ak4522/version.hpp"

namespace ak4522 {

std::string_view versionString() noexcept
{
    return AK4522_VERSION_STRING;
}

} // namespace ak4522
