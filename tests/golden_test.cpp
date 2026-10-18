// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// Regression lock for the clean Specification profile (REQ-SCOPE-03, P-01).
// The outputs must match golden vectors generated before the NominalAnalog
// models were added. Tolerance 1e-12 allows for compilers fusing
// multiply-adds differently; any modeling change is far larger.

#include "golden_stimulus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<double> load(const std::string& name)
{
    std::ifstream file(std::string(AK4522_TEST_DATA_DIR) + "/" + name);
    std::vector<double> values;
    std::string line;
    while (std::getline(file, line)) {
        values.push_back(std::strtod(line.c_str(), nullptr));
    }
    return values;
}

void compare(const std::vector<double>& actual, const std::vector<double>& golden)
{
    REQUIRE_FALSE(golden.empty());
    REQUIRE(actual.size() == golden.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < golden.size(); ++i) {
        worst = std::max(worst, std::abs(actual[i] - golden[i]));
    }
    CHECK(worst <= 1e-12);
}

} // namespace

TEST_CASE("Specification profile DacCore output matches the golden vectors", "[unit][golden]")
{
    compare(ak4522::golden::runCore(), load("golden_core.txt"));
}

TEST_CASE("Specification profile HostProcessor output matches the golden vectors", "[unit][golden]")
{
    compare(ak4522::golden::runHost(), load("golden_host.txt"));
}
