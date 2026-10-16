// Opt-in extended DAC rates up to 50.4 kHz (REQ-CLOCK-02, P-103). The model
// keeps its normalized-frequency policy, so outside the documented range the
// checks here are of consistency, not of datasheet conformance.

#include "ak4522/dac_core.hpp"
#include "ak4522/dac_stream.hpp"
#include "ak4522/host_processor.hpp"
#include "core/config_validation.hpp"
#include "support/sine_fit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using namespace ak4522;

namespace {

constexpr double pi = 3.14159265358979323846;

DacConfig dacAt(double rate, bool extended)
{
    DacConfig c;
    c.dacRate = rate;
    c.allowExtendedRate = extended;
    return c;
}

// Phase delay in host frames of a sine of `freq` Hz, resolved near `expected`.
double phaseDelay(const std::vector<double>& out, double freq, double host, double expected)
{
    const std::size_t skip = static_cast<std::size_t>(expected) + 2000;
    const auto fit = test::fitSine(std::span(out).subspan(skip), freq, host, static_cast<double>(skip) / host);
    const double period = host / freq;
    double d = -fit.phase / (2.0 * pi) * period;
    d += std::round((expected - d) / period) * period;
    return d;
}

// Special-K's DAC rate: 112 MHz / 2304, about 48 611.1 Hz.
constexpr ExactRate specialK{112000000, 2304};

struct PullSine {
    double freq;
    double rate;
    std::size_t position = 0;

    static void pull(void* context, DacPullBlock& b) noexcept
    {
        auto& s = *static_cast<PullSine*>(context);
        for (std::size_t j = 0; j < b.frames; ++j) {
            const double x = 0.5 * std::sin(2.0 * pi * s.freq * static_cast<double>(s.position + j) / s.rate);
            b.doubles.left[j] = x;
            b.doubles.right[j] = x;
        }
        s.position += b.frames;
    }
};

} // namespace

TEST_CASE("Extended DAC rates need the opt-in", "[unit][clock][extended]")
{
    const double rate = GENERATE(48000.001, 48611.111, 50400.0);
    CAPTURE(rate);
    CHECK(detail::validate(dacAt(rate, false)) == Status::invalidDacRate);
    CHECK(detail::validate(dacAt(rate, true)) == Status::ok);
}

TEST_CASE("The extension is upper side only and stops at 50.4 kHz", "[unit][clock][extended]")
{
    const double rate = GENERATE(50400.001, 52800.0, 96000.0, 15999.999, 8000.0, 0.0,
                                 std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity());
    CAPTURE(rate);
    CHECK(detail::validate(dacAt(rate, true)) == Status::invalidDacRate);
}

TEST_CASE("Latency reports flag rates outside the documented range", "[unit][clock][extended]")
{
    DacCore core;
    REQUIRE(core.prepare(dacAt(48000.0, true), 64) == Status::ok);
    CHECK_FALSE(core.latency().outsideDocumentedRate);
    REQUIRE(core.prepare(dacAt(50400.0, true), 64) == Status::ok);
    CHECK(core.latency().outsideDocumentedRate);

    HostConfig host;
    host.dac = dacAt(49000.0, true);
    HostProcessor p;
    REQUIRE(p.prepare(host) == Status::ok);
    CHECK(p.latency().outsideDocumentedRate);
    host.dac = dacAt(44100.0, true);
    REQUIRE(p.prepare(host) == Status::ok);
    CHECK_FALSE(p.latency().outsideDocumentedRate);
}

TEST_CASE("The core response is the same in normalized frequency", "[unit][core][extended]")
{
    // With de-emphasis and imperfections off, the core's output sequence does
    // not depend on the rate (P-81 applied unchanged, P-103).
    std::vector<Pcm20Frame> in(3000);
    for (std::size_t j = 0; j < in.size(); ++j) {
        const auto code = static_cast<std::int32_t>(200000.0 * std::sin(0.37 * static_cast<double>(j)));
        in[j] = {Pcm20{code}, Pcm20{-code}};
    }
    auto run = [&](double rate, bool extended) {
        DacCore core;
        REQUIRE(core.prepare(dacAt(rate, extended), in.size()) == Status::ok);
        std::vector<double> l(in.size() * DacCore::oversampling), r(l.size());
        REQUIRE(core.process(in, {}, {l, r}).status == Status::ok);
        return l;
    };
    CHECK(run(50400.0, true) == run(44100.0, false));
}

TEST_CASE("HostProcessor at an extended DAC rate keeps level and timing", "[dsp][host][extended]")
{
    const double host = GENERATE(48000.0, 96000.0);
    const double dac = 50400.0;
    CAPTURE(host);
    HostConfig c;
    c.hostRate = host;
    c.dac = dacAt(dac, true);
    HostProcessor p;
    REQUIRE(p.prepare(c) == Status::ok);
    const double expected = p.latency().reportedHostFrames;
    const double edge = 20.0 / 44.1 * std::min(host, dac);
    double sum = 0.0;
    for (int k = 1; k <= 6; ++k) {
        const double freq = edge * k / 7.0;
        p.reset();
        const auto frames = static_cast<std::size_t>(host * 0.25);
        std::vector<double> in(frames), outL(frames), outR(frames);
        for (std::size_t n = 0; n < frames; ++n) {
            in[n] = 0.5 * std::sin(2.0 * pi * freq * static_cast<double>(n) / host);
        }
        for (std::size_t at = 0; at < frames; at += 512) {
            const std::size_t n = std::min<std::size_t>(512, frames - at);
            REQUIRE(p.process(StereoView<const double>{std::span(in).subspan(at, n), std::span(in).subspan(at, n)},
                              StereoView<double>{std::span(outL).subspan(at, n), std::span(outR).subspan(at, n)},
                              {})
                    == Status::ok);
        }
        const std::size_t skip = static_cast<std::size_t>(expected) + 2000;
        const auto fit = test::fitSine(std::span(outL).subspan(skip), freq, host, static_cast<double>(skip) / host);
        const double d = phaseDelay(outL, freq, host, expected);
        CAPTURE(freq, d, fit.amplitude);
        CHECK(std::abs(fit.amplitude - 0.5) < 0.5 * 0.03);
        CHECK(std::abs(d - expected) < 0.02 * std::max(1.0, host / dac));
        sum += d;
    }
    CHECK(std::abs(sum / 6.0 - expected) <= 0.01 * std::max(1.0, host / dac));
}

TEST_CASE("DacStream plays a 112 MHz / 2304 source with the opt-in", "[dsp][stream][extended]")
{
    DacStreamConfig c;
    c.hostRate = 48000.0;
    c.dacRate = specialK;
    c.format = DacSampleFormat::float64;
    PullSine sine{1000.0, specialK.hz()};
    c.pull = &PullSine::pull;
    c.pullContext = &sine;

    DacStream s;
    CHECK(s.prepare(c) == Status::invalidDacRate);
    c.dac.allowExtendedRate = true;
    REQUIRE(s.prepare(c) == Status::ok);
    CHECK(s.latency().outsideDocumentedRate);
    CHECK(s.latency().dacRate == specialK.hz());

    const std::size_t frames = 24000;
    std::vector<double> l(frames), r(frames);
    for (std::size_t at = 0; at < frames; at += 480) {
        REQUIRE(s.render(StereoView<double>{std::span(l).subspan(at, 480), std::span(r).subspan(at, 480)})
                == Status::ok);
    }
    // ceil(24000 * 112e6 / (2304 * 48000)) DAC frames, computed exactly.
    const std::uint64_t num = 24000ull * 112000000ull;
    const std::uint64_t den = 2304ull * 48000ull;
    CHECK(sine.position == (num + den - 1) / den);

    const double expected = s.latency().reportedHostFrames;
    const std::size_t skip = static_cast<std::size_t>(expected) + 2000;
    const auto fit = test::fitSine(std::span(l).subspan(skip), 1000.0, 48000.0, static_cast<double>(skip) / 48000.0);
    CHECK(std::abs(fit.amplitude - 0.5) < 0.5 * 0.03);
    CHECK(std::abs(phaseDelay(l, 1000.0, 48000.0, expected) - expected) < 0.02);
}

TEST_CASE("A 112 MHz / 2304 source plays at high host rates", "[dsp][stream][extended]")
{
    // The synth runs at its own clock; the DAW at 96 or 192 kHz (P-100, P-101, P-103).
    const double host = GENERATE(96000.0, 192000.0);
    CAPTURE(host);
    const double fs = specialK.hz();
    const double edge = 20.0 / 44.1 * fs; // the DAC's own band limits the output
    for (int k = 1; k <= 4; ++k) {
        const double freq = edge * k / 5.0;
        CAPTURE(freq);
        DacStreamConfig c;
        c.hostRate = host;
        c.dacRate = specialK;
        c.format = DacSampleFormat::float64;
        c.dac.allowExtendedRate = true;
        PullSine sine{freq, fs};
        c.pull = &PullSine::pull;
        c.pullContext = &sine;
        DacStream s;
        REQUIRE(s.prepare(c) == Status::ok);

        // Random render sizes, as a DAW's varying buffers.
        std::mt19937_64 rng(static_cast<std::uint64_t>(k));
        std::uniform_int_distribution<std::size_t> size(1, 2048);
        const auto frames = static_cast<std::size_t>(host * 0.4);
        std::vector<double> l(frames), r(frames);
        for (std::size_t at = 0; at < frames;) {
            const std::size_t n = std::min(size(rng), frames - at);
            REQUIRE(s.render(StereoView<double>{std::span(l).subspan(at, n), std::span(r).subspan(at, n)}) == Status::ok);
            at += n;
        }
        // Exactly ceil(T fs / H) DAC frames consumed, in integers.
        const std::uint64_t num = static_cast<std::uint64_t>(frames) * 112000000ull;
        const std::uint64_t den = 2304ull * static_cast<std::uint64_t>(host);
        CHECK(sine.position == (num + den - 1) / den);

        const double expected = s.latency().reportedHostFrames;
        const std::size_t skip = static_cast<std::size_t>(expected) + 4000;
        const auto fit = test::fitSine(std::span(l).subspan(skip), freq, host, static_cast<double>(skip) / host);
        CHECK(std::abs(fit.amplitude - 0.5) < 0.5 * 0.03); // within the +/-0.2 dB response
        CHECK(std::abs(phaseDelay(l, freq, host, expected) - expected) < 0.02 * host / fs);
    }
}
