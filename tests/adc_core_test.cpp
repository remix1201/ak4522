// AdcCore, the ADC section (REQ-ADC-01, REQ-ADC-03, REQ-ADC-05; AD-01, AD-05,
// AD-09, AD-10, AD-13, AD-14, AD-19, I-AD04; P-105 to P-108). Timing and
// levels are measured from sine fits, independent of the implementation.

#include "ak4522/adc_core.hpp"
#include "ak4522/dac_core.hpp"
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
constexpr std::size_t os = AdcCore::oversampling;

AdcCore prepared(double rate = 44100.0, std::size_t maxBlock = 512)
{
    AdcCore a;
    AdcConfig c;
    c.adcRate = rate;
    REQUIRE(a.prepare(c, maxBlock) == Status::ok);
    return a;
}

struct Input {
    std::vector<double> left, right;
};

Input tone(std::size_t frames, double freq, double rate, double amplitude = 0.5)
{
    Input in{std::vector<double>(frames * os), std::vector<double>(frames * os)};
    for (std::size_t n = 0; n < in.left.size(); ++n) {
        in.left[n] = amplitude * std::sin(2.0 * pi * freq * static_cast<double>(n) / (os * rate));
        in.right[n] = -in.left[n];
    }
    return in;
}

// Converts in blocks of the given sizes; events are absolute frames.
std::vector<Pcm20Frame> run(AdcCore& a, const Input& in, const std::vector<std::size_t>& sizes,
                            const std::vector<ControlEvent>& absolute = {})
{
    const std::size_t frames = in.left.size() / os;
    std::vector<Pcm20Frame> out(frames);
    std::size_t at = 0, b = 0, next = 0;
    while (at < frames) {
        const std::size_t n = std::min(sizes[b++ % sizes.size()], frames - at);
        std::vector<ControlEvent> local;
        while (next < absolute.size() && absolute[next].offset < at + n) {
            ControlEvent e = absolute[next++];
            e.offset -= static_cast<std::uint32_t>(at);
            local.push_back(e);
        }
        const ProcessResult r = a.process(
            StereoView<const double>{std::span(in.left).subspan(at * os, n * os), std::span(in.right).subspan(at * os, n * os)},
            local, std::span(out).subspan(at, n));
        REQUIRE(r.status == Status::ok);
        REQUIRE(r.framesWritten == n);
        at += n;
    }
    return out;
}

std::vector<double> leftOf(const std::vector<Pcm20Frame>& f, std::size_t from = 0)
{
    std::vector<double> x;
    for (std::size_t i = from; i < f.size(); ++i) {
        x.push_back(toNormalized(f[i].left));
    }
    return x;
}

bool silent(const std::vector<Pcm20Frame>& f, std::size_t from, std::size_t to)
{
    return std::all_of(f.begin() + static_cast<std::ptrdiff_t>(from), f.begin() + static_cast<std::ptrdiff_t>(to),
                       [](const Pcm20Frame& x) { return x.left.code == 0 && x.right.code == 0; });
}

// Phase delay in frames of a fitted sine, resolved near `expected`.
double delayOf(const std::vector<double>& y, std::size_t skip, double freq, double rate, double expected,
               double* amplitude = nullptr)
{
    const auto fit = test::fitSine(std::span(y).subspan(skip), freq, rate, static_cast<double>(skip) / rate);
    if (amplitude) {
        *amplitude = fit.amplitude;
    }
    const double period = rate / freq;
    double d = -fit.phase / (2.0 * pi) * period;
    return d + std::round((expected - d) / period) * period;
}

} // namespace

TEST_CASE("ADC prepare validates rate, clocks, profile, and block size", "[unit][adc]")
{
    AdcCore a;
    AdcConfig c;
    CHECK(a.prepare(c, 64) == Status::ok);
    c.adcRate = 96000.0;
    CHECK(a.prepare(c, 64) == Status::invalidDacRate);
    c.adcRate = 49000.0;
    CHECK(a.prepare(c, 64) == Status::invalidDacRate);
    c.allowExtendedRate = true;
    CHECK(a.prepare(c, 64) == Status::ok);
    c = AdcConfig{};
    c.cmode = CmodePin::high;
    c.masterClockHz = 256.0 * 44100.0;
    CHECK(a.prepare(c, 64) == Status::invalidMasterClock);
    c = AdcConfig{};
    c.profile = ModelProfile::nominalAnalog;
    CHECK(a.prepare(c, 64) == Status::invalidConfiguration); // not built yet
    CHECK(a.prepare(AdcConfig{}, 0) == Status::invalidBlockSize);
    CHECK_FALSE(a.isPrepared());
}

TEST_CASE("Startup outputs zero for 516 frames, then the converted signal", "[unit][adc]")
{
    AdcCore a = prepared();
    const auto in = tone(8000, 1000.0, 44100.0);
    const auto out = run(a, in, {512});
    CHECK(silent(out, 0, 516)); // AD-10, P-108
    CHECK(out[516].left.code != 0);
    double amplitude = 0.0;
    const double d = delayOf(leftOf(out), 2000, 1000.0, 44100.0, 29.3125, &amplitude);
    CHECK(std::abs(d - 29.3125) < 0.01);                      // AD-05, P-105
    CHECK(std::abs(20.0 * std::log10(amplitude / 0.5)) < 0.01); // unity at 1 kHz
    CHECK(std::abs(out[3000].right.code + out[3000].left.code) <= 1); // right is the inverted input
}

TEST_CASE("Zero input gives code 00000h", "[unit][adc]")
{
    AdcCore a = prepared();
    Input zero{std::vector<double>(os * 3000, 0.0), std::vector<double>(os * 3000, 0.0)};
    CHECK(silent(run(a, zero, {700}), 0, 3000)); // AD-09
}

TEST_CASE("Over-range input saturates at full-scale codes and is counted", "[unit][adc]")
{
    AdcCore a = prepared();
    const auto out = run(a, tone(6000, 1000.0, 44100.0, 1.25), {512});
    std::int32_t hi = 0, lo = 0;
    for (std::size_t i = 600; i < out.size(); ++i) {
        hi = std::max(hi, out[i].left.code);
        lo = std::min(lo, out[i].left.code);
    }
    CHECK(hi == Pcm20::maxCode); // 7FFFFh (AD-09)
    CHECK(lo == Pcm20::minCode); // 80000h
    CHECK(a.diagnostics().clippedInputSamples > 0);

    AdcCore b = prepared();
    static_cast<void>(run(b, tone(6000, 1000.0, 44100.0, 0.95), {512}));
    CHECK(b.diagnostics().clippedInputSamples == 0);
}

TEST_CASE("DC at the input is removed by the high-pass filter", "[unit][adc]")
{
    AdcCore a = prepared(44100.0, 4096);
    Input dc{std::vector<double>(os * 44100 * 6, 0.2), std::vector<double>(os * 44100 * 6, -0.1)};
    const auto out = run(a, dc, {4096});
    CHECK(out.back().left.code == 0); // AD-09, AD-06
    CHECK(out.back().right.code == 0);
}

TEST_CASE("PD low silences the output; release restarts the 516-frame initialization", "[unit][adc]")
{
    const auto in = tone(12000, 700.0, 44100.0);
    AdcCore a = prepared();
    const auto out = run(a, in, {333},
                         {ControlEvent::powerDown(4000, false), ControlEvent::powerDown(5000, true)});
    CHECK_FALSE(silent(out, 3000, 4000));
    CHECK(silent(out, 4000, 5000 + 516)); // reset, then initialization (AD-10)
    CHECK(out[5000 + 516].left.code != 0);

    // After release the filters restarted from a cleared state: the same as a
    // fresh core fed the input from the release frame.
    AdcCore fresh = prepared();
    Input tail{std::vector<double>(in.left.begin() + 5000 * os, in.left.end()),
               std::vector<double>(in.right.begin() + 5000 * os, in.right.end())};
    const auto expected = run(fresh, tail, {512});
    for (std::size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(out[5000 + i] == expected[i]);
    }
}

TEST_CASE("DAC pins have no effect on the ADC", "[unit][adc]")
{
    const auto in = tone(5000, 1500.0, 44100.0);
    AdcCore a = prepared();
    AdcCore b = prepared();
    const auto plain = run(a, in, {512});
    const auto withPins = run(b, in, {512},
                              {ControlEvent::softMute(1000, true), ControlEvent::deemphasis(1200, DeemphasisSelect::fs44100)});
    CHECK(plain == withPins);
}

TEST_CASE("ADC output does not depend on block sizes", "[unit][adc][streaming]")
{
    std::mt19937_64 rng(9);
    std::uniform_real_distribution<double> u(-0.9, 0.9);
    Input in{std::vector<double>(os * 6000), std::vector<double>(os * 6000)};
    for (std::size_t n = 0; n < in.left.size(); ++n) {
        in.left[n] = u(rng);
        in.right[n] = u(rng);
    }
    const std::vector<ControlEvent> events{ControlEvent::powerDown(2500, false), ControlEvent::powerDown(2600, true)};
    AdcCore a = prepared(44100.0, 100);
    AdcCore b = prepared(44100.0, 100);
    CHECK(run(a, in, {6000}, events) == run(b, in, {0, 1, 99, 100, 101, 517, 3}, events));
    CHECK(a.diagnostics().chunkedBlocks > 0);
}

TEST_CASE("ADC process rejects bad calls", "[unit][adc]")
{
    AdcCore unprepared;
    std::vector<double> in(os * 4);
    std::vector<Pcm20Frame> out(4), shortOut(3);
    CHECK(unprepared.process({in, in}, {}, out).status == Status::notPrepared);
    AdcCore a = prepared();
    CHECK(a.process({in, std::span<const double>(in).first(8)}, {}, out).status == Status::channelSizeMismatch);
    CHECK(a.process({in, in}, {}, shortOut).status == Status::insufficientOutputCapacity);
    const std::vector<ControlEvent> late{ControlEvent::powerDown(4, false)};
    CHECK(a.process({in, in}, late, out).status == Status::eventOutOfRange);
    CHECK(a.diagnostics().rejectedBlocks == 3);
}

TEST_CASE("Voltage conversion and latency report", "[unit][adc]")
{
    static_assert(adcInputFromVolts(1.5) == 1.0); // 3.0 Vpp full scale at 5 V (I-AD04)
    static_assert(adcInputFromVolts(-0.75) == -0.5);
    CHECK(std::abs(adcInputFromVolts(1.35, 4.5) - 1.0) < 1e-15);
    AdcCore a = prepared(48000.0);
    const AdcLatencyInfo l = a.latency();
    CHECK(l.coreAdcFrames == 29.3125);
    CHECK(l.nominalAdcFrames == 29.3);
    CHECK(l.initializationFrames == 516);
    CHECK(std::abs(l.totalSeconds - 29.3125 / 48000.0) < 1e-15);
    CHECK(a.inputRate() == 384000.0);
}

TEST_CASE("DAC output fed to the ADC returns the signal with both delays", "[dsp][adc][loopback]")
{
    // DacCore outputs at 8x the rate the AdcCore takes in: a direct loopback.
    const double rate = 44100.0;
    const std::size_t frames = 9000;
    std::vector<Pcm20Frame> pcm(frames);
    for (std::size_t j = 0; j < frames; ++j) {
        const auto code = static_cast<std::int32_t>(std::lround(0.5 * 524288.0 * std::sin(2.0 * pi * 997.0 * static_cast<double>(j) / rate)));
        pcm[j] = {Pcm20{code}, Pcm20{code}};
    }
    DacCore dac;
    REQUIRE(dac.prepare(DacConfig{}, frames) == Status::ok);
    Input analog{std::vector<double>(frames * os), std::vector<double>(frames * os)};
    REQUIRE(dac.process(pcm, {}, {analog.left, analog.right}).status == Status::ok);
    AdcCore adc = prepared(rate, frames);
    const auto out = run(adc, analog, {frames});
    double amplitude = 0.0;
    const double d = delayOf(leftOf(out), 3000, 997.0, rate, 44.0125, &amplitude);
    CHECK(std::abs(d - (14.7 + 29.3125)) < 0.02);
    CHECK(std::abs(20.0 * std::log10(amplitude / 0.5)) < 0.05); // both responses near 1 kHz
}

TEST_CASE("ADC processing does not allocate", "[unit][adc][rt]")
{
    AdcCore a = prepared(44100.0, 256);
    std::vector<double> in(os * 1000, 0.1);
    std::vector<Pcm20Frame> out(1000);
    const std::vector<ControlEvent> events{ControlEvent::powerDown(10, false), ControlEvent::powerDown(20, true)};
    test::AllocationCounter counter;
    const ProcessResult r = a.process({in, in}, events, out);
    a.reset();
    static_cast<void>(a.latency());
    static_cast<void>(a.diagnostics());
    CHECK(counter.allocations() == 0);
    CHECK(r.status == Status::ok);
}
