// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// HostProcessor's ADC paths: loopback (DAC into ADC) and adc (host audio as
// the ADC's input) (REQ-ADC-06, REQ-RATE-01, REQ-LAT-03; P-108, P-111).
// Timing is measured from sine-fit phases, independent of the implementation.

#include "ak4522/host_processor.hpp"
#include "support/allocation_counter.hpp"
#include "support/sine_fit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace ak4522;

namespace {

constexpr double pi = 3.14159265358979323846;

struct Stereo {
    std::vector<double> left, right;
};

HostConfig config(HostPath path, double host, double rate, std::size_t maxBlock = 512)
{
    HostConfig c;
    c.path = path;
    c.hostRate = host;
    c.dac.dacRate = rate;
    c.maxBlockFrames = maxBlock;
    return c;
}

Stereo sine(std::size_t frames, double freq, double rate, double amplitude = 0.5)
{
    Stereo s{std::vector<double>(frames), std::vector<double>(frames)};
    for (std::size_t n = 0; n < frames; ++n) {
        s.left[n] = amplitude * std::sin(2.0 * pi * freq * static_cast<double>(n) / rate);
        s.right[n] = -s.left[n];
    }
    return s;
}

Stereo run(HostProcessor& p, const Stereo& in, const std::vector<std::size_t>& sizes,
           const std::vector<ControlEvent>& absolute = {})
{
    Stereo out{std::vector<double>(in.left.size()), std::vector<double>(in.left.size())};
    std::size_t at = 0, block = 0, next = 0;
    while (at < in.left.size()) {
        const std::size_t n = std::min(sizes[block++ % sizes.size()], in.left.size() - at);
        std::vector<ControlEvent> local;
        while (next < absolute.size() && absolute[next].offset < at + n) {
            ControlEvent e = absolute[next++];
            e.offset -= static_cast<std::uint32_t>(at);
            local.push_back(e);
        }
        REQUIRE(p.process(StereoView<const double>{std::span(in.left).subspan(at, n), std::span(in.right).subspan(at, n)},
                          StereoView<double>{std::span(out.left).subspan(at, n), std::span(out.right).subspan(at, n)},
                          local)
                == Status::ok);
        at += n;
    }
    return out;
}

struct Rates {
    double host;
    double rate;
};

const HostPath adcPaths[] = {HostPath::loopback, HostPath::adc};

} // namespace

TEST_CASE("ADC paths: output timing matches the reported latency", "[dsp][host][adc]")
{
    const HostPath path = GENERATE(HostPath::loopback, HostPath::adc);
    const Rates r = GENERATE(Rates{44100, 44100}, Rates{48000, 44100}, Rates{96000, 48000}, Rates{44100, 32000});
    CAPTURE(static_cast<int>(path), r.host, r.rate);
    HostProcessor p;
    REQUIRE(p.prepare(config(path, r.host, r.rate)) == Status::ok);
    const double expected = p.latency().reportedHostFrames;
    const double edge = 20.0 / 44.1 * std::min(r.host, r.rate);
    const double scale = std::max(1.0, r.host / r.rate);
    double sum = 0.0;
    for (int k = 1; k <= 6; ++k) {
        const double freq = edge * k / 7.0;
        p.reset();
        const auto out = run(p, sine(static_cast<std::size_t>(r.host * 0.4), freq, r.host), {512});
        const std::size_t skip = static_cast<std::size_t>(expected + 600 * scale) + 2000;
        const auto fit = test::fitSine(std::span(out.left).subspan(skip), freq, r.host,
                                       static_cast<double>(skip) / r.host);
        const double period = r.host / freq;
        double d = -fit.phase / (2.0 * pi) * period;
        d += std::round((expected - d) / period) * period;
        CAPTURE(freq, d, expected, fit.amplitude);
        CHECK(std::abs(d - expected) < 0.02 * scale);
        CHECK(std::abs(20.0 * std::log10(fit.amplitude / 0.5)) < 0.1);
        sum += d;
    }
    CHECK(std::abs(sum / 6.0 - expected) <= 0.01 * scale);
}

TEST_CASE("ADC paths report their chip delay", "[unit][host][adc]")
{
    HostProcessor p;
    REQUIRE(p.prepare(config(HostPath::loopback, 48000, 44100)) == Status::ok);
    CHECK(p.latency().coreDacFrames == 14.7 + 29.3125);
    REQUIRE(p.prepare(config(HostPath::adc, 48000, 44100)) == Status::ok);
    CHECK(p.latency().coreDacFrames == 29.3125);
    REQUIRE(p.prepare(config(HostPath::dac, 48000, 44100)) == Status::ok);
    CHECK(p.latency().coreDacFrames == 14.7);
    for (const HostPath path : adcPaths) {
        REQUIRE(p.prepare(config(path, 48000, 44100)) == Status::ok);
        const LatencyInfo l = p.latency();
        const double total = l.bufferingHostFrames + l.coreDacFrames * 48000.0 / 44100.0 + l.alignmentHostFrames;
        CHECK(std::abs(total - l.reportedHostFrames) < 1e-9);
    }
    HostConfig bad = config(HostPath::dac, 48000, 44100);
    bad.path = static_cast<HostPath>(3);
    CHECK(p.prepare(bad) == Status::invalidConfiguration);
}

TEST_CASE("ADC paths start with the 516-frame initialization", "[unit][host][adc]")
{
    for (const HostPath path : adcPaths) {
        CAPTURE(static_cast<int>(path));
        HostProcessor p;
        REQUIRE(p.prepare(config(path, 48000, 48000)) == Status::ok);
        const auto out = run(p, sine(4000, 1000.0, 48000.0), {256});
        // The ADC outputs "0" for its first 516 frames. ADC frame j reaches the
        // host at about j frames plus the buffering; the reported latency adds
        // the chip's delay to the signal, which the zeros do not have (P-111).
        const auto quiet = static_cast<std::size_t>(p.latency().bufferingHostFrames + 516 - 64);
        CHECK(std::all_of(out.left.begin(), out.left.begin() + static_cast<std::ptrdiff_t>(quiet),
                          [](double v) { return v == 0.0; }));
        CHECK(std::abs(out.left[p.latency().reportedHostFrames + 1000]) > 0.0);
    }
}

TEST_CASE("ADC paths never underrun and do not depend on block sizes", "[unit][host][adc][streaming]")
{
    const HostPath path = GENERATE(HostPath::loopback, HostPath::adc);
    const Rates r = GENERATE(Rates{44100, 48000}, Rates{192000, 16000}, Rates{88200, 44100});
    CAPTURE(static_cast<int>(path), r.host, r.rate);
    std::mt19937_64 rng(2);
    std::uniform_int_distribution<std::size_t> size(0, 700);
    std::vector<std::size_t> sizes(50);
    for (auto& s : sizes) {
        s = size(rng);
    }
    sizes.push_back(1);
    const auto in = sine(static_cast<std::size_t>(r.host * 0.3), 997.0, r.host);
    const std::vector<ControlEvent> events{ControlEvent::powerDown(5000, false), ControlEvent::powerDown(6000, true)};
    HostProcessor a, b;
    REQUIRE(a.prepare(config(path, r.host, r.rate, 256)) == Status::ok);
    REQUIRE(b.prepare(config(path, r.host, r.rate, 256)) == Status::ok);
    const auto one = run(a, in, {in.left.size()}, events);
    const auto many = run(b, in, sizes, events);
    CHECK(one.left == many.left);
    CHECK(one.right == many.right);
    CHECK(a.diagnostics().hostUnderruns == 0);
    CHECK(b.diagnostics().hostUnderruns == 0);
}

TEST_CASE("PD reaches the ADC on the adc path; over-range is counted", "[unit][host][adc]")
{
    HostProcessor p;
    REQUIRE(p.prepare(config(HostPath::adc, 44100, 44100)) == Status::ok);
    const auto in = sine(20000, 500.0, 44100.0);
    const auto out = run(p, in, {512}, {ControlEvent::powerDown(8000, false), ControlEvent::powerDown(9000, true)});
    // From PD low until the release plus 516 frames the ADC outputs "0"; its
    // frames reach the host after the buffering, with the converter's spread.
    const std::size_t buffering = p.latency().bufferingHostFrames;
    CHECK(std::all_of(out.left.begin() + static_cast<std::ptrdiff_t>(8000 + buffering + 64),
                      out.left.begin() + static_cast<std::ptrdiff_t>(9000 + 516 + buffering - 64),
                      [](double v) { return v == 0.0; }));
    CHECK(p.diagnostics().adcClippedSamples == 0);

    REQUIRE(p.prepare(config(HostPath::adc, 44100, 44100)) == Status::ok);
    static_cast<void>(run(p, sine(8000, 500.0, 44100.0, 1.3), {512}));
    CHECK(p.diagnostics().adcClippedSamples > 0);
}

TEST_CASE("Loopback accepts the DAC's DC offset; the ADC removes it", "[unit][host][adc]")
{
    HostConfig c = config(HostPath::loopback, 44100, 44100, 4096);
    c.dac.profile = ModelProfile::nominalAnalog;
    c.dac.analog.dcOffset = true;
    HostProcessor p;
    REQUIRE(p.prepare(c) == Status::ok);
    Stereo zero{std::vector<double>(44100 * 6, 0.0), std::vector<double>(44100 * 6, 0.0)};
    const auto out = run(p, zero, {4096});
    CHECK(out.left.back() == 0.0);
}

TEST_CASE("ADC paths do not allocate", "[unit][host][adc][rt]")
{
    for (const HostPath path : adcPaths) {
        HostProcessor p;
        REQUIRE(p.prepare(config(path, 48000, 44100, 512)) == Status::ok);
        std::vector<float> in(2048, 0.1f), l(2048), r(2048);
        const std::vector<ControlEvent> events{ControlEvent::powerDown(5, false), ControlEvent::powerDown(900, true)};
        test::AllocationCounter counter;
        const Status s = p.process(StereoView<const float>{in, in}, StereoView<float>{l, r}, events);
        p.reset();
        static_cast<void>(p.diagnostics());
        CHECK(counter.allocations() == 0);
        CHECK(s == Status::ok);
    }
}

TEST_CASE("PD restarts the ADC's initialization on the loopback path too", "[unit][host][adc]")
{
    HostProcessor p;
    REQUIRE(p.prepare(config(HostPath::loopback, 44100, 44100)) == Status::ok);
    const auto out = run(p, sine(20000, 500.0, 44100.0), {512},
                         {ControlEvent::powerDown(8000, false), ControlEvent::powerDown(9000, true)});
    // The DAC resumes at once after release; the ADC stays "0" for 516 frames more (AD-10).
    const std::size_t buffering = p.latency().bufferingHostFrames;
    CHECK(std::all_of(out.left.begin() + static_cast<std::ptrdiff_t>(8000 + buffering + 64),
                      out.left.begin() + static_cast<std::ptrdiff_t>(9000 + 516 + buffering - 64),
                      [](double v) { return v == 0.0; }));
    CHECK(std::abs(out.left[9000 + 516 + p.latency().reportedHostFrames + 200]) > 0.0);
}

TEST_CASE("Reset returns the ADC paths to the state after prepare", "[unit][host][adc]")
{
    for (const HostPath path : adcPaths) {
        CAPTURE(static_cast<int>(path));
        HostProcessor p;
        REQUIRE(p.prepare(config(path, 48000, 44100)) == Status::ok);
        const auto in = sine(6000, 700.0, 48000.0);
        const auto first = run(p, in, {256});
        p.reset();
        CHECK(run(p, in, {256}).left == first.left);
    }
}
