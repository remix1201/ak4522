// De-emphasis (REQ-DEEM-01 to REQ-DEEM-03; D-E01, D-E02, I-E01; P-60 to
// P-64). The frequency-domain fit is checked against the analog reference
// in tools/tests/test_generated_files.py; these tests check the
// implementation against closed-form expressions and the selection rules.

#include "dsp/deemphasis.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

using namespace ak4522;
using detail::Deemphasis;

namespace {

Deemphasis prepared(double rate, DeemphasisSwitching switching = DeemphasisSwitching::resetSection)
{
    Deemphasis d;
    d.prepare(rate, switching);
    return d;
}

std::vector<double> runMono(Deemphasis& d, std::vector<double> x)
{
    std::vector<double> right = x;
    d.process(x, right);
    return x;
}

// Closed-form impulse response of y = b0 x + b1 x[-1] - a1 y[-1]:
// h[0] = b0, h[n] = (b1 - a1 b0) (-a1)^(n-1).
double impulseAt(const Deemphasis::Coefficients& c, std::size_t n)
{
    if (n == 0) {
        return c.b0;
    }
    return (c.b1 - c.a1 * c.b0) * std::pow(-c.a1, static_cast<double>(n - 1));
}

std::vector<double> impulse(std::size_t length)
{
    std::vector<double> x(length, 0.0);
    x[0] = 1.0;
    return x;
}

} // namespace

TEST_CASE("OFF is an exact bypass", "[unit][deemph]")
{
    Deemphasis d = prepared(44100.0);
    d.select(DeemphasisSelect::off, false);
    std::mt19937_64 rng(15);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(1000);
    for (double& v : x) {
        v = dist(rng);
    }
    CHECK(runMono(d, x) == x);
}

TEST_CASE("Each selection's impulse response matches its closed form", "[unit][deemph]")
{
    const DeemphasisSelect set = GENERATE(DeemphasisSelect::fs32000, DeemphasisSelect::fs44100,
                                          DeemphasisSelect::fs48000);
    CAPTURE(static_cast<int>(set));
    Deemphasis d = prepared(44100.0);
    d.select(set, false);
    const auto c = Deemphasis::coefficients(set);
    const auto h = runMono(d, impulse(60));
    for (std::size_t n = 0; n < h.size(); ++n) {
        const double expected = impulseAt(c, n);
        REQUIRE(std::abs(h[n] - expected) <= 1e-14 + 1e-12 * std::abs(expected));
    }
}

TEST_CASE("Sections have unity DC gain and their closed-form Nyquist gain", "[unit][deemph]")
{
    const DeemphasisSelect set = GENERATE(DeemphasisSelect::fs32000, DeemphasisSelect::fs44100,
                                          DeemphasisSelect::fs48000);
    const auto c = Deemphasis::coefficients(set);
    CAPTURE(static_cast<int>(set));
    CHECK(std::abs((c.b0 + c.b1) / (1.0 + c.a1) - 1.0) < 1e-12);
    CHECK(std::abs(c.a1) < 1.0); // stable

    // A step settles to the DC gain of exactly 1.
    Deemphasis d = prepared(44100.0);
    d.select(set, false);
    const auto y = runMono(d, std::vector<double>(400, 1.0));
    CHECK(std::abs(y.back() - 1.0) < 1e-12);

    // Alternating input settles to the closed-form gain at the DAC Nyquist
    // frequency, |b0 - b1| / |1 - a1|. (The fit band ends below Nyquist, so
    // this gain is not constrained to the analog shelf; see the Python fit test.)
    Deemphasis alt = prepared(44100.0);
    alt.select(set, false);
    std::vector<double> x(400);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = (i % 2 == 0) ? 1.0 : -1.0;
    }
    const auto z = runMono(alt, x);
    CHECK(std::abs(std::abs(z.back()) - std::abs((c.b0 - c.b1) / (1.0 - c.a1))) < 1e-12);
}

TEST_CASE("Each selection matches the 50/15 us curve at its own rate", "[unit][deemph]")
{
    // Ties the DEM selection to the right coefficient set (REQ-DEEM-01): each
    // set is within 0.1 dB of the analog reference at its nominal rate
    // (P-61), evaluated from the coefficients with complex arithmetic.
    const auto [set, rate] = GENERATE(table<DeemphasisSelect, double>({
        {DeemphasisSelect::fs32000, 32000.0},
        {DeemphasisSelect::fs44100, 44100.0},
        {DeemphasisSelect::fs48000, 48000.0},
    }));
    CAPTURE(rate);
    const auto c = Deemphasis::coefficients(set);
    const double pi = 3.14159265358979323846;
    const double bandEnd = std::min(20000.0, 0.45 * rate); // P-61
    for (const double f : {100.0, 1000.0, 3183.0, 5000.0, 10000.0, bandEnd}) {
        const std::complex<double> zInv = std::polar(1.0, -2.0 * pi * f / rate);
        const std::complex<double> h = (c.b0 + c.b1 * zInv) / (1.0 + c.a1 * zInv);
        const std::complex<double> s(0.0, 2.0 * pi * f);
        const std::complex<double> reference = (1.0 + s * 15e-6) / (1.0 + s * 50e-6);
        CAPTURE(f);
        CHECK(std::abs(20.0 * std::log10(std::abs(h) / std::abs(reference))) <= 0.1);
    }
}

TEST_CASE("A selection keeps its set at other DAC rates", "[unit][deemph]")
{
    // P-62: the 44.1 kHz set runs unchanged at 32 kHz.
    Deemphasis d = prepared(32000.0);
    d.select(DeemphasisSelect::fs44100, false);
    CHECK(d.active() == DeemphasisSelect::fs44100);
    const auto h = runMono(d, impulse(4));
    CHECK(h[0] == Deemphasis::coefficients(DeemphasisSelect::fs44100).b0);
}

TEST_CASE("Switching resets the new section's state", "[unit][deemph]")
{
    std::mt19937_64 rng(16);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(600);
    for (double& v : x) {
        v = dist(rng);
    }
    Deemphasis d = prepared(44100.0);
    d.select(DeemphasisSelect::fs44100, false);
    std::vector<double> head(x.begin(), x.begin() + 300);
    static_cast<void>(runMono(d, head));
    d.select(DeemphasisSelect::fs48000, false);
    std::vector<double> tail(x.begin() + 300, x.end());
    const auto switched = runMono(d, tail);

    Deemphasis fresh = prepared(44100.0);
    fresh.select(DeemphasisSelect::fs48000, false);
    CHECK(switched == runMono(fresh, tail));
}

TEST_CASE("Selecting the set already in use keeps its state", "[unit][deemph]")
{
    Deemphasis d = prepared(44100.0);
    d.select(DeemphasisSelect::fs44100, false);
    static_cast<void>(runMono(d, std::vector<double>(50, 1.0)));
    d.select(DeemphasisSelect::fs44100, false); // no change: no reset
    const auto y = runMono(d, std::vector<double>(1, 1.0));
    CHECK(std::abs(y[0] - 1.0) < 1e-6); // still settled at DC
}

TEST_CASE("Crossfade length is 5 ms", "[unit][deemph]")
{
    CHECK(Deemphasis::crossfadeFrames(32000.0) == 160);
    CHECK(Deemphasis::crossfadeFrames(44100.0) == 221); // 220.5 rounds away from zero
    CHECK(Deemphasis::crossfadeFrames(48000.0) == 240);
}

TEST_CASE("Host crossfade blends old and new sections linearly", "[unit][deemph]")
{
    const double rate = 32000.0;
    const std::size_t n = Deemphasis::crossfadeFrames(rate);
    std::mt19937_64 rng(17);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(400 + n + 50);
    for (double& v : x) {
        v = dist(rng);
    }
    const std::vector<double> head(x.begin(), x.begin() + 400);
    const std::vector<double> tail(x.begin() + 400, x.end());

    Deemphasis faded = prepared(rate, DeemphasisSwitching::hostCrossfade);
    faded.select(DeemphasisSelect::fs44100, false);
    static_cast<void>(runMono(faded, head));
    faded.select(DeemphasisSelect::off, true);
    REQUIRE(faded.crossfading());
    const auto y = runMono(faded, tail);
    CHECK_FALSE(faded.crossfading());

    // Old path: the 44.1 kHz section continuing from its state.
    Deemphasis old = prepared(rate);
    old.select(DeemphasisSelect::fs44100, false);
    static_cast<void>(runMono(old, head));
    const auto oldOut = runMono(old, tail);

    for (std::size_t i = 0; i < tail.size(); ++i) {
        const double g = i < n ? static_cast<double>(i + 1) / static_cast<double>(n) : 1.0;
        const double expected = g * tail[i] + (1.0 - g) * oldOut[i]; // new section is OFF
        REQUIRE(std::abs(y[i] - expected) < 1e-15);
    }
    CHECK(y[n - 1] == tail[n - 1]); // fully the new section at the last fade frame
}

TEST_CASE("Crossfade is suppressed when not allowed or in reset mode", "[unit][deemph]")
{
    Deemphasis hostMode = prepared(44100.0, DeemphasisSwitching::hostCrossfade);
    hostMode.select(DeemphasisSelect::fs44100, false);
    CHECK_FALSE(hostMode.crossfading());

    Deemphasis chipMode = prepared(44100.0);
    chipMode.select(DeemphasisSelect::fs44100, true);
    CHECK_FALSE(chipMode.crossfading());
}

TEST_CASE("Reset clears state and ends a crossfade", "[unit][deemph]")
{
    Deemphasis d = prepared(48000.0, DeemphasisSwitching::hostCrossfade);
    d.select(DeemphasisSelect::fs48000, false);
    static_cast<void>(runMono(d, std::vector<double>(100, 0.8)));
    d.select(DeemphasisSelect::fs32000, true);
    d.reset();
    CHECK_FALSE(d.crossfading());
    CHECK(d.active() == DeemphasisSelect::fs32000);
    const auto y = runMono(d, std::vector<double>(20, 0.0));
    for (const double v : y) {
        CHECK(v == 0.0);
    }
}

TEST_CASE("De-emphasis is independent of block partitioning", "[unit][deemph][streaming]")
{
    std::mt19937_64 rng(18);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(3000);
    for (double& v : x) {
        v = dist(rng);
    }
    // Selection changes at fixed frames, with a crossfade spanning blocks.
    const std::vector<std::pair<std::size_t, DeemphasisSelect>> changes{
        {0, DeemphasisSelect::fs44100}, {700, DeemphasisSelect::off},
        {800, DeemphasisSelect::fs32000}, {2000, DeemphasisSelect::fs48000}};

    auto run = [&](const std::vector<std::size_t>& sizes) {
        Deemphasis d = prepared(44100.0, DeemphasisSwitching::hostCrossfade);
        std::vector<double> left = x;
        std::vector<double> right = x;
        std::size_t at = 0;
        std::size_t next = 0;
        std::size_t block = 0;
        while (at < x.size()) {
            while (next < changes.size() && changes[next].first == at) {
                d.select(changes[next++].second, at != 0);
            }
            std::size_t len = std::min(sizes[block++ % sizes.size()], x.size() - at);
            if (next < changes.size() && changes[next].first < at + len) {
                len = changes[next].first - at;
            }
            d.process(std::span(left).subspan(at, len), std::span(right).subspan(at, len));
            at += len;
        }
        return left;
    };
    const auto reference = run({x.size()});
    CHECK(run({1}) == reference);
    CHECK(run({0, 5, 100, 221, 13}) == reference);
}

TEST_CASE("De-emphasis does not allocate and keeps channels separate", "[unit][deemph][rt]")
{
    Deemphasis d = prepared(44100.0, DeemphasisSwitching::hostCrossfade);
    d.select(DeemphasisSelect::fs44100, false);
    std::vector<double> left(512, 0.0);
    std::vector<double> right(512, 0.0);
    left[0] = 1.0;
    ak4522::test::AllocationCounter counter;
    d.process(left, right);
    d.select(DeemphasisSelect::fs48000, true);
    d.process(left, right);
    d.reset();
    CHECK(counter.allocations() == 0);
    for (const double v : right) {
        CHECK(v == 0.0);
    }
}
