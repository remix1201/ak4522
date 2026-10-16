// 8x interpolator (REQ-FILTER-01; D-02; P-87, P-88). The frequency-domain
// gates are measured by tools/analyze_response.py on this implementation's
// impulse response (CTest dsp.interpolator_response); these tests cover the
// time-domain contract.

#include "dsp/interpolator.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using ak4522::detail::Interpolator8x;

namespace {

constexpr std::size_t factor = Interpolator8x::factor;

struct Output {
    std::vector<double> left;
    std::vector<double> right;
};

Output run(Interpolator8x& interpolator, const std::vector<double>& left,
           const std::vector<double>& right)
{
    Output out{std::vector<double>(left.size() * factor), std::vector<double>(right.size() * factor)};
    interpolator.process(left, right, out.left, out.right);
    return out;
}

} // namespace

TEST_CASE("Coefficients are symmetric and fully populated", "[unit][filter]")
{
    const auto h = Interpolator8x::coefficients();
    for (std::size_t n = 0; n < h.size(); ++n) {
        REQUIRE(h[n] == h[h.size() - 1 - n]);
    }
    // A short coefficient file would leave zeros at the end of the array.
    CHECK(h.front() != 0.0);
    CHECK(h.back() != 0.0);
    CHECK(Interpolator8x::delaySamples == 95);
}

TEST_CASE("Impulse response equals the designed coefficients", "[unit][filter]")
{
    Interpolator8x interpolator;
    std::vector<double> input(Interpolator8x::tapsPerPhase + 2, 0.0);
    input[0] = 1.0;
    const Output out = run(interpolator, input, input);
    const auto h = Interpolator8x::coefficients();
    for (std::size_t n = 0; n < out.left.size(); ++n) {
        const double expected = n < h.size() ? h[n] : 0.0;
        REQUIRE(out.left[n] == expected);
        REQUIRE(out.right[n] == expected);
    }
}

TEST_CASE("The impulse response peaks at the designed delay", "[unit][filter]")
{
    const auto h = Interpolator8x::coefficients();
    const auto peak = std::max_element(h.begin(), h.end(),
                                       [](double a, double b) { return std::abs(a) < std::abs(b); });
    CHECK(static_cast<std::size_t>(peak - h.begin()) == Interpolator8x::delaySamples);
}

TEST_CASE("DC input settles to unity with bounded image ripple", "[unit][filter]")
{
    // A DC input has images at 1 to 7 times fs, each at most the stopband
    // level, so settled samples repeat an 8-sample pattern around the DC gain.
    Interpolator8x interpolator;
    const std::vector<double> ones(200, 1.0);
    const Output out = run(interpolator, ones, ones);
    const double rippleLimit = std::pow(10.0, 0.06 / 20.0) - 1.0;     // D-F04
    const double imageLimit = 7.0 * std::pow(10.0, -43.0 / 20.0);    // D-F05

    const std::size_t settled = Interpolator8x::taps + factor;
    double mean = 0.0;
    for (std::size_t p = 0; p < factor; ++p) {
        mean += out.left[settled + p] / static_cast<double>(factor);
    }
    CHECK(std::abs(mean - 1.0) <= rippleLimit);
    for (std::size_t n = settled; n < out.left.size(); ++n) {
        REQUIRE(std::abs(out.left[n] - mean) <= imageLimit);
        REQUIRE(out.left[n] == out.left[n - factor]); // periodic once settled
    }
}

TEST_CASE("Channels are independent", "[unit][filter]")
{
    Interpolator8x interpolator;
    std::vector<double> left(64, 0.0);
    std::vector<double> right(64, 0.0);
    left[3] = 1.0;
    const Output out = run(interpolator, left, right);
    CHECK(std::all_of(out.right.begin(), out.right.end(), [](double v) { return v == 0.0; }));
    CHECK(std::any_of(out.left.begin(), out.left.end(), [](double v) { return v != 0.0; }));
}

TEST_CASE("Output is independent of block partitioning", "[unit][filter][streaming]")
{
    std::mt19937_64 rng(12);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> left(3000);
    std::vector<double> right(3000);
    for (std::size_t i = 0; i < left.size(); ++i) {
        left[i] = dist(rng);
        right[i] = dist(rng);
    }

    auto partitioned = [&](const std::vector<std::size_t>& sizes) {
        Interpolator8x interpolator;
        Output out{std::vector<double>(left.size() * factor), std::vector<double>(right.size() * factor)};
        std::size_t frame = 0;
        std::size_t block = 0;
        while (frame < left.size()) {
            const std::size_t n = std::min(sizes[block++ % sizes.size()], left.size() - frame);
            interpolator.process(std::span(left).subspan(frame, n), std::span(right).subspan(frame, n),
                                 std::span(out.left).subspan(frame * factor, n * factor),
                                 std::span(out.right).subspan(frame * factor, n * factor));
            frame += n;
        }
        return out;
    };

    const Output reference = partitioned({left.size()});
    for (const auto& sizes : std::vector<std::vector<std::size_t>>{{1}, {0, 1, 7, 64, 500}, {23, 0, 2}}) {
        const Output out = partitioned(sizes);
        CHECK(out.left == reference.left);
        CHECK(out.right == reference.right);
    }
}

TEST_CASE("Reset clears the history", "[unit][filter]")
{
    Interpolator8x interpolator;
    std::vector<double> noise(40, 0.7);
    static_cast<void>(run(interpolator, noise, noise));
    interpolator.reset();
    const std::vector<double> zeros(40, 0.0);
    const Output out = run(interpolator, zeros, zeros);
    CHECK(std::all_of(out.left.begin(), out.left.end(), [](double v) { return v == 0.0; }));
    CHECK(std::all_of(out.right.begin(), out.right.end(), [](double v) { return v == 0.0; }));
}

TEST_CASE("Full-scale alternating input stays bounded", "[unit][filter]")
{
    // Worst case for an FIR: input signs matching each phase's coefficient
    // signs reach the sum of absolute coefficients of that phase.
    Interpolator8x interpolator;
    std::vector<double> input(400);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = (i % 2 == 0) ? 1.0 : -1.0;
    }
    const Output out = run(interpolator, input, input);
    double bound = 0.0;
    for (const double c : Interpolator8x::coefficients()) {
        bound += std::abs(c);
    }
    for (const double v : out.left) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) <= bound);
    }
}

TEST_CASE("Interpolation does not allocate", "[unit][filter][rt]")
{
    Interpolator8x interpolator;
    std::vector<double> in(256, 0.25);
    std::vector<double> out(256 * factor);
    std::vector<double> out2(256 * factor);
    ak4522::test::AllocationCounter counter;
    interpolator.process(in, in, out, out2);
    interpolator.reset();
    CHECK(counter.allocations() == 0);
}
