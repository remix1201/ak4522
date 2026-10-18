// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "ak4522/version.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Library reports a version string", "[unit]")
{
    CHECK_FALSE(ak4522::versionString().empty());
}
