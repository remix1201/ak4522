// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// Pipeline delay and reconstruction sections (REQ-FILTER-02, REQ-LAT-02;
// D-03; P-87, P-89). The response and 14.7-frame delay gates are measured on
// the whole linear chain by tools/analyze_response.py (CTest
// dsp.core_linear_response_*); these tests cover the time-domain contract.

#include "dsp/reconstruction.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using ak4522::detail::PipelineDelay;
using ak4522::detail::Reconstruction;

namespace {

// Independent reference: the sections' difference equation in direct form I,
// a different structure from the implementation's transposed form II.
std::vector<double> directFormI(const std::vector<double>& x)
{
    std::vector<double> v = x;
    for (const auto& c : Reconstruction::coefficients()) {
        std::vector<double> y(v.size());
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (std::size_t n = 0; n < v.size(); ++n) {
            y[n] = c.b0 * v[n] + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
            x2 = x1;
            x1 = v[n];
            y2 = y1;
            y1 = y[n];
        }
        v = y;
    }
    return v;
}

} // namespace

TEST_CASE("Pipeline delay shifts by exactly 21 samples", "[unit][filter]")
{
    PipelineDelay delay;
    std::vector<double> left(100, 0.0);
    std::vector<double> right(100, 0.0);
    left[0] = 1.0;
    right[5] = -1.0;
    delay.process(left, right);
    for (std::size_t n = 0; n < left.size(); ++n) {
        REQUIRE(left[n] == (n == PipelineDelay::samples ? 1.0 : 0.0));
        REQUIRE(right[n] == (n == PipelineDelay::samples + 5 ? -1.0 : 0.0));
    }
}

TEST_CASE("Sections are stable with unity DC gain", "[unit][filter]")
{
    double dcGain = 1.0;
    for (const auto& c : Reconstruction::coefficients()) {
        // Poles of 1 + a1 z^-1 + a2 z^-2 inside the unit circle.
        const double disc = c.a1 * c.a1 - 4.0 * c.a2;
        const double radius = disc < 0.0
            ? std::sqrt(c.a2)
            : std::max(std::abs((-c.a1 + std::sqrt(disc)) / 2.0), std::abs((-c.a1 - std::sqrt(disc)) / 2.0));
        CHECK(radius < 1.0);
        dcGain *= (c.b0 + c.b1 + c.b2) / (1.0 + c.a1 + c.a2);
    }
    CHECK(std::abs(dcGain - 1.0) < 1e-12);
}

TEST_CASE("Sections match an independent direct-form-I reference", "[unit][filter]")
{
    std::mt19937_64 rng(13);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(4000);
    for (double& v : x) {
        v = dist(rng);
    }
    const std::vector<double> expected = directFormI(x);
    std::vector<double> left = x;
    std::vector<double> right = x;
    Reconstruction reconstruction;
    reconstruction.process(left, right);
    for (std::size_t n = 0; n < x.size(); ++n) {
        REQUIRE(std::abs(left[n] - expected[n]) < 1e-12);
        REQUIRE(right[n] == left[n]);
    }
}

TEST_CASE("Signals far below one 20-bit LSB pass unchanged by the flush", "[unit][filter]")
{
    // One LSB is 2^-19, about 1.9e-6. A tone at 1/16 LSB must match the
    // reference: the flush threshold may only remove what is far below that.
    const double amplitude = std::ldexp(1.0, -23);
    std::vector<double> x(6000);
    for (std::size_t n = 0; n < x.size(); ++n) {
        x[n] = amplitude * std::sin(0.01 * static_cast<double>(n));
    }
    const std::vector<double> expected = directFormI(x);
    std::vector<double> left = x;
    std::vector<double> right = x;
    Reconstruction reconstruction;
    reconstruction.process(left, right);
    for (std::size_t n = 0; n < x.size(); ++n) {
        REQUIRE(std::abs(left[n] - expected[n]) <= 1e-9 * amplitude);
    }
}

TEST_CASE("Impulse response decays to exactly zero", "[unit][filter]")
{
    // States below the flush threshold become zero, so a long silent tail
    // ends in exact zeros instead of subnormal numbers.
    Reconstruction reconstruction;
    std::vector<double> left(20000, 0.0);
    std::vector<double> right(20000, 0.0);
    left[0] = 1.0;
    reconstruction.process(left, right);
    CHECK(std::all_of(left.end() - 1000, left.end(), [](double v) { return v == 0.0; }));
    for (const double v : left) {
        REQUIRE((v == 0.0 || std::abs(v) >= 1e-300));
    }
}

TEST_CASE("Delay and sections are independent of block partitioning", "[unit][filter][streaming]")
{
    std::mt19937_64 rng(14);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(5000);
    for (double& v : x) {
        v = dist(rng);
    }
    auto run = [&](const std::vector<std::size_t>& sizes) {
        PipelineDelay delay;
        Reconstruction reconstruction;
        std::vector<double> left = x;
        std::vector<double> right = x;
        std::size_t at = 0;
        std::size_t block = 0;
        while (at < x.size()) {
            const std::size_t n = std::min(sizes[block++ % sizes.size()], x.size() - at);
            delay.process(std::span(left).subspan(at, n), std::span(right).subspan(at, n));
            reconstruction.process(std::span(left).subspan(at, n), std::span(right).subspan(at, n));
            at += n;
        }
        return left;
    };
    const auto reference = run({x.size()});
    CHECK(run({1}) == reference);
    CHECK(run({0, 3, 21, 22, 500}) == reference);
}

TEST_CASE("Channels are independent and reset clears state", "[unit][filter]")
{
    PipelineDelay delay;
    Reconstruction reconstruction;
    std::vector<double> left(200, 0.0);
    std::vector<double> right(200, 0.0);
    left[0] = 1.0;
    delay.process(left, right);
    reconstruction.process(left, right);
    CHECK(std::all_of(right.begin(), right.end(), [](double v) { return v == 0.0; }));

    std::vector<double> busy(64, 0.9);
    std::vector<double> busy2(64, -0.9);
    delay.process(busy, busy2);
    reconstruction.process(busy, busy2);
    delay.reset();
    reconstruction.reset();
    std::vector<double> zeros(200, 0.0);
    std::vector<double> zeros2(200, 0.0);
    delay.process(zeros, zeros2);
    reconstruction.process(zeros, zeros2);
    CHECK(std::all_of(zeros.begin(), zeros.end(), [](double v) { return v == 0.0; }));
    CHECK(std::all_of(zeros2.begin(), zeros2.end(), [](double v) { return v == 0.0; }));
}

TEST_CASE("Reconstruction does not allocate", "[unit][filter][rt]")
{
    PipelineDelay delay;
    Reconstruction reconstruction;
    std::vector<double> left(2048, 0.3);
    std::vector<double> right(2048, -0.3);
    ak4522::test::AllocationCounter counter;
    delay.process(left, right);
    reconstruction.process(left, right);
    delay.reset();
    reconstruction.reset();
    CHECK(counter.allocations() == 0);
}
