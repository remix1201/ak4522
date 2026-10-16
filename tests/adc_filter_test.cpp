// ADC decimator and high-pass filter (REQ-ADC-02; AD-03 to AD-07, AD-13,
// I-AD02, I-AD03; P-105 to P-107). Expected values come from the committed
// coefficients through explicit formulas, or from sine fits.

#include "dsp/adc_highpass.hpp"
#include "dsp/decimator.hpp"
#include "support/allocation_counter.hpp"
#include "support/sine_fit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace ak4522;
using detail::AdcHighPass;
using detail::Decimator8x;

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr std::size_t os = Decimator8x::factor;

struct Out {
    std::vector<double> left, right;
};

Out decimate(Decimator8x& d, const std::vector<double>& l, const std::vector<double>& r,
             const std::vector<std::size_t>& frameBlocks = {})
{
    const std::size_t frames = l.size() / os;
    Out o{std::vector<double>(frames), std::vector<double>(frames)};
    std::size_t at = 0, b = 0;
    while (at < frames) {
        const std::size_t n = frameBlocks.empty() ? frames : std::min(frameBlocks[b++ % frameBlocks.size()], frames - at);
        d.process(std::span(l).subspan(at * os, n * os), std::span(r).subspan(at * os, n * os),
                  std::span(o.left).subspan(at, n), std::span(o.right).subspan(at, n));
        at += n;
    }
    return o;
}

std::vector<double> tone(std::size_t samples, double freq, double rate, double amplitude = 0.5)
{
    std::vector<double> x(samples);
    for (std::size_t n = 0; n < samples; ++n) {
        x[n] = amplitude * std::sin(2.0 * pi * freq * static_cast<double>(n) / rate);
    }
    return x;
}

} // namespace

TEST_CASE("Decimator impulse response follows its coefficients and delay", "[unit][adc][filter]")
{
    // An impulse at 8x sample s gives output frame j = h[8j - s - pipeline].
    const std::size_t s = GENERATE(0u, 3u, 7u, 8u, 13u);
    CAPTURE(s);
    std::vector<double> x(os * 100, 0.0);
    x[s] = 1.0;
    Decimator8x d;
    const Out o = decimate(d, x, x);
    const auto h = Decimator8x::coefficients();
    for (std::size_t j = 0; j < o.left.size(); ++j) {
        const auto k = static_cast<long>(os * j) - static_cast<long>(s) - static_cast<long>(Decimator8x::pipelineSamples);
        const double expected = k >= 0 && k < static_cast<long>(h.size()) ? h[static_cast<std::size_t>(k)] : 0.0;
        REQUIRE(o.left[j] == expected);
    }
}

TEST_CASE("Coefficients are symmetric with unity gain at DC", "[unit][adc][filter]")
{
    const auto h = Decimator8x::coefficients();
    double sum = 0.0;
    for (std::size_t k = 0; k < h.size(); ++k) {
        REQUIRE(h[k] == h[h.size() - 1 - k]); // linear phase: zero group-delay distortion (AD-05)
        sum += h[k];
    }
    CHECK(std::abs(20.0 * std::log10(sum)) <= 0.005);
    CHECK(Decimator8x::delaySamples == 234.5); // 29.3125 frames (P-105)
}

TEST_CASE("A passband tone keeps its level and the 29.3-frame delay", "[dsp][adc][filter]")
{
    const double rate = 44100.0;
    const double freq = GENERATE(1000.0, 5000.0, 15000.0, 19000.0);
    CAPTURE(freq);
    const auto x = tone(os * 44100, freq, os * rate);
    Decimator8x d;
    const Out o = decimate(d, x, x);
    const std::size_t skip = 1000;
    const auto fit = test::fitSine(std::span(o.left).subspan(skip), freq, rate, static_cast<double>(skip) / rate);
    CHECK(std::abs(20.0 * std::log10(fit.amplitude / 0.5)) <= 0.005); // AD-03, AD-04
    // out = 0.5 sin(w (t - d)): phase = -w d (mod 2 pi).
    const double period = rate / freq;
    double delay = -fit.phase / (2.0 * pi) * period;
    delay += std::round((29.3125 - delay) / period) * period;
    CHECK(std::abs(delay - 29.3125) < 0.01); // AD-05, P-105
}

TEST_CASE("Content above the stopband edge is rejected by 80 dB", "[dsp][adc][filter]")
{
    // Tones from the stopband edge to near the 8x Nyquist frequency alias into
    // the output band; they must arrive at least 80 dB down (AD-04, I-AD02).
    const double rate = 44100.0;
    const double freq = GENERATE(24400.0, 30000.0, 44100.0 - 1000.0, 66150.0, 150000.0, 176000.0);
    CAPTURE(freq);
    const auto x = tone(os * 22050, freq, os * rate, 1.0);
    Decimator8x d;
    const Out o = decimate(d, x, x);
    double peak = 0.0;
    for (std::size_t j = 1000; j < o.left.size(); ++j) {
        peak = std::max(peak, std::abs(o.left[j]));
    }
    CHECK(20.0 * std::log10(peak) <= -80.0);
}

TEST_CASE("Decimator output does not depend on block sizes", "[unit][adc][filter][streaming]")
{
    std::mt19937_64 rng(3);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<double> l(os * 5000), r(os * 5000);
    for (std::size_t n = 0; n < l.size(); ++n) {
        l[n] = u(rng);
        r[n] = u(rng);
    }
    Decimator8x a, b;
    const Out one = decimate(a, l, r);
    const Out many = decimate(b, l, r, {0, 1, 7, 64, 333, 2});
    CHECK(one.left == many.left);
    CHECK(one.right == many.right);

    Decimator8x c; // channels independent: right zero gives zero
    std::vector<double> zero(l.size(), 0.0);
    const Out separate = decimate(c, l, zero);
    CHECK(std::all_of(separate.right.begin(), separate.right.end(), [](double v) { return v == 0.0; }));
    CHECK(separate.left == one.left);
    c.reset();
    CHECK(decimate(c, l, r).left == one.left);
}

TEST_CASE("High-pass corners match the datasheet points", "[dsp][adc][filter]")
{
    // -3 dB at 0.9 Hz, about -0.46 dB at 2.7 Hz (printed -0.5), -0.1 dB at
    // 6.0 Hz, at 44.1 kHz; frequencies scale with fs (AD-06, I-AD03, P-107).
    struct Point {
        double hzAt44100;
        double db;
        double tolerance;
    };
    const double rate = GENERATE(44100.0, 32000.0, 48000.0);
    const Point p = GENERATE(Point{0.9, -3.0, 0.02}, Point{2.7, -0.5, 0.05}, Point{6.0, -0.1, 0.02},
                             Point{1000.0, 0.0, 0.001});
    CAPTURE(rate, p.hzAt44100);
    const double freq = p.hzAt44100 * rate / 44100.0;
    AdcHighPass hp;
    hp.prepare(rate);
    const double seconds = 12.0 / freq + 4.0;
    auto x = tone(static_cast<std::size_t>(seconds * rate), freq, rate);
    std::vector<double> y = x;
    hp.process(x, y);
    const auto skip = static_cast<std::size_t>(4.0 * rate); // settled
    const auto fit = test::fitSine(std::span(x).subspan(skip), freq, rate, static_cast<double>(skip) / rate);
    CHECK(std::abs(20.0 * std::log10(fit.amplitude / 0.5) - p.db) <= p.tolerance);
}

TEST_CASE("High-pass removes DC to exactly zero", "[unit][adc][filter]")
{
    AdcHighPass hp;
    hp.prepare(44100.0);
    std::vector<double> l(44100 * 30, 0.25), r(44100 * 30, -0.1);
    hp.process(l, r);
    CHECK(l.back() == 0.0);
    CHECK(r.back() == 0.0);
    CHECK(std::abs(hp.cornerHz() - 0.9) < 1e-12);

    // reset() clears the state: after a step, a reset, and silence, output is silent.
    std::vector<double> step(100, 1.0), silence(100, 0.0), s2(100, 0.0);
    hp.process(step, silence);
    hp.reset();
    std::fill(silence.begin(), silence.end(), 0.0);
    hp.process(silence, s2);
    CHECK(std::all_of(silence.begin(), silence.end(), [](double v) { return v == 0.0; }));
    hp.prepare(48000.0);
    CHECK(std::abs(hp.cornerHz() - 0.9 * 48000.0 / 44100.0) < 1e-12);
}

TEST_CASE("ADC filters do not allocate", "[unit][adc][filter][rt]")
{
    Decimator8x d;
    AdcHighPass hp;
    hp.prepare(44100.0);
    std::vector<double> in(os * 256, 0.1), outL(256), outR(256);
    test::AllocationCounter counter;
    d.process(in, in, outL, outR);
    hp.process(outL, outR);
    d.reset();
    hp.reset();
    CHECK(counter.allocations() == 0);
}
