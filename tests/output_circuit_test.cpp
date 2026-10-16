// The datasheet's example output circuit, Fig.11 (REQ-ANALOG-02; D-O02,
// I-O02, I-O03, DISC-05; P-104). Expected responses come from the analog
// transfer function of the drawn components, not from the implementation.

#include "ak4522/output_circuit.hpp"
#include "support/allocation_counter.hpp"
#include "support/sine_fit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

using namespace ak4522;

namespace {

constexpr double pi = 3.14159265358979323846;

// Analog reference: g * HP(f, f1) * HP(f, f2), HP = jf/fc / (1 + jf/fc).
std::complex<double> reference(double f, double g, double f1, double f2)
{
    const std::complex<double> j{0.0, 1.0};
    const auto hp = [&](double fc) { return (j * f / fc) / (1.0 + j * f / fc); };
    return g * hp(f1) * hp(f2);
}

// Measured complex response at f from a settled sine.
std::complex<double> measure(ExampleOutputCircuit& c, double f, double rate)
{
    c.reset();
    const double settle = 20.0 / std::min(f, 0.589); // many time constants
    const auto total = static_cast<std::size_t>((settle + 20.0 / f) * rate);
    std::vector<double> l(total), r(total);
    for (std::size_t n = 0; n < total; ++n) {
        l[n] = r[n] = std::sin(2.0 * pi * f * static_cast<double>(n) / rate);
    }
    c.process(StereoView<double>{l, r});
    const auto skip = static_cast<std::size_t>(settle * rate);
    const auto fit = test::fitSine(std::span(l).subspan(skip), f, rate, static_cast<double>(skip) / rate);
    return std::polar(fit.amplitude, fit.phase);
}

ExampleOutputCircuit prepared(double rate, ExampleCircuitOptions o = {})
{
    ExampleOutputCircuit c;
    REQUIRE(c.prepare(rate, o) == Status::ok);
    return c;
}

} // namespace

TEST_CASE("Corners and gain follow from the drawn components", "[unit][analog][circuit]")
{
    ExampleOutputCircuit c = prepared(48000.0);
    // 1 / (2 pi 10k 27u) and 1 / (2 pi 27k 10u): both 0.58946 Hz (I-O03).
    CHECK(std::abs(c.inputCornerHz() - 0.589463) < 1e-5);
    CHECK(std::abs(c.outputCornerHz() - 0.589463) < 1e-5);
    CHECK(c.gain() == -1.8); // -18k / 10k (I-O02)
    CHECK(std::abs(20.0 * std::log10(1.8) - 5.105) < 0.001); // not 6 dB (DISC-05)

    ExampleOutputCircuit loaded = prepared(48000.0, {true, true, 27e3}); // load halves 27k
    CHECK(std::abs(loaded.outputCornerHz() - 2.0 * 0.589463) < 1e-5);
    CHECK(prepared(48000.0, {false, true, 0.0}).gain() == -1.0);
    CHECK(prepared(48000.0, {true, false, 0.0}).gain() == 1.8);
    CHECK(prepared(48000.0, {false, false, 0.0}).gain() == 1.0);
}

TEST_CASE("Response matches the analog circuit", "[dsp][analog][circuit]")
{
    struct Point {
        double f;
        double rate;
    };
    const Point p = GENERATE(Point{0.2, 1000.0}, Point{0.589, 1000.0}, Point{2.0, 1000.0}, Point{20.0, 1000.0},
                             Point{20.0, 48000.0}, Point{1000.0, 48000.0}, Point{15000.0, 48000.0},
                             Point{40000.0, 192000.0});
    const double load = GENERATE(0.0, 10e3);
    CAPTURE(p.f, p.rate, load);
    ExampleOutputCircuit c = prepared(p.rate, {true, true, load});
    const auto expected = reference(p.f, -1.8, c.inputCornerHz(), c.outputCornerHz());
    const auto got = measure(c, p.f, p.rate);
    const double dbError = 20.0 * std::log10(std::abs(got) / std::abs(expected));
    const double phaseError = std::remainder(std::arg(got) - std::arg(expected), 2.0 * pi) * 180.0 / pi;
    CAPTURE(dbError, phaseError);
    CHECK(std::abs(dbError) < 0.01);
    CHECK(std::abs(phaseError) < 0.5);
}

TEST_CASE("In the audio band the circuit is a gain of -1.8", "[unit][analog][circuit]")
{
    ExampleOutputCircuit c = prepared(48000.0);
    std::vector<double> x(48000), l(48000), r(48000);
    for (std::size_t n = 0; n < x.size(); ++n) {
        x[n] = l[n] = r[n] = 0.5 * std::sin(2.0 * pi * 1000.0 * static_cast<double>(n) / 48000.0);
    }
    c.process(StereoView<double>{l, r});
    double worst = 0.0;
    for (std::size_t n = 24000; n < x.size(); ++n) {
        worst = std::max(worst, std::abs(l[n] - (-1.8 * x[n])));
    }
    // The two corners at 0.59 Hz shift 1 kHz by about 0.07 degrees.
    CHECK(worst < 0.5 * 1.8 * 0.0015);
}

TEST_CASE("DC and offsets are removed", "[unit][analog][circuit]")
{
    ExampleOutputCircuit c = prepared(1000.0);
    std::vector<double> l(60000, 0.01), r(60000, -0.002); // offsets of 14.5 and -2.9 mV
    c.process(StereoView<double>{l, r});
    CHECK(l.back() == 0.0); // decayed, then flushed
    CHECK(r.back() == 0.0);
    CHECK(std::abs(l.front() - (-1.8 * 0.01)) < 1e-4); // the step passes at first
}

TEST_CASE("Channels are independent; float matches double; reset clears", "[unit][analog][circuit]")
{
    ExampleOutputCircuit c = prepared(48000.0);
    std::vector<double> l(2000, 0.0), r(2000, 0.0);
    l[0] = 1.0;
    c.process(StereoView<double>{l, r});
    CHECK(std::all_of(r.begin(), r.end(), [](double v) { return v == 0.0; }));

    c.reset();
    std::vector<double> dl(2000), dr(2000);
    std::vector<float> fl(2000), fr(2000);
    for (std::size_t n = 0; n < dl.size(); ++n) {
        dl[n] = dr[n] = 0.25 * std::sin(0.03 * static_cast<double>(n));
        fl[n] = fr[n] = static_cast<float>(dl[n]);
    }
    c.process(StereoView<double>{dl, dr});
    c.reset();
    c.process(StereoView<float>{fl, fr});
    for (std::size_t n = 0; n < dl.size(); ++n) {
        REQUIRE(std::abs(static_cast<double>(fl[n]) - dl[n]) < 1e-6);
    }
}

TEST_CASE("Circuit prepare rejects invalid options", "[unit][analog][circuit]")
{
    ExampleOutputCircuit c;
    CHECK(c.prepare(0.0) == Status::invalidConfiguration);
    CHECK(c.prepare(std::nan("")) == Status::invalidConfiguration);
    CHECK(c.prepare(48000.0, {true, true, -1.0}) == Status::invalidConfiguration);
    CHECK(c.prepare(48000.0, {true, true, 0.001}) == Status::invalidConfiguration); // corner far above the band
    CHECK(c.prepare(48000.0, {true, true, 600.0}) == Status::ok);
}

TEST_CASE("The circuit does not allocate", "[unit][analog][circuit][rt]")
{
    ExampleOutputCircuit c = prepared(48000.0);
    std::vector<float> l(512, 0.1f), r(512, 0.1f);
    test::AllocationCounter counter;
    c.process(StereoView<float>{l, r});
    c.reset();
    CHECK(counter.allocations() == 0);
}
