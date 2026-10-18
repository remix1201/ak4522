// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// NominalAnalog output stage: distortion, mismatch, crosstalk, DC offset
// (REQ-DIST-01, REQ-CHANNEL-01, REQ-ANALOG-01, REQ-ANALOG-02; P-96 to P-99),
// and the voltage and profile helpers. Levels are measured on DacCore's
// output with least-squares sine fits, one imperfection at a time.

#include "ak4522/analog.hpp"
#include "ak4522/dac_core.hpp"
#include "ak4522/host_processor.hpp"
#include "analog/output_stage.hpp"
#include "support/allocation_counter.hpp"
#include "support/multi_sine_fit.hpp"
#include "support/sine_fit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace ak4522;

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double dacRate = 44100.0;
constexpr double highRate = 8.0 * dacRate;

double db(double x)
{
    return 20.0 * std::log10(x);
}

struct Out {
    std::vector<double> left;
    std::vector<double> right;
};

// Runs DacCore with the given options on per-channel 1 kHz-type sines.
Out run(const AnalogOptions& options, double leftDbfs, double rightDbfs, double freq = 1000.0,
        std::size_t frames = 44100)
{
    DacConfig c;
    c.profile = ModelProfile::nominalAnalog;
    c.analog = options;
    DacCore core;
    REQUIRE(core.prepare(c, 1024) == Status::ok);
    const double al = leftDbfs < -500.0 ? 0.0 : std::pow(10.0, leftDbfs / 20.0);
    const double ar = rightDbfs < -500.0 ? 0.0 : std::pow(10.0, rightDbfs / 20.0);
    std::vector<Pcm20Frame> in(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        const double s = std::sin(2.0 * pi * freq * static_cast<double>(n) / dacRate);
        in[n] = {Pcm20{static_cast<std::int32_t>(std::lround(al * s * 524287.0))},
                 Pcm20{static_cast<std::int32_t>(std::lround(ar * s * 524287.0))}};
    }
    Out out{std::vector<double>(frames * 8), std::vector<double>(frames * 8)};
    REQUIRE(core.process(in, {}, {out.left, out.right}).status == Status::ok);
    return out;
}

std::span<const double> settled(const std::vector<double>& x)
{
    return std::span(x).subspan(8000); // skip filter start-up
}

} // namespace

TEST_CASE("Distortion shapes give the calibrated harmonics at 0 dBFS", "[unit][analog]")
{
    // Total distortion -90.76 dB re the tone, so with the noise floor S/(N+D)
    // is 90 dB (P-96).
    struct Case {
        DistortionShape shape;
        double h2, h3; // dB re the fundamental; -999 means absent
    };
    const Case c = GENERATE(Case{DistortionShape::equal2nd3rd, -93.77, -93.77},
                            Case{DistortionShape::mostly3rd, -999.0, -90.76},
                            Case{DistortionShape::mostly2nd, -90.76, -999.0});
    CAPTURE(static_cast<int>(c.shape));
    AnalogOptions o;
    o.distortion = true;
    o.distortionShape = c.shape;
    // 997 Hz: a tone whose 20-bit quantization error does not repeat in a
    // short period, so it spreads like noise instead of adding harmonics.
    const Out out = run(o, 0.0, -999.0, 997.0);
    const std::vector<double> freqs{997.0, 1994.0, 2991.0};
    const auto fit = test::fitSines(settled(out.left), freqs, highRate);
    const double h2 = db(fit.amplitudes[1] / fit.amplitudes[0]);
    const double h3 = db(fit.amplitudes[2] / fit.amplitudes[0]);
    CAPTURE(h2, h3);
    if (c.h2 > -500.0) {
        CHECK(std::abs(h2 - c.h2) < 0.05);
    } else {
        CHECK(h2 < -140.0);
    }
    if (c.h3 > -500.0) {
        CHECK(std::abs(h3 - c.h3) < 0.05);
    } else {
        CHECK(h3 < -140.0);
    }
}

TEST_CASE("The cubic term compresses", "[unit][analog]")
{
    // y = x + a3 x^3 with x = sin(t) gives -(a3 / 4) sin(3t). A compressive
    // (negative) a3 puts the 3rd harmonic in phase with sin(3t) when the
    // fundamental's phase is zero (P-96).
    for (const auto shape : {DistortionShape::equal2nd3rd, DistortionShape::mostly3rd}) {
        CHECK(detail::OutputStage::distortion(shape).cubic < 0.0);
    }
    AnalogOptions o;
    o.distortion = true;
    o.distortionShape = DistortionShape::mostly3rd;
    const Out out = run(o, -6.0, -999.0, 997.0);
    // Joint fit: fitting each frequency alone would pick up leakage from the
    // much stronger fundamental.
    const auto fit = test::fitSines(settled(out.left), std::vector<double>{997.0, 2991.0}, highRate);
    // Phase of the 3rd harmonic relative to three times the fundamental's.
    const double relative = std::remainder(fit.phases[1] - 3.0 * fit.phases[0], 2.0 * pi);
    CHECK(std::abs(relative) < 0.1);
}

TEST_CASE("Distortion falls with level as a polynomial", "[unit][analog]")
{
    // 2nd harmonic falls 1 dB per dB of level relative to the tone, 3rd by 2.
    // At -6 dBFS the harmonics (-105.8 and -111.8 dBFS) stay well above the
    // 20-bit quantization error of the test tone.
    AnalogOptions o;
    o.distortion = true;
    const Out out = run(o, -6.0, -999.0, 997.0);
    const auto fit = test::fitSines(settled(out.left), std::vector<double>{997.0, 1994.0, 2991.0}, highRate);
    CHECK(std::abs(db(fit.amplitudes[1] / fit.amplitudes[0]) - (-93.77 - 6.0)) < 0.1);
    CHECK(std::abs(db(fit.amplitudes[2] / fit.amplitudes[0]) - (-93.77 - 12.0)) < 0.2);
}

TEST_CASE("Fixed mismatch makes the left channel 0.2 dB louder", "[unit][analog]")
{
    AnalogOptions o;
    o.channelMismatch = true;
    const Out out = run(o, -6.0, -6.0);
    const double l = test::fitSine(settled(out.left), 1000.0, highRate).amplitude;
    const double r = test::fitSine(settled(out.right), 1000.0, highRate).amplitude;
    CHECK(std::abs(db(l / r) - 0.2) < 0.001);
}

TEST_CASE("Per-instance mismatch is seeded, capped, and centred on 0.2 dB", "[unit][analog]")
{
    using detail::OutputStage;
    CHECK(OutputStage::mismatchDb(MismatchMode::perInstance, 5) == OutputStage::mismatchDb(MismatchMode::perInstance, 5));
    std::vector<double> totals;
    int positive = 0;
    for (std::uint64_t seed = 0; seed < 4001; ++seed) {
        const auto [l, r] = OutputStage::mismatchDb(MismatchMode::perInstance, seed);
        CHECK(std::abs(l + r) < 1e-12); // split symmetrically
        totals.push_back(std::abs(l - r));
        positive += l > 0.0 ? 1 : 0;
    }
    std::sort(totals.begin(), totals.end());
    CHECK(totals.back() <= 0.5 + 1e-12);                 // D-A05 maximum
    CHECK(std::abs(totals[totals.size() / 2] - 0.2) < 0.02); // median near the typical
    CHECK(positive > 1800);
    CHECK(positive < 2200);
}

TEST_CASE("Crosstalk is -110 dB at 1 kHz with the chosen shape", "[unit][analog]")
{
    struct Case {
        CrosstalkShape shape;
        double at10k;     // dB at 10 kHz
        double phaseDeg;  // leakage phase relative to the source at 1 kHz
    };
    const Case c = GENERATE(Case{CrosstalkShape::capacitive, -90.0, 90.0}, Case{CrosstalkShape::flat, -110.0, 0.0});
    CAPTURE(static_cast<int>(c.shape));
    AnalogOptions o;
    o.crosstalk = true;
    o.crosstalkShape = c.shape;
    for (const double f : {1000.0, 10000.0}) {
        const Out out = run(o, -6.0, -999.0, f);
        const auto src = test::fitSine(settled(out.left), f, highRate);
        const auto leak = test::fitSine(settled(out.right), f, highRate);
        const double level = db(leak.amplitude / src.amplitude);
        CAPTURE(f, level);
        CHECK(std::abs(level - (f == 1000.0 ? -110.0 : c.at10k)) < 0.05);
        if (f == 1000.0) {
            // The first difference adds half a sample of delay at 8x: 0.5 degree.
            const double phase = std::remainder(leak.phase - src.phase, 2.0 * pi) * 180.0 / pi;
            CHECK(std::abs(phase - c.phaseDeg) < 1.0);
        }
    }
}

TEST_CASE("Crosstalk couples in both directions", "[unit][analog]")
{
    // Gate 7: evaluate both directions.
    AnalogOptions o;
    o.crosstalk = true;
    const Out leftDriven = run(o, -6.0, -999.0);
    const Out rightDriven = run(o, -999.0, -6.0);
    const double lr = db(test::fitSine(settled(leftDriven.right), 1000.0, highRate).amplitude
                         / test::fitSine(settled(leftDriven.left), 1000.0, highRate).amplitude);
    const double rl = db(test::fitSine(settled(rightDriven.left), 1000.0, highRate).amplitude
                         / test::fitSine(settled(rightDriven.right), 1000.0, highRate).amplitude);
    CHECK(std::abs(lr + 110.0) < 0.05);
    CHECK(std::abs(rl + 110.0) < 0.05);
}

TEST_CASE("DC offset adds 3 mV to both channels", "[unit][analog]")
{
    AnalogOptions o;
    o.dcOffset = true;
    const Out out = run(o, -999.0, -999.0);
    const double expected = 0.003 / 1.45;
    CHECK(std::abs(out.left.back() - expected) < 1e-15);
    CHECK(std::abs(out.right.back() - expected) < 1e-15);
    CHECK(std::abs(outputVolts(out.left.back()) - 2.503) < 1e-12);
}

TEST_CASE("Power-down is exactly VCOM with every option on", "[unit][analog]")
{
    DacConfig c;
    c.profile = ModelProfile::nominalAnalog;
    c.analog = AnalogOptions::nominal();
    c.analog.dcOffset = true;
    DacCore core;
    REQUIRE(core.prepare(c, 256) == Status::ok);
    std::vector<Pcm20Frame> in(1000, {Pcm20{100000}, Pcm20{-100000}});
    std::vector<double> l(8000), r(8000);
    const std::vector<ControlEvent> events{ControlEvent::powerDown(400, false)};
    REQUIRE(core.process(in, events, {l, r}).status == Status::ok);
    CHECK(std::all_of(l.begin() + 400 * 8, l.end(), [](double v) { return v == 0.0; }));
    CHECK(std::all_of(r.begin() + 400 * 8, r.end(), [](double v) { return v == 0.0; }));
}

TEST_CASE("Every option is independent of block partitioning", "[unit][analog][streaming]")
{
    DacConfig c;
    c.profile = ModelProfile::nominalAnalog;
    c.analog = AnalogOptions::nominal();
    c.analog.dcOffset = true;
    std::vector<Pcm20Frame> in(3000);
    for (std::size_t n = 0; n < in.size(); ++n) {
        in[n] = {Pcm20{static_cast<std::int32_t>(300000 * std::sin(0.07 * static_cast<double>(n)))},
                 Pcm20{static_cast<std::int32_t>(200000 * std::cos(0.05 * static_cast<double>(n)))}};
    }
    auto runBlocks = [&](std::size_t block) {
        DacCore core;
        REQUIRE(core.prepare(c, 256) == Status::ok);
        std::vector<double> l(in.size() * 8), r(l.size());
        for (std::size_t at = 0; at < in.size(); at += block) {
            const std::size_t n = std::min(block, in.size() - at);
            REQUIRE(core.process(std::span(in).subspan(at, n), {},
                                 {std::span(l).subspan(at * 8, n * 8), std::span(r).subspan(at * 8, n * 8)})
                        .status == Status::ok);
        }
        return l;
    };
    CHECK(runBlocks(1) == runBlocks(3000));
}

TEST_CASE("The output stage does not allocate", "[unit][analog][rt]")
{
    DacConfig c;
    c.profile = ModelProfile::nominalAnalog;
    c.analog = AnalogOptions::nominal();
    DacCore core;
    REQUIRE(core.prepare(c, 512) == Status::ok);
    std::vector<Pcm20Frame> in(512, {Pcm20{1000}, Pcm20{-1000}});
    std::vector<double> l(4096), r(4096);
    test::AllocationCounter counter;
    const auto result = core.process(in, {}, {l, r});
    CHECK(counter.allocations() == 0);
    CHECK(result.status == Status::ok);
}

TEST_CASE("Isolation and mismatch hold through the host path with noise", "[dsp][analog]")
{
    // Gate 7: coherent leakage measured with averaging over 10 s, because the
    // 100 dB noise floor would hide 110 dB crosstalk in a broadband reading.
    HostConfig c;
    c.hostRate = 44100.0;
    c.dac.dacRate = 44100.0;
    c.dac.profile = ModelProfile::nominalAnalog;
    c.dac.analog = AnalogOptions::nominal();
    HostProcessor host;
    REQUIRE(host.prepare(c) == Status::ok);
    const std::size_t frames = 441000;
    std::vector<double> left(frames), silent(frames, 0.0), outL(frames), outR(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        left[n] = 0.5 * std::sin(2.0 * pi * 1000.0 * static_cast<double>(n) / 44100.0);
    }
    for (std::size_t at = 0; at < frames; at += 512) {
        const std::size_t n = std::min<std::size_t>(512, frames - at);
        REQUIRE(host.process(StereoView<const double>{std::span(left).subspan(at, n), std::span(silent).subspan(at, n)},
                             StereoView<double>{std::span(outL).subspan(at, n), std::span(outR).subspan(at, n)}, {})
                == Status::ok);
    }
    const auto src = test::fitSine(std::span(outL).subspan(4096), 1000.0, 44100.0);
    const auto leak = test::fitSine(std::span(outR).subspan(4096), 1000.0, 44100.0);
    // Isolation 110 dB (D-A04), adjusted for the fixed mismatch between channels.
    CHECK(std::abs(db(leak.amplitude / src.amplitude) - (-110.0 - 0.2)) < 0.5);
}

TEST_CASE("Voltage scaling and profile descriptions", "[unit][analog]")
{
    static_assert(outputVolts(0.0) == 2.5);
    CHECK(std::abs(outputVolts(1.0) - 3.95) < 1e-12);      // 2.5 + 1.45
    CHECK(std::abs(outputVolts(-1.0) - 1.05) < 1e-12);     // 2.9 Vpp at 5 V
    CHECK(std::abs(outputVolts(1.0, 4.5) - outputVolts(-1.0, 4.5) - 0.58 * 4.5) < 1e-12);
    CHECK(describe(ModelProfile::specification).available);
    CHECK_FALSE(describe(ModelProfile::specification).synthetic);
    CHECK(describe(ModelProfile::nominalAnalog).synthetic);
    CHECK_FALSE(describe(ModelProfile::measured).available);
}
