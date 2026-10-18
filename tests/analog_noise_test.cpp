// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// NominalAnalog noise (REQ-NOISE-01, REQ-NOISE-02; P-54, P-74, P-91, P-94,
// P-95). The weighted S/N calibration is measured in Python (CTest label
// analog); these tests cover the generator and its wiring into DacCore.

#include "ak4522/dac_core.hpp"
#include "analog/analog_noise.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace ak4522;
using detail::AnalogNoise;

namespace {

struct Stats {
    double mean, variance, kurtosis;
};

Stats stats(const std::vector<double>& x)
{
    double m = 0.0;
    for (const double v : x) {
        m += v;
    }
    m /= static_cast<double>(x.size());
    double m2 = 0.0, m4 = 0.0;
    for (const double v : x) {
        const double d = (v - m) * (v - m);
        m2 += d;
        m4 += d * d;
    }
    m2 /= static_cast<double>(x.size());
    m4 /= static_cast<double>(x.size());
    return {m, m2, m4 / (m2 * m2)};
}

std::pair<std::vector<double>, std::vector<double>> noiseOnly(std::uint64_t seed, std::size_t n, double rate = 352800.0)
{
    AnalogNoise noise;
    noise.prepare(seed, rate);
    std::vector<double> l(n, 0.0), r(n, 0.0);
    noise.add(l, r);
    return {l, r};
}

DacConfig nominal(bool withNoise)
{
    DacConfig c;
    c.profile = ModelProfile::nominalAnalog;
    c.analog.noise = withNoise;
    return c;
}

std::vector<double> runCore(const DacConfig& c, std::size_t frames, const std::vector<ControlEvent>& events = {},
                            Pcm20 level = Pcm20{0})
{
    DacCore core;
    REQUIRE(core.prepare(c, 256) == Status::ok);
    std::vector<Pcm20Frame> in(frames, {level, level});
    std::vector<double> l(frames * DacCore::oversampling), r(l.size());
    REQUIRE(core.process(in, events, {l, r}).status == Status::ok);
    return l;
}

} // namespace

TEST_CASE("Noise level follows the physical density at every rate", "[unit][analog]")
{
    for (const double rate : {256000.0, 352800.0, 384000.0}) {
        AnalogNoise noise;
        noise.prepare(1, rate);
        CHECK(std::abs(noise.sigma() - std::sqrt(AnalogNoise::density() * rate / 2.0)) < 1e-18);
    }
    // 100 dB A-weighted S/N over an A-weighted bandwidth of about 12 468 Hz.
    CHECK(std::abs(AnalogNoise::density() - 0.5e-10 / 12467.86) < 1e-18);
}

TEST_CASE("Noise is zero-mean Gaussian with the set variance", "[unit][analog]")
{
    const auto [l, r] = noiseOnly(7, 1'000'000);
    AnalogNoise reference;
    reference.prepare(7, 352800.0);
    const double sigma = reference.sigma();
    for (const auto& x : {l, r}) {
        const Stats s = stats(x);
        CHECK(std::abs(s.mean) < 5.0 * sigma / 1000.0);      // 5 standard errors
        CHECK(std::abs(s.variance / (sigma * sigma) - 1.0) < 0.01);
        CHECK(std::abs(s.kurtosis - 3.0) < 0.05);              // Gaussian
    }
}

TEST_CASE("Channels are independent", "[unit][analog]")
{
    const auto [l, r] = noiseOnly(8, 1'000'000);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) {
        lr += l[i] * r[i];
        ll += l[i] * l[i];
        rr += r[i] * r[i];
    }
    CHECK(std::abs(lr / std::sqrt(ll * rr)) < 0.005); // about 5 standard errors
}

TEST_CASE("Noise is reproducible from its seed", "[unit][analog]")
{
    CHECK(noiseOnly(42, 1000).first == noiseOnly(42, 1000).first);
    CHECK(noiseOnly(42, 1000).first != noiseOnly(43, 1000).first);
}

TEST_CASE("NominalAnalog without options equals the clean profile", "[unit][analog]")
{
    CHECK(runCore(nominal(false), 500, {}, Pcm20{1000}) == runCore(DacConfig{}, 500, {}, Pcm20{1000}));
    CHECK(runCore(nominal(true), 500, {}, Pcm20{1000}) != runCore(DacConfig{}, 500, {}, Pcm20{1000}));
}

TEST_CASE("Noise stays when muted and stops in power-down", "[unit][analog]")
{
    const std::size_t os = DacCore::oversampling;
    const auto out = runCore(nominal(true), 4000,
                             {ControlEvent::softMute(0, true), ControlEvent::powerDown(3000, false)},
                             Pcm20{Pcm20::maxCode});
    // Fully muted by frame 1024 + filter delay; noise remains (P-54).
    const auto mutedBegin = out.begin() + static_cast<std::ptrdiff_t>(1200 * os);
    const auto mutedEnd = out.begin() + static_cast<std::ptrdiff_t>(2900 * os);
    CHECK(std::any_of(mutedBegin, mutedEnd, [](double v) { return v != 0.0; }));
    double peak = 0.0;
    for (auto it = mutedBegin; it != mutedEnd; ++it) {
        peak = std::max(peak, std::abs(*it));
    }
    CHECK(peak < 1e-3); // noise only, no signal
    // PD low: exactly VCOM (P-95).
    CHECK(std::all_of(out.begin() + static_cast<std::ptrdiff_t>(3000 * os), out.end(),
                      [](double v) { return v == 0.0; }));
}

TEST_CASE("Reset does not reseed the noise", "[unit][analog]")
{
    // P-74: host lifecycle resets continue the noise stream.
    DacCore core;
    REQUIRE(core.prepare(nominal(true), 256) == Status::ok);
    std::vector<Pcm20Frame> in(200);
    std::vector<double> a(1600), b(1600), r(1600);
    REQUIRE(core.process(in, {}, {a, r}).status == Status::ok);
    core.reset();
    REQUIRE(core.process(in, {}, {b, r}).status == Status::ok);
    CHECK(a != b);
    // A fresh prepare restarts it.
    REQUIRE(core.prepare(nominal(true), 256) == Status::ok);
    REQUIRE(core.process(in, {}, {b, r}).status == Status::ok);
    CHECK(a == b);
}

TEST_CASE("Noise generation does not allocate", "[unit][analog][rt]")
{
    DacCore core;
    REQUIRE(core.prepare(nominal(true), 256) == Status::ok);
    std::vector<Pcm20Frame> in(1000);
    std::vector<double> l(8000), r(8000);
    test::AllocationCounter counter;
    const auto result = core.process(in, {}, {l, r});
    CHECK(counter.allocations() == 0);
    CHECK(result.status == Status::ok);
}
