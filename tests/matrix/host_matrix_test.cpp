// Host-rate matrix verification (M3 gate: REQ-RATE-01 to REQ-RATE-04,
// REQ-RT-01, REQ-LAT-03; P-33, P-34, P-38, P-39, P-84). Every supported host
// rate against DAC rates 16, 32, 37.8, 44.1, and 48 kHz.

#include "ak4522/host_processor.hpp"
#include "support/allocation_counter.hpp"
#include "support/multi_sine_fit.hpp"
#include "support/sine_fit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace ak4522;

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr std::size_t os = 8;

struct Rates {
    double host;
    double dac;
};

std::vector<Rates> allPairs()
{
    std::vector<Rates> pairs;
    for (const double host : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
        for (const double dac : {16000.0, 32000.0, 37800.0, 44100.0, 48000.0}) {
            pairs.push_back({host, dac});
        }
    }
    return pairs;
}

HostProcessor prepared(Rates r, std::size_t maxBlock = 512)
{
    HostConfig c;
    c.hostRate = r.host;
    c.dac.dacRate = r.dac;
    c.maxBlockFrames = maxBlock;
    HostProcessor p;
    REQUIRE(p.prepare(c) == Status::ok);
    return p;
}

std::vector<double> process(HostProcessor& p, const std::vector<double>& in, std::size_t block = 512)
{
    std::vector<double> out(in.size());
    std::vector<double> right(in.size());
    for (std::size_t at = 0; at < in.size(); at += block) {
        const std::size_t n = std::min(block, in.size() - at);
        REQUIRE(p.process(StereoView<const double>{std::span(in).subspan(at, n), std::span(in).subspan(at, n)},
                          StereoView<double>{std::span(out).subspan(at, n), std::span(right).subspan(at, n)}, {})
                == Status::ok);
    }
    return out;
}

std::vector<double> tones(std::size_t frames, double rate, const std::vector<double>& freqs, double amplitude)
{
    std::vector<double> x(frames, 0.0);
    for (std::size_t n = 0; n < frames; ++n) {
        for (const double f : freqs) {
            x[n] += amplitude * std::sin(2.0 * pi * f * static_cast<double>(n) / rate);
        }
    }
    return x;
}

// Frequencies the design intends to produce at the host output for an input
// tone f (P-38 transition-band fold-over, O-01 DAC images). Everything else
// is unintended and must stay below -110 dBFS (P-34).
std::vector<double> intendedOutputs(double f, Rates r)
{
    const double fpIn = 20.0 / 44.1 * std::min(r.host, r.dac);
    const double stopIn = std::min(r.host, r.dac) - fpIn;
    std::vector<double> dacBand{f};
    if (r.host > r.dac && f > r.dac / 2.0 && f < stopIn) {
        dacBand = {r.dac - f}; // input converter fold-over into its transition band
    } else if (r.host > r.dac && f >= stopIn) {
        dacBand = {};          // rejected by the input converter
    } else if (r.host < r.dac && f > fpIn) {
        dacBand.push_back(r.host - f); // input converter image in its transition band
    }

    std::vector<double> core; // DAC output at 8x: signal and its images
    for (const double b : dacBand) {
        core.push_back(b);
        for (int k = 1; k <= 4; ++k) {
            core.push_back(k * r.dac - b);
            core.push_back(k * r.dac + b);
        }
    }

    const double high = os * r.dac;
    const double fpOut = 20.0 / 44.1 * std::min(high, r.host);
    const double stopOut = std::min(high, r.host) - fpOut;
    std::vector<double> out;
    for (const double c : core) {
        if (c <= 0.0 || c >= high / 2.0) {
            continue;
        }
        if (c < r.host / 2.0) {
            out.push_back(c);
        }
        if (high > r.host && c > r.host / 2.0 && c < stopOut) {
            double alias = std::fmod(c, r.host);
            if (alias > r.host / 2.0) {
                alias = r.host - alias;
            }
            out.push_back(alias); // output converter fold-over into its transition band
        }
        if (high < r.host && c > fpOut && high - c < r.host / 2.0) {
            out.push_back(high - c); // output converter image in its transition band
        }
    }
    // Drop components at DC or Nyquist and merge near-duplicates, which the
    // fit cannot separate.
    std::sort(out.begin(), out.end());
    std::vector<double> unique;
    for (const double v : out) {
        if (v < 20.0 || v > r.host / 2.0 - 20.0) {
            continue;
        }
        if (unique.empty() || v - unique.back() > 20.0) {
            unique.push_back(v);
        }
    }
    return unique;
}

double db(double x)
{
    return 20.0 * std::log10(x);
}

} // namespace

TEST_CASE("Every pair prepares and aligns within 0.01 frame", "[matrix]")
{
    const Rates r = GENERATE(Catch::Generators::from_range(allPairs()));
    CAPTURE(r.host, r.dac);
    HostProcessor p = prepared(r);
    const double expected = p.latency().reportedHostFrames;
    const double edge = 20.0 / 44.1 * std::min(r.host, r.dac);
    double sum = 0.0;
    for (int k : {1, 3, 5}) {
        const double f = edge * k / 7.0;
        p.reset();
        const auto out = process(p, tones(static_cast<std::size_t>(r.host / 4), r.host, {f}, 0.5));
        const std::size_t skip = static_cast<std::size_t>(expected) + 2000;
        const auto fit = test::fitSine(std::span(out).subspan(skip), f, r.host, static_cast<double>(skip) / r.host);
        const double period = r.host / f;
        double d = -fit.phase / (2.0 * pi) * period;
        d += std::round((expected - d) / period) * period;
        sum += d;
    }
    CHECK(std::abs(sum / 3.0 - expected) <= 0.01 * std::max(1.0, r.host / r.dac));
}

TEST_CASE("Long runs keep exact counts and constant timing", "[matrix]")
{
    // Four seconds in random blocks: no underruns, and the phase of a steady
    // tone is the same at the start and the end (no drift, REQ-RATE-02).
    const Rates r = GENERATE(Catch::Generators::from_range(allPairs()));
    CAPTURE(r.host, r.dac);
    HostProcessor p = prepared(r, 256);
    const double f = 997.0;
    const auto in = tones(static_cast<std::size_t>(r.host * 4.0), r.host, {f}, 0.5);
    std::vector<double> out(in.size()), right(in.size());
    std::mt19937_64 rng(static_cast<std::uint64_t>(r.host + r.dac));
    std::uniform_int_distribution<std::size_t> sizes(0, 1500);
    for (std::size_t at = 0; at < in.size();) {
        const std::size_t n = std::min(sizes(rng), in.size() - at);
        REQUIRE(p.process(StereoView<const double>{std::span(in).subspan(at, n), std::span(in).subspan(at, n)},
                          StereoView<double>{std::span(out).subspan(at, n), std::span(right).subspan(at, n)}, {})
                == Status::ok);
        at += n;
    }
    CHECK(p.diagnostics().hostUnderruns == 0);
    const std::size_t window = static_cast<std::size_t>(r.host / 4);
    const std::size_t early = static_cast<std::size_t>(r.host / 2);
    const std::size_t late = out.size() - window;
    const auto a = test::fitSine(std::span(out).subspan(early, window), f, r.host, static_cast<double>(early) / r.host);
    const auto b = test::fitSine(std::span(out).subspan(late, window), f, r.host, static_cast<double>(late) / r.host);
    // Drift would show as a phase change. The amplitude estimate jitters by a
    // few 1e-6 dB from the 20-bit quantization noise (about -125 dBFS), so
    // its tolerance sits above that floor.
    CHECK(std::abs(std::remainder(a.phase - b.phase, 2.0 * pi)) < 1e-6);
    CHECK(std::abs(db(a.amplitude / b.amplitude)) < 1e-4);
}

TEST_CASE("Tones near each Nyquist frequency produce no unintended aliases", "[matrix]")
{
    const Rates r = GENERATE(Catch::Generators::from_range(allPairs()));
    CAPTURE(r.host, r.dac);
    HostProcessor p = prepared(r);
    std::vector<std::vector<double>> stimuli;
    for (const double x : {0.40, 0.45, 0.49}) {
        stimuli.push_back({x * r.dac}); // near the DAC Nyquist frequency
    }
    for (const double x : {0.45, 0.49}) {
        stimuli.push_back({x * r.host}); // near the host Nyquist frequency
    }
    stimuli.push_back({0.3 * r.dac, 0.47 * r.dac}); // two-tone
    for (const auto& freqs : stimuli) {
        CAPTURE(freqs);
        std::vector<double> expected;
        for (const double f : freqs) {
            const auto e = intendedOutputs(f, r);
            expected.insert(expected.end(), e.begin(), e.end());
        }
        std::sort(expected.begin(), expected.end());
        expected.erase(std::unique(expected.begin(), expected.end(),
                                   [](double a, double b) { return b - a <= 20.0; }),
                       expected.end());
        p.reset();
        const double amplitude = 0.5 / static_cast<double>(freqs.size());
        const auto out = process(p, tones(static_cast<std::size_t>(r.host / 2), r.host, freqs, amplitude));
        const std::size_t skip = p.latency().reportedHostFrames + 4000;
        const auto fit = test::fitSines(std::span(out).subspan(skip), expected, r.host,
                                        static_cast<double>(skip) / r.host);
        CAPTURE(expected, db(fit.residualRms));
        CHECK(db(fit.residualRms) <= -110.0); // P-34
    }
}

TEST_CASE("Every pair is independent of block partitioning", "[matrix]")
{
    const Rates r = GENERATE(Catch::Generators::from_range(allPairs()));
    CAPTURE(r.host, r.dac);
    const auto in = tones(static_cast<std::size_t>(r.host / 10), r.host, {1234.0, 0.4 * r.dac}, 0.3);
    HostProcessor a = prepared(r);
    HostProcessor b = prepared(r, 64);
    const auto whole = process(a, in, in.size());
    CHECK(process(b, in, 37) == whole);
}

TEST_CASE("Every pair processes without allocating", "[matrix]")
{
    const Rates r = GENERATE(Catch::Generators::from_range(allPairs()));
    CAPTURE(r.host, r.dac);
    HostProcessor p = prepared(r, 1024);
    std::vector<double> in(4096, 0.1), out(4096), right(4096);
    const std::vector<ControlEvent> events{ControlEvent::softMute(100, true),
                                           ControlEvent::deemphasis(200, DeemphasisSelect::fs44100)};
    test::AllocationCounter counter;
    const Status s = p.process(StereoView<const double>{in, in}, StereoView<double>{out, right}, events);
    p.reset();
    CHECK(counter.allocations() == 0);
    CHECK(s == Status::ok);
}
