// Sample-rate converter (REQ-RATE-02, REQ-RATE-04; P-33, P-34, P-37, P-38).
// Quality is measured by least-squares sine fits on the output, independent
// of the converter's implementation.

#include "dsp/resampler.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

using ak4522::Status;
using ak4522::detail::Resampler;
using ak4522::detail::ResamplerSpec;

namespace {

constexpr double pi = 3.14159265358979323846;

struct Fit {
    double amplitude;
    double residualRms; // after removing the fitted sine and DC
};

// Least-squares fit of a + b cos + c sin at a known frequency.
Fit fitSine(std::span<const double> y, double freq, double rate, double startTime)
{
    double s[3][3]{};
    double t[3]{};
    for (std::size_t n = 0; n < y.size(); ++n) {
        const double ph = 2.0 * pi * freq * (startTime + static_cast<double>(n) / rate);
        const double basis[3] = {1.0, std::cos(ph), std::sin(ph)};
        for (int i = 0; i < 3; ++i) {
            t[i] += basis[i] * y[n];
            for (int j = 0; j < 3; ++j) {
                s[i][j] += basis[i] * basis[j];
            }
        }
    }
    // Solve the 3x3 normal equations by Gaussian elimination.
    double a[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            a[i][j] = s[i][j];
        }
        a[i][3] = t[i];
    }
    for (int c = 0; c < 3; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 3; ++r) {
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) {
                pivot = r;
            }
        }
        std::swap(a[c], a[pivot]);
        for (int r = 0; r < 3; ++r) {
            if (r != c) {
                const double f = a[r][c] / a[c][c];
                for (int k = c; k < 4; ++k) {
                    a[r][k] -= f * a[c][k];
                }
            }
        }
    }
    const double coef[3] = {a[0][3] / a[0][0], a[1][3] / a[1][1], a[2][3] / a[2][2]};
    double sq = 0.0;
    for (std::size_t n = 0; n < y.size(); ++n) {
        const double ph = 2.0 * pi * freq * (startTime + static_cast<double>(n) / rate);
        const double e = y[n] - (coef[0] + coef[1] * std::cos(ph) + coef[2] * std::sin(ph));
        sq += e * e;
    }
    return {std::hypot(coef[1], coef[2]), std::sqrt(sq / static_cast<double>(y.size()))};
}

double rms(std::span<const double> y)
{
    double sq = 0.0;
    for (const double v : y) {
        sq += v * v;
    }
    return std::sqrt(sq / static_cast<double>(y.size()));
}

double db(double x)
{
    return 20.0 * std::log10(x);
}

// Resamples a sine of the given frequency; returns the settled output and the
// input time of its first sample.
std::vector<double> resampleSine(Resampler& r, double freq, double inRate, std::size_t outputs,
                                 double& startTime, double amplitude = 0.5)
{
    r.reset();
    const std::size_t settle = r.halfWidth() * 2 + 8; // outputs to skip
    const double ratio = static_cast<double>(r.ratioNumerator()) / static_cast<double>(r.ratioDenominator());
    const auto inputs = static_cast<std::size_t>(static_cast<double>(outputs + settle) * ratio)
        + r.halfWidth() + 16;
    std::vector<double> x(inputs);
    for (std::size_t n = 0; n < inputs; ++n) {
        x[n] = amplitude * std::sin(2.0 * pi * freq * static_cast<double>(n) / inRate);
    }
    std::vector<double> yl(r.maxOutputsFor(inputs)), yr(yl.size());
    const std::size_t produced = r.process(x, x, yl, yr);
    REQUIRE(produced >= settle + outputs);
    // Output m represents input time m * ratio.
    startTime = static_cast<double>(settle) * ratio / inRate;
    return {yl.begin() + static_cast<std::ptrdiff_t>(settle),
            yl.begin() + static_cast<std::ptrdiff_t>(settle + outputs)};
}

struct Pair {
    double in;
    double out;
};

// Input-side pairs (host to DAC) and output-side pairs (8x DAC to host),
// including ratios that need interpolated phases (44 123 Hz).
const std::vector<Pair> qualityPairs{
    {48000, 44100},  {44100, 48000},  {96000, 44100},  {192000, 48000}, {88200, 32000},
    {44100, 16000},  {176400, 37800}, {48000, 44123},  {352800, 44100}, {352800, 48000},
    {384000, 44100}, {256000, 192000}, {302400, 88200}, {352984, 48000},
};

} // namespace

TEST_CASE("Ratios are exact and common ones use exact phases", "[unit][rate]")
{
    Resampler r;
    REQUIRE(r.prepare({44100, 48000}) == Status::ok);
    CHECK(r.ratioNumerator() == 147);
    CHECK(r.ratioDenominator() == 160);
    CHECK(r.exactPhases());
    REQUIRE(r.prepare({352800, 44100}) == Status::ok);
    CHECK(r.ratioNumerator() == 8);
    CHECK(r.ratioDenominator() == 1);
    REQUIRE(r.prepare({48000, 44123}) == Status::ok);
    CHECK_FALSE(r.exactPhases()); // denominator 44123: interpolated phases
}

TEST_CASE("Prepare rejects invalid specifications", "[unit][rate]")
{
    Resampler r;
    CHECK(r.prepare({0, 48000}) == Status::invalidConfiguration);
    CHECK(r.prepare({48000, -1}) == Status::invalidConfiguration);
    CHECK(r.prepare({48000, 44100, 23000, 22000}) == Status::invalidConfiguration);
    CHECK(r.prepare({48000, 44100, 20000, 50000}) == Status::invalidConfiguration);
}

TEST_CASE("Passband error and residual meet the SRC targets", "[dsp][rate]")
{
    const Pair pair = GENERATE(Catch::Generators::from_range(qualityPairs));
    CAPTURE(pair.in, pair.out);
    Resampler r;
    REQUIRE(r.prepare({pair.in, pair.out}) == Status::ok);
    for (int k = 0; k < 12; ++k) {
        const double freq = 50.0 * std::pow(r.passbandHz() / 50.0, k / 11.0);
        double start = 0.0;
        const auto y = resampleSine(r, freq, pair.in, 6000, start);
        const Fit fit = fitSine(y, freq, pair.out, start);
        CAPTURE(freq);
        CHECK(std::abs(db(fit.amplitude / 0.5)) <= 0.005);   // P-33 ripple
        CHECK(db(fit.residualRms) <= -110.0);                 // P-34, dBFS
    }
}

TEST_CASE("Stopband content is rejected by at least 120 dB", "[dsp][rate]")
{
    const Pair pair = GENERATE(Catch::Generators::from_range(qualityPairs));
    CAPTURE(pair.in, pair.out);
    Resampler r;
    REQUIRE(r.prepare({pair.in, pair.out}) == Status::ok);
    // Only input frequencies from the stopband edge to the input Nyquist
    // frequency can be tested; for 44.1 / 48 kHz pairs that range is empty.
    const double top = 0.499 * pair.in;
    if (r.stopbandHz() >= top) {
        SUCCEED("no input frequency lies in the stopband");
        return;
    }
    for (int k = 0; k < 6; ++k) {
        const double freq = r.stopbandHz() + (top - r.stopbandHz()) * (k + 0.5) / 6.0;
        double start = 0.0;
        const auto y = resampleSine(r, freq, pair.in, 4000, start);
        CAPTURE(freq);
        CHECK(db(rms(y) / (0.5 / std::sqrt(2.0))) <= -120.0); // P-33
    }
}

TEST_CASE("Transition-band content folds only above the passband edge", "[dsp][rate]")
{
    // Downsampling: an input tone between the output Nyquist frequency and
    // the stopband edge appears at out - f, which lies above the passband
    // edge by construction (P-38). Nothing else may appear (P-34).
    const Pair pair = GENERATE(Pair{48000, 44100}, Pair{96000, 44100}, Pair{352800, 44100},
                               Pair{352800, 48000}, Pair{48000, 44123});
    CAPTURE(pair.in, pair.out);
    Resampler r;
    REQUIRE(r.prepare({pair.in, pair.out}) == Status::ok);
    const double lo = 0.5 * pair.out;
    const double hi = std::min(r.stopbandHz(), 0.499 * pair.in);
    REQUIRE(hi > lo);
    for (int k = 0; k < 4; ++k) {
        const double freq = lo + (hi - lo) * (k + 0.5) / 4.0;
        const double alias = pair.out - freq;
        CAPTURE(freq, alias);
        CHECK(alias > r.passbandHz());
        double start = 0.0;
        const auto y = resampleSine(r, freq, pair.in, 6000, start);
        // At the output rate the tone is indistinguishable from one at the
        // alias frequency; the fit allows any phase.
        const Fit fit = fitSine(y, alias, pair.out, start);
        CHECK(db(fit.residualRms) <= -110.0);
    }
}

TEST_CASE("Output m represents input time m P / Q exactly", "[unit][rate]")
{
    // The converter's delay is reported from this timing, so a shift of even
    // one input sample must fail. Exact 8:1 ratio: output m is input time 8 m,
    // so an impulse at input 800 peaks at output 100, symmetrically.
    Resampler r;
    REQUIRE(r.prepare({352800, 44100}) == Status::ok);
    std::vector<double> x(4000, 0.0);
    x[800] = 1.0;
    std::vector<double> yl(r.maxOutputsFor(x.size())), yr(yl.size());
    const std::size_t produced = r.process(x, x, yl, yr);
    REQUIRE(produced > 200);
    const auto peak = std::max_element(yl.begin(), yl.begin() + static_cast<std::ptrdiff_t>(produced));
    CHECK(peak - yl.begin() == 100);
    for (std::size_t k = 1; k < 40; ++k) {
        REQUIRE(std::abs(yl[100 + k] - yl[100 - k]) < 1e-15);
    }

    // Non-integer ratio: the fitted phase of a slow sine matches its value at
    // the input times the outputs represent.
    Resampler s;
    REQUIRE(s.prepare({44100, 48000}) == Status::ok);
    const double freq = 1000.0;
    std::vector<double> sine(6000);
    for (std::size_t n = 0; n < sine.size(); ++n) {
        sine[n] = std::sin(2.0 * pi * freq * static_cast<double>(n) / 44100.0);
    }
    std::vector<double> ol(s.maxOutputsFor(sine.size())), orr(ol.size());
    const std::size_t got = s.process(sine, sine, ol, orr);
    double worst = 0.0;
    for (std::size_t m = s.halfWidth() * 2; m + 100 < got; ++m) {
        const double t = static_cast<double>(m) * 147.0 / 160.0 / 44100.0; // input time of output m
        worst = std::max(worst, std::abs(ol[m] - std::sin(2.0 * pi * freq * t)));
    }
    CHECK(worst < 1e-5); // a one-sample shift at 1 kHz would be about 0.14
}

TEST_CASE("DC passes with unity gain", "[unit][rate]")
{
    Resampler r;
    REQUIRE(r.prepare({48000, 44123}) == Status::ok);
    std::vector<double> ones(2000, 1.0);
    std::vector<double> yl(r.maxOutputsFor(ones.size())), yr(yl.size());
    const std::size_t produced = r.process(ones, ones, yl, yr);
    for (std::size_t m = r.halfWidth() + 2; m < produced; ++m) {
        REQUIRE(std::abs(yl[m] - 1.0) < 1e-12);
    }
}

TEST_CASE("Output count is exact over long runs", "[unit][rate]")
{
    // A short kernel keeps the run fast; the count depends only on timing.
    const Pair pair = GENERATE(Pair{48000, 44100}, Pair{44100, 48000}, Pair{352800, 44100},
                               Pair{48000, 44123}, Pair{32000, 192000});
    CAPTURE(pair.in, pair.out);
    Resampler r;
    REQUIRE(r.prepare({pair.in, pair.out, 0, 0, 30.0}) == Status::ok);
    const auto p = r.ratioNumerator();
    const auto q = r.ratioDenominator();
    const auto w = static_cast<std::int64_t>(r.halfWidth());

    std::mt19937_64 rng(3);
    std::uniform_int_distribution<std::size_t> sizes(0, 3000);
    std::vector<double> in(3000, 0.25), outL(r.maxOutputsFor(3000)), outR(outL.size());
    std::int64_t consumed = 0;
    std::int64_t produced = 0;
    while (consumed < 3'000'000) {
        const std::size_t n = sizes(rng);
        produced += static_cast<std::int64_t>(
            r.process(std::span(in).first(n), std::span(in).first(n), outL, outR));
        consumed += static_cast<std::int64_t>(n);
        // Outputs m with floor(m p / q) + W + 1 <= consumed.
        const std::int64_t expected = consumed > w ? ((consumed - w) * q - 1) / p + 1 : 0;
        REQUIRE(produced == expected);
    }
}

TEST_CASE("Output is independent of block partitioning", "[unit][rate][streaming]")
{
    const Pair pair = GENERATE(Pair{48000, 44100}, Pair{44100, 48000}, Pair{48000, 44123});
    CAPTURE(pair.in, pair.out);
    std::mt19937_64 rng(4);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(20000);
    for (double& v : x) {
        v = dist(rng);
    }
    auto run = [&](const std::vector<std::size_t>& sizes) {
        Resampler r;
        REQUIRE(r.prepare({pair.in, pair.out}) == Status::ok);
        std::vector<double> all;
        std::vector<double> ol(r.maxOutputsFor(x.size())), orr(ol.size());
        std::size_t at = 0, block = 0;
        while (at < x.size()) {
            const std::size_t n = std::min(sizes[block++ % sizes.size()], x.size() - at);
            const std::size_t got = r.process(std::span(x).subspan(at, n), std::span(x).subspan(at, n), ol, orr);
            all.insert(all.end(), ol.begin(), ol.begin() + static_cast<std::ptrdiff_t>(got));
            at += n;
        }
        return all;
    };
    const auto reference = run({x.size()});
    CHECK(run({1}) == reference);
    CHECK(run({0, 7, 160, 147, 999}) == reference);
}

TEST_CASE("Reset restarts output timing", "[unit][rate]")
{
    Resampler r;
    REQUIRE(r.prepare({48000, 44100}) == Status::ok);
    std::vector<double> x(500, 0.3), yl(r.maxOutputsFor(500)), yr(yl.size());
    const std::size_t first = r.process(x, x, yl, yr);
    const std::vector<double> firstOut(yl.begin(), yl.begin() + static_cast<std::ptrdiff_t>(first));
    r.reset();
    const std::size_t second = r.process(x, x, yl, yr);
    CHECK(second == first);
    CHECK(std::vector<double>(yl.begin(), yl.begin() + static_cast<std::ptrdiff_t>(second)) == firstOut);
}

TEST_CASE("Channels are independent", "[unit][rate]")
{
    Resampler r;
    REQUIRE(r.prepare({44100, 48000}) == Status::ok);
    std::vector<double> left(400, 0.0), right(400, 0.0);
    left[10] = 1.0;
    std::vector<double> ol(r.maxOutputsFor(400)), orr(ol.size());
    const std::size_t got = r.process(left, right, ol, orr);
    CHECK(std::all_of(orr.begin(), orr.begin() + static_cast<std::ptrdiff_t>(got), [](double v) { return v == 0.0; }));
}

TEST_CASE("Resampling does not allocate", "[unit][rate][rt]")
{
    Resampler r;
    REQUIRE(r.prepare({48000, 44123}) == Status::ok);
    std::vector<double> x(1024, 0.1), ol(r.maxOutputsFor(1024)), orr(ol.size());
    ak4522::test::AllocationCounter counter;
    static_cast<void>(r.process(x, x, ol, orr));
    r.reset();
    CHECK(counter.allocations() == 0);
}
