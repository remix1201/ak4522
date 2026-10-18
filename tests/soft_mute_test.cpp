// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// Soft mute (REQ-MUTE-01, REQ-MUTE-02; D-M02 to D-M04, I-M01; P-50 to P-53,
// P-72). Expected gains are computed here from integer positions, not by the
// component under test.

#include "core/soft_mute.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using ak4522::detail::SoftMute;

namespace {

constexpr std::int32_t ramp = 1024;

double expectedGain(std::int32_t position)
{
    return static_cast<double>(position) / 1024.0;
}

// Integer reference model: the position after each frame, given the pin
// level in force for that frame.
std::vector<std::int32_t> referencePositions(std::int32_t start, const std::vector<bool>& pinPerFrame)
{
    std::vector<std::int32_t> positions;
    std::int32_t position = start;
    for (const bool high : pinPerFrame) {
        const std::int32_t goal = high ? 0 : ramp;
        position += (goal > position) - (goal < position);
        positions.push_back(position);
    }
    return positions;
}

} // namespace

TEST_CASE("Released mute passes signal at unity", "[unit][mute]")
{
    SoftMute mute;
    mute.reset(false);
    for (int i = 0; i < 3000; ++i) {
        REQUIRE(mute.nextGain() == 1.0);
    }
}

TEST_CASE("Full mute takes exactly 1024 frames and ends at zero", "[unit][mute]")
{
    SoftMute mute;
    mute.reset(false);
    REQUIRE(mute.setPin(true));
    for (std::int32_t frame = 1; frame <= ramp; ++frame) {
        REQUIRE(mute.nextGain() == expectedGain(ramp - frame));
    }
    CHECK(mute.settled());
    CHECK(mute.nextGain() == 0.0); // exactly zero, and it stays there
    CHECK(mute.nextGain() == 0.0);
}

TEST_CASE("Full release takes exactly 1024 frames and ends at unity", "[unit][mute]")
{
    SoftMute mute;
    mute.reset(true);
    REQUIRE(mute.nextGain() == 0.0);
    REQUIRE(mute.setPin(false));
    for (std::int32_t frame = 1; frame <= ramp; ++frame) {
        REQUIRE(mute.nextGain() == expectedGain(frame));
    }
    CHECK(mute.settled());
    CHECK(mute.nextGain() == 1.0);
}

TEST_CASE("Full transition duration follows the DAC rate", "[unit][mute]")
{
    // I-M01: 1024 / fs.
    const auto [rate, milliseconds] = GENERATE(table<double, double>({
        {32000.0, 32.0},
        {44100.0, 23.21995},
        {48000.0, 21.33333},
    }));
    SoftMute mute;
    mute.reset(false);
    mute.setPin(true);
    int frames = 0;
    while (!mute.settled()) {
        static_cast<void>(mute.nextGain());
        ++frames;
    }
    CHECK(frames == ramp);
    CHECK(std::abs(1000.0 * frames / rate - milliseconds) < 1e-5);
}

TEST_CASE("Reversal mid-ramp retraces from the current position", "[unit][mute]")
{
    // AGENTS.md section 13.1: reversal at 1, 511, and 1023 frames.
    const std::int32_t reverseAfter = GENERATE(1, 511, 1023);
    CAPTURE(reverseAfter);

    std::vector<bool> pins(static_cast<std::size_t>(reverseAfter), true);
    pins.resize(static_cast<std::size_t>(reverseAfter + ramp + 10), false);
    const auto expected = referencePositions(ramp, pins);

    SoftMute mute;
    mute.reset(false);
    mute.setPin(true);
    double previous = 1.0;
    for (std::size_t frame = 0; frame < pins.size(); ++frame) {
        if (frame == static_cast<std::size_t>(reverseAfter)) {
            REQUIRE(mute.setPin(false));
        }
        const double gain = mute.nextGain();
        REQUIRE(gain == expectedGain(expected[frame]));
        REQUIRE(std::abs(gain - previous) <= 1.0 / 1024.0); // no gain jump
        previous = gain;
    }
    // The partial reversal takes as many frames as were spent muting.
    CHECK(expected[static_cast<std::size_t>(2 * reverseAfter - 1)] == ramp);
    CHECK(mute.settled());
}

TEST_CASE("Reversal during release retraces toward zero", "[unit][mute]")
{
    SoftMute mute;
    mute.reset(true);
    mute.setPin(false);
    for (int i = 0; i < 300; ++i) {
        static_cast<void>(mute.nextGain());
    }
    REQUIRE(mute.position() == 300);
    REQUIRE(mute.setPin(true));
    CHECK(mute.nextGain() == expectedGain(299));
    for (int i = 0; i < 299; ++i) {
        static_cast<void>(mute.nextGain());
    }
    CHECK(mute.position() == 0);
    CHECK(mute.settled());
}

TEST_CASE("Repeated events at the same level are ignored", "[unit][mute]")
{
    SoftMute mute;
    mute.reset(false);
    CHECK_FALSE(mute.setPin(false)); // already low

    REQUIRE(mute.setPin(true));
    for (int i = 0; i < 100; ++i) {
        static_cast<void>(mute.nextGain());
    }
    CHECK_FALSE(mute.setPin(true)); // does not restart the ramp
    CHECK(mute.nextGain() == expectedGain(ramp - 101));
}

TEST_CASE("Reset latches the pin level without a ramp", "[unit][mute]")
{
    SoftMute mute;
    mute.reset(true);
    CHECK(mute.position() == 0);
    CHECK(mute.nextGain() == 0.0);
    mute.reset(false);
    CHECK(mute.position() == ramp);
    CHECK(mute.nextGain() == 1.0);
}

TEST_CASE("Block processing applies one shared ramp to both channels", "[unit][mute]")
{
    SoftMute mute;
    mute.reset(false);
    mute.setPin(true);
    std::vector<double> left(1500, 0.5);
    std::vector<double> right(1500, -0.25);
    mute.process(left, right);
    for (std::size_t i = 0; i < left.size(); ++i) {
        const std::int32_t position = i < 1024 ? ramp - static_cast<std::int32_t>(i) - 1 : 0;
        REQUIRE(left[i] == 0.5 * expectedGain(position));
        REQUIRE(right[i] == -0.25 * expectedGain(position));
    }
}

TEST_CASE("Output is independent of block partitioning", "[unit][mute][streaming]")
{
    constexpr std::size_t total = 6000;
    // Pin changes at fixed absolute frames, including block-edge cases.
    const std::vector<std::pair<std::size_t, bool>> events{
        {0, true}, {700, false}, {1200, true}, {1201, false}, {2500, true}, {5000, false}};

    std::vector<double> input(total);
    std::mt19937_64 rng(1);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (double& sample : input) {
        sample = dist(rng);
    }

    auto run = [&](const std::vector<std::size_t>& blockSizes) {
        SoftMute mute;
        mute.reset(false);
        std::vector<double> left = input;
        std::vector<double> right = input;
        std::size_t frame = 0;
        std::size_t nextEvent = 0;
        std::size_t block = 0;
        while (frame < total) {
            std::size_t length = std::min(blockSizes[block++ % blockSizes.size()], total - frame);
            // Split at the next event, as the control timeline will.
            while (nextEvent < events.size() && events[nextEvent].first == frame) {
                mute.setPin(events[nextEvent++].second);
            }
            if (nextEvent < events.size() && events[nextEvent].first < frame + length) {
                length = events[nextEvent].first - frame;
            }
            mute.process(std::span(left).subspan(frame, length), std::span(right).subspan(frame, length));
            frame += length;
        }
        return left;
    };

    const auto reference = run({total});
    CHECK(run({1}) == reference);
    CHECK(run({0, 1, 7, 64, 511, 1024, 3}) == reference);
    std::uniform_int_distribution<std::size_t> sizes(0, 300);
    std::vector<std::size_t> randomSizes(50);
    for (auto& size : randomSizes) {
        size = sizes(rng);
    }
    randomSizes.push_back(1);
    CHECK(run(randomSizes) == reference);
}

TEST_CASE("Soft mute does not allocate", "[unit][mute][rt]")
{
    std::vector<double> left(4096, 1.0);
    std::vector<double> right(4096, 1.0);
    SoftMute mute;

    ak4522::test::AllocationCounter counter;
    mute.reset(false);
    mute.setPin(true);
    mute.process(left, right);
    mute.setPin(false);
    mute.process(left, right);
    static_cast<void>(mute.nextGain());
    CHECK(counter.allocations() == 0);
}
