// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// Configuration validation (REQ-CLOCK-01, REQ-RATE-03; D-04, D-C01 to D-C05,
// I-C01, I-C02; P-32, P-35; O-03).

#include "core/config_validation.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>

using namespace ak4522;
using detail::validate;

namespace {

DacConfig dacAt(double rate)
{
    DacConfig config;
    config.dacRate = rate;
    return config;
}

DacConfig withClock(double rate, CmodePin cmode, double mclkHz)
{
    DacConfig config = dacAt(rate);
    config.cmode = cmode;
    config.masterClockHz = mclkHz;
    return config;
}

} // namespace

TEST_CASE("Default configurations are valid", "[unit][clock]")
{
    CHECK(validate(DacConfig{}) == Status::ok);
    CHECK(validate(HostConfig{}) == Status::ok);
}

TEST_CASE("DAC rates inside 16 to 48 kHz are accepted", "[unit][clock]")
{
    const double rate = GENERATE(16000.0, 32000.0, 44100.0, 48000.0, 22050.0, 37800.0);
    CAPTURE(rate);
    CHECK(validate(dacAt(rate)) == Status::ok);
}

TEST_CASE("DAC rates outside 16 to 48 kHz are rejected", "[unit][clock]")
{
    const double rate = GENERATE(15999.999, 48000.001, 8000.0, 88200.0, 96000.0, 0.0, -44100.0,
                                 std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity());
    CAPTURE(rate);
    CHECK(validate(dacAt(rate)) == Status::invalidDacRate);
}

TEST_CASE("Derived MCLK matches the datasheet system clock table", "[unit][clock]")
{
    // [DS] p.9 Table 1. The 44.1 kHz, 512fs entry is printed rounded as 22.579 MHz;
    // the exact product is used here (I-C01).
    const auto [rate, cmode, expectedHz] = GENERATE(table<double, CmodePin, double>({
        {32000.0, CmodePin::low, 8.192e6},
        {32000.0, CmodePin::high, 12.288e6},
        {32000.0, CmodePin::open, 16.384e6},
        {44100.0, CmodePin::low, 11.2896e6},
        {44100.0, CmodePin::high, 16.9344e6},
        {44100.0, CmodePin::open, 22.5792e6},
        {48000.0, CmodePin::low, 12.288e6},
        {48000.0, CmodePin::high, 18.432e6},
        {48000.0, CmodePin::open, 24.576e6},
    }));
    CAPTURE(rate, masterClockRatio(cmode));
    CHECK(detail::derivedMasterClockHz(cmode, rate) == expectedHz);
    CHECK(validate(withClock(rate, cmode, expectedHz)) == Status::ok);
}

TEST_CASE("MCLK range endpoints are accepted at the DAC rate limits", "[unit][clock]")
{
    // [DS] p.7: 256fs 4.096 to 12.288 MHz, 384fs 6.144 to 18.432 MHz,
    // 512fs 8.192 to 24.576 MHz.
    const auto [rate, cmode, mclkHz] = GENERATE(table<double, CmodePin, double>({
        {16000.0, CmodePin::low, 4.096e6},
        {48000.0, CmodePin::low, 12.288e6},
        {16000.0, CmodePin::high, 6.144e6},
        {48000.0, CmodePin::high, 18.432e6},
        {16000.0, CmodePin::open, 8.192e6},
        {48000.0, CmodePin::open, 24.576e6},
    }));
    CAPTURE(rate, mclkHz);
    CHECK(validate(withClock(rate, cmode, mclkHz)) == Status::ok);
}

TEST_CASE("MCLK not synchronized with the DAC rate is rejected", "[unit][clock]")
{
    // Valid frequency for the ratio, but for a different sample rate.
    CHECK(validate(withClock(44100.0, CmodePin::low, 12.288e6)) == Status::invalidMasterClock);
    // Right sample rate, wrong ratio for CMODE.
    CHECK(validate(withClock(44100.0, CmodePin::low, 384.0 * 44100.0)) == Status::invalidMasterClock);
    CHECK(validate(withClock(44100.0, CmodePin::open, 256.0 * 44100.0)) == Status::invalidMasterClock);
}

TEST_CASE("MCLK synchronization uses a 1e-9 relative tolerance", "[unit][clock]")
{
    const double exact = 256.0 * 44100.0;
    CHECK(validate(withClock(44100.0, CmodePin::low, exact * (1.0 + 1e-10))) == Status::ok);
    CHECK(validate(withClock(44100.0, CmodePin::low, exact * (1.0 - 1e-10))) == Status::ok);
    CHECK(validate(withClock(44100.0, CmodePin::low, exact * (1.0 + 1e-8))) == Status::invalidMasterClock);
    CHECK(validate(withClock(44100.0, CmodePin::low, exact * (1.0 - 1e-8))) == Status::invalidMasterClock);
}

TEST_CASE("MCLK outside the CMODE range is rejected", "[unit][clock]")
{
    // Rejected through the synchronization rule, which implies the range (I-C02).
    CHECK(validate(withClock(16000.0, CmodePin::low, 4.0e6)) == Status::invalidMasterClock);
    CHECK(validate(withClock(48000.0, CmodePin::open, 25.0e6)) == Status::invalidMasterClock);
    CHECK(validate(withClock(44100.0, CmodePin::low, std::numeric_limits<double>::quiet_NaN()))
          == Status::invalidMasterClock);
    CHECK(validate(withClock(44100.0, CmodePin::low, 0.0)) == Status::invalidMasterClock);
}

TEST_CASE("The measured profile is rejected until one exists", "[unit][clock]")
{
    DacConfig config;
    config.profile = ModelProfile::measured;
    CHECK(validate(config) == Status::invalidConfiguration);
    config.profile = ModelProfile::nominalAnalog;
    CHECK(validate(config) == Status::ok);
}

TEST_CASE("Out-of-range enum values are rejected", "[unit][clock]")
{
    DacConfig config;
    config.cmode = static_cast<CmodePin>(3);
    CHECK(validate(config) == Status::invalidConfiguration);

    config = DacConfig{};
    config.initialDeemphasis = static_cast<DeemphasisSelect>(4);
    CHECK(validate(config) == Status::invalidConfiguration);

    config = DacConfig{};
    config.profile = static_cast<ModelProfile>(9);
    CHECK(validate(config) == Status::invalidConfiguration);

    HostConfig host;
    host.inputResolution = static_cast<InputResolution>(2);
    CHECK(validate(host) == Status::invalidConfiguration);
}

TEST_CASE("Supported host rates are accepted", "[unit][rate]")
{
    HostConfig config;
    config.hostRate = GENERATE(44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0);
    CAPTURE(config.hostRate);
    CHECK(validate(config) == Status::ok);
}

TEST_CASE("Unsupported host rates are rejected", "[unit][rate]")
{
    HostConfig config;
    config.hostRate = GENERATE(32000.0, 22050.0, 44100.5, 384000.0, 0.0,
                               std::numeric_limits<double>::quiet_NaN());
    CAPTURE(config.hostRate);
    CHECK(validate(config) == Status::unsupportedHostRate);
}

TEST_CASE("Host capacities must be non-zero", "[unit][rate]")
{
    HostConfig config;
    config.maxBlockFrames = 0;
    CHECK(validate(config) == Status::invalidBlockSize);

    config = HostConfig{};
    config.maxPendingEvents = 0;
    CHECK(validate(config) == Status::invalidConfiguration);
}

TEST_CASE("Host validation reports DAC configuration errors", "[unit][rate]")
{
    HostConfig config;
    config.dac.dacRate = 96000.0; // a valid host rate is not a valid DAC rate
    CHECK(validate(config) == Status::invalidDacRate);
}
