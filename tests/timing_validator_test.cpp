// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// Serial and clock timing validator (REQ-SERIAL-02; D-T01 to D-T08, D-04,
// D-C02 to D-C05, D-R03, I-T01; P-24, P-36). Limits are checked at the
// datasheet value (passes) and just beyond it (fails).

#include "ak4522/serial.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <random>
#include <vector>

using namespace ak4522;
using V = TimingViolation;

namespace {

TimingValidator prepared(CmodePin cmode = CmodePin::low)
{
    TimingValidator validator;
    REQUIRE(validator.prepare(cmode) == Status::ok);
    return validator;
}

void feed(TimingValidator& validator, const std::vector<PinEdge>& edges)
{
    for (const PinEdge& edge : edges) {
        validator.observe(edge);
    }
}

// Only the given violation kind was counted, the given number of times.
void checkOnly(const TimingValidator& validator, V kind, std::uint64_t expected)
{
    CHECK(validator.violations(kind) == expected);
    CHECK(validator.totalViolations() == expected);
}

struct StreamSpec {
    double fs;
    std::uint32_t mclkRatio;  // MCLK cycles per LRCK cycle actually generated
    std::uint32_t sclkPerFrame;
    int frames;
};

// Ideal slave-mode clocks: LRCK and SDTI change on SCLK falling edges, MCLK
// offset by a quarter period so its edges never tie with LRCK's.
std::vector<PinEdge> idealStream(const StreamSpec& spec, std::mt19937_64& rng)
{
    std::vector<PinEdge> edges;
    const double frame = 1e9 / spec.fs;
    const double mclk = frame / spec.mclkRatio;
    const double sclk = frame / spec.sclkPerFrame;
    const double end = frame * spec.frames;

    for (int k = 0;; ++k) {
        const double t = static_cast<double>(k) * mclk;
        if (t >= end) {
            break;
        }
        edges.push_back({t + mclk / 4, Pin::mclk, true});
        edges.push_back({t + 3 * mclk / 4, Pin::mclk, false});
    }
    std::bernoulli_distribution coin;
    bool sdti = false;
    for (int j = 0;; ++j) {
        const double t = static_cast<double>(j) * sclk;
        if (t >= end) {
            break;
        }
        edges.push_back({t, Pin::sclk, false});
        edges.push_back({t + sclk / 2, Pin::sclk, true});
        const bool next = coin(rng);
        if (next != sdti || j == 0) {
            edges.push_back({t, Pin::sdti, next});
            sdti = next;
        }
    }
    for (int f = 0; f < spec.frames; ++f) {
        edges.push_back({f * frame, Pin::lrck, true});
        edges.push_back({f * frame + frame / 2, Pin::lrck, false});
    }
    std::stable_sort(edges.begin(), edges.end(),
                     [](const PinEdge& a, const PinEdge& b) { return a.timeNs < b.timeNs; });
    return edges;
}

} // namespace

TEST_CASE("Ideal clocks produce no violations at every supported setting", "[unit][serial]")
{
    const CmodePin cmode = GENERATE(CmodePin::low, CmodePin::high, CmodePin::open);
    const double fs = GENERATE(16000.0, 32000.0, 44100.0, 48000.0);
    const std::uint32_t sclk = GENERATE(32u, 64u, 128u); // 128fs is the largest valid at 48 kHz (I-T01)
    CAPTURE(masterClockRatio(cmode), fs, sclk);

    std::mt19937_64 rng(1);
    TimingValidator validator = prepared(cmode);
    feed(validator, idealStream({fs, masterClockRatio(cmode), sclk, 6}, rng));
    CHECK(validator.totalViolations() == 0);
}

TEST_CASE("SCLK at 256fs and 48 kHz violates the SCLK period", "[unit][serial]")
{
    // 256 x 48 kHz = 12.288 MHz, an 81 ns period (I-T01).
    std::mt19937_64 rng(2);
    TimingValidator validator = prepared(CmodePin::open);
    feed(validator, idealStream({48000.0, 512, 256, 3}, rng));
    CHECK(validator.violations(V::sclkPeriod) > 0);
    CHECK(validator.violations(V::sclkPulseWidth) > 0);
}

TEST_CASE("SCLK period limit is 160 ns", "[unit][serial]")
{
    TimingValidator ok = prepared();
    feed(ok, {{0, Pin::sclk, true}, {80, Pin::sclk, false}, {160, Pin::sclk, true}});
    CHECK(ok.totalViolations() == 0);

    TimingValidator bad = prepared();
    feed(bad, {{0, Pin::sclk, true}, {80, Pin::sclk, false}, {159.9, Pin::sclk, true}});
    checkOnly(bad, V::sclkPeriod, 1);
}

TEST_CASE("SCLK high and low widths must be at least 65 ns", "[unit][serial]")
{
    TimingValidator ok = prepared();
    feed(ok, {{0, Pin::sclk, true}, {65, Pin::sclk, false}, {200, Pin::sclk, true}});
    CHECK(ok.totalViolations() == 0);

    TimingValidator high = prepared();
    feed(high, {{0, Pin::sclk, true}, {64.9, Pin::sclk, false}});
    checkOnly(high, V::sclkPulseWidth, 1);

    TimingValidator low = prepared();
    feed(low, {{0, Pin::sclk, false}, {64.9, Pin::sclk, true}});
    checkOnly(low, V::sclkPulseWidth, 1);
}

TEST_CASE("LRCK edge to SCLK rise must be at least 45 ns", "[unit][serial]")
{
    TimingValidator ok = prepared();
    feed(ok, {{0, Pin::lrck, true}, {45, Pin::sclk, true}});
    CHECK(ok.totalViolations() == 0);

    TimingValidator bad = prepared();
    feed(bad, {{0, Pin::lrck, true}, {44.9, Pin::sclk, true}});
    checkOnly(bad, V::lrckToSclkRise, 1);
}

TEST_CASE("Only the first SCLK rise after an LRCK edge is checked for tLRS", "[unit][serial]")
{
    // Two SCLK rises within 45 ns of one LRCK edge: tLRS is one violation,
    // not two. (The fast SCLK also breaks its own period and width limits.)
    TimingValidator validator = prepared();
    feed(validator, {{0, Pin::lrck, true}, {10, Pin::sclk, true}, {20, Pin::sclk, false},
                     {30, Pin::sclk, true}});
    CHECK(validator.violations(V::lrckToSclkRise) == 1);
    CHECK(validator.violations(V::sclkRiseToLrck) == 0);
}

TEST_CASE("SCLK rise to LRCK edge must be at least 45 ns", "[unit][serial]")
{
    TimingValidator ok = prepared();
    feed(ok, {{0, Pin::sclk, true}, {45, Pin::lrck, true}});
    CHECK(ok.totalViolations() == 0);

    TimingValidator bad = prepared();
    feed(bad, {{0, Pin::sclk, true}, {44.9, Pin::lrck, false}});
    checkOnly(bad, V::sclkRiseToLrck, 1);
}

TEST_CASE("An SCLK rise coinciding with an LRCK edge is reported once", "[unit][serial]")
{
    TimingValidator lrckFirst = prepared();
    feed(lrckFirst, {{100, Pin::lrck, true}, {100, Pin::sclk, true}});
    checkOnly(lrckFirst, V::sclkRiseCoincidesWithLrck, 1);

    TimingValidator sclkFirst = prepared();
    feed(sclkFirst, {{100, Pin::sclk, true}, {100, Pin::lrck, true}});
    checkOnly(sclkFirst, V::sclkRiseCoincidesWithLrck, 1);
}

TEST_CASE("SDTI setup must be at least 25 ns", "[unit][serial]")
{
    TimingValidator ok = prepared();
    feed(ok, {{0, Pin::sdti, true}, {25, Pin::sclk, true}});
    CHECK(ok.totalViolations() == 0);

    TimingValidator bad = prepared();
    feed(bad, {{0, Pin::sdti, true}, {24.9, Pin::sclk, true}});
    checkOnly(bad, V::sdtiSetup, 1);
}

TEST_CASE("SDTI hold must be at least 40 ns", "[unit][serial]")
{
    TimingValidator ok = prepared();
    feed(ok, {{0, Pin::sclk, true}, {40, Pin::sdti, true}});
    CHECK(ok.totalViolations() == 0);

    TimingValidator bad = prepared();
    feed(bad, {{0, Pin::sclk, true}, {39.9, Pin::sdti, false}});
    checkOnly(bad, V::sdtiHold, 1);
}

TEST_CASE("LRCK duty cycle must be 45 to 55 percent", "[unit][serial]")
{
    const double period = 1e9 / 48000.0;
    const auto [high, violation] = GENERATE(table<double, bool>({
        {0.45, false}, {0.55, false}, {0.50, false}, {0.449, true}, {0.551, true}}));
    CAPTURE(high);
    TimingValidator validator = prepared();
    feed(validator, {{0, Pin::lrck, true}, {high * period, Pin::lrck, false}, {period, Pin::lrck, true}});
    checkOnly(validator, V::lrckDutyCycle, violation ? 1 : 0);
}

TEST_CASE("LRCK frequency must be 16 to 48 kHz", "[unit][serial]")
{
    const auto [hz, violation] = GENERATE(table<double, bool>({
        {16000.0, false}, {48000.0, false}, {44100.0, false}, {15999.0, true}, {48001.0, true}}));
    CAPTURE(hz);
    const double period = 1e9 / hz;
    TimingValidator validator = prepared();
    feed(validator, {{0, Pin::lrck, true}, {period / 2, Pin::lrck, false}, {period, Pin::lrck, true}});
    checkOnly(validator, V::lrckFrequency, violation ? 1 : 0);
}

TEST_CASE("MCLK frequency must be inside the CMODE range", "[unit][serial]")
{
    const auto [cmode, mhz, violation] = GENERATE(table<CmodePin, double, bool>({
        {CmodePin::low, 4.096, false},   {CmodePin::low, 12.288, false},
        {CmodePin::low, 4.09, true},     {CmodePin::low, 12.3, true},
        {CmodePin::high, 6.144, false},  {CmodePin::high, 18.432, false},
        {CmodePin::high, 6.1, true},     {CmodePin::high, 18.5, true},
        {CmodePin::open, 8.192, false},  {CmodePin::open, 24.576, false},
        {CmodePin::open, 8.1, true},     {CmodePin::open, 24.6, true},
    }));
    CAPTURE(masterClockRatio(cmode), mhz);
    const double period = 1e3 / mhz;
    TimingValidator validator = prepared(cmode);
    feed(validator, {{0, Pin::mclk, true}, {period / 2, Pin::mclk, false}, {period, Pin::mclk, true}});
    checkOnly(validator, V::mclkFrequency, violation ? 1 : 0);
}

TEST_CASE("MCLK pulse width minimum depends on CMODE", "[unit][serial]")
{
    const auto [cmode, width] = GENERATE(table<CmodePin, double>({
        {CmodePin::low, 27.0}, {CmodePin::high, 20.0}, {CmodePin::open, 15.0}}));
    CAPTURE(masterClockRatio(cmode));

    TimingValidator ok = prepared(cmode);
    feed(ok, {{0, Pin::mclk, true}, {width, Pin::mclk, false}});
    CHECK(ok.totalViolations() == 0);

    TimingValidator high = prepared(cmode);
    feed(high, {{0, Pin::mclk, true}, {width - 0.1, Pin::mclk, false}});
    checkOnly(high, V::mclkPulseWidth, 1);

    TimingValidator low = prepared(cmode);
    feed(low, {{0, Pin::mclk, false}, {width - 0.1, Pin::mclk, true}});
    checkOnly(low, V::mclkPulseWidth, 1);
}

TEST_CASE("MCLK cycles per LRCK cycle must equal the CMODE ratio", "[unit][serial]")
{
    // 384 MCLK cycles per frame at 32 kHz is 12.288 MHz, inside the 256fs
    // frequency range, so only the synchronization check fails.
    std::mt19937_64 rng(4);
    TimingValidator validator = prepared(CmodePin::low);
    feed(validator, idealStream({32000.0, 384, 64, 5}, rng));
    // Five LRCK rises; the first period starts before MCLK has run, so the
    // periods ending at rises 3, 4, and 5 are checked.
    checkOnly(validator, V::mclkNotSynchronized, 3);
}

TEST_CASE("MCLK synchronization is checked only while MCLK is observed", "[unit][serial]")
{
    // No MCLK at all: nothing to check.
    const double period = 1e9 / 48000.0;
    TimingValidator noMclk = prepared();
    feed(noMclk, {{0, Pin::lrck, true}, {period / 2, Pin::lrck, false}, {period, Pin::lrck, true}});
    CHECK(noMclk.totalViolations() == 0);

    // MCLK starting mid-period: that period is not counted.
    std::mt19937_64 rng(6);
    auto edges = idealStream({48000.0, 256, 64, 4}, rng);
    edges.erase(std::remove_if(edges.begin(), edges.end(),
                               [&](const PinEdge& e) { return e.pin == Pin::mclk && e.timeNs < period / 3; }),
                edges.end());
    TimingValidator late = prepared();
    feed(late, edges);
    CHECK(late.totalViolations() == 0);
}

TEST_CASE("PD low pulse must be at least 150 ns", "[unit][serial]")
{
    TimingValidator ok = prepared();
    feed(ok, {{0, Pin::pd, true}, {10, Pin::pd, false}, {160, Pin::pd, true}});
    CHECK(ok.totalViolations() == 0);

    TimingValidator bad = prepared();
    feed(bad, {{0, Pin::pd, false}, {149.9, Pin::pd, true}});
    checkOnly(bad, V::pdPulseWidth, 1);
}

TEST_CASE("Repeated levels and out-of-order edges are ignored", "[unit][serial]")
{
    TimingValidator validator = prepared();
    feed(validator, {
        {0, Pin::sclk, true},
        {10, Pin::sclk, true},  // same level: not an edge
        {100, Pin::sclk, false},
        {50, Pin::sclk, true},  // earlier than the previous edge: ignored
        {200, Pin::sclk, true},
    });
    CHECK(validator.totalViolations() == 0);
}

TEST_CASE("Reset clears counts and edge history", "[unit][serial]")
{
    TimingValidator validator = prepared();
    feed(validator, {{0, Pin::sclk, true}, {10, Pin::sclk, false}});
    REQUIRE(validator.totalViolations() == 1);
    validator.reset();
    CHECK(validator.totalViolations() == 0);
    feed(validator, {{5, Pin::sclk, true}}); // no history: nothing to compare
    CHECK(validator.totalViolations() == 0);
}

TEST_CASE("Prepare rejects an invalid CMODE value", "[unit][serial]")
{
    TimingValidator validator;
    CHECK(validator.prepare(static_cast<CmodePin>(3)) == Status::invalidConfiguration);
}

TEST_CASE("Timing validation does not allocate", "[unit][serial][rt]")
{
    std::mt19937_64 rng(5);
    const auto edges = idealStream({44100.0, 384, 64, 4}, rng);
    TimingValidator validator = prepared(CmodePin::high);

    ak4522::test::AllocationCounter counter;
    feed(validator, edges);
    validator.reset();
    CHECK(counter.allocations() == 0);
}

// Clock stop (D-C07, P-31): a clock that has been running and then has no
// transition for more than 62.5 us while PD is high is reported once.
namespace {

// A square wave on one pin from `start` to `end` with the given period.
void square(std::vector<PinEdge>& edges, Pin pin, double period, double start, double end)
{
    for (double t = start; t < end; t += period) {
        edges.push_back({t, pin, true});
        edges.push_back({t + period / 2, pin, false});
    }
}

void sortEdges(std::vector<PinEdge>& edges)
{
    std::stable_sort(edges.begin(), edges.end(),
                     [](const PinEdge& a, const PinEdge& b) { return a.timeNs < b.timeNs; });
}

} // namespace

TEST_CASE("A clock silent for more than 62.5 us is reported once", "[unit][serial][clock]")
{
    // LRCK keeps running at 44.1 kHz; SCLK runs throughout; MCLK stops
    // for 200 us in the middle.
    const double frame = 1e9 / 44100.0;
    std::vector<PinEdge> edges;
    square(edges, Pin::lrck, frame, 0.0, 1e6);
    square(edges, Pin::sclk, frame / 64, 0.0, 1e6);
    square(edges, Pin::mclk, frame / 256, 0.0, 400e3);
    square(edges, Pin::mclk, frame / 256, 600e3, 1e6);
    sortEdges(edges);
    TimingValidator v = prepared();
    feed(v, edges);
    CHECK(v.violations(V::clockStopped) == 1);
}

TEST_CASE("A gap of exactly 62.5 us passes and a longer one fails", "[unit][serial][clock]")
{
    const double gap = GENERATE(62500.0, 62500.001);
    TimingValidator v = prepared();
    v.observe({0.0, Pin::lrck, true});
    v.observe({gap, Pin::lrck, false});
    CHECK(v.violations(V::clockStopped) == (gap > 62500.0 ? 1u : 0u));
}

TEST_CASE("Clocks that stop for good are found by advanceTo", "[unit][serial][clock]")
{
    const double frame = 1e9 / 48000.0;
    std::vector<PinEdge> edges;
    square(edges, Pin::lrck, frame, 0.0, 200e3);
    square(edges, Pin::sclk, frame / 64, 0.0, 200e3);
    square(edges, Pin::mclk, frame / 256, 0.0, 200e3);
    sortEdges(edges);
    TimingValidator v = prepared();
    feed(v, edges);
    CHECK(v.violations(V::clockStopped) == 0);
    v.advanceTo(240e3);
    CHECK(v.violations(V::clockStopped) == 0);
    v.advanceTo(300e3);
    CHECK(v.violations(V::clockStopped) == 3);
    v.advanceTo(900e3); // already reported
    CHECK(v.violations(V::clockStopped) == 3);
    v.advanceTo(100e3); // earlier than the latest time: ignored
    CHECK(v.violations(V::clockStopped) == 3);
}

TEST_CASE("Clocks may stop while PD is low and must resume after release", "[unit][serial][clock]")
{
    const double frame = 1e9 / 48000.0;
    TimingValidator v = prepared();
    std::vector<PinEdge> edges{{0.0, Pin::pd, true}};
    square(edges, Pin::lrck, frame, 10.0, 100e3);
    edges.push_back({100e3, Pin::pd, false}); // reset asserted; clocks stop
    edges.push_back({500e3, Pin::pd, true});  // released
    square(edges, Pin::lrck, frame, 540e3, 700e3); // resumes within 62.5 us
    sortEdges(edges);
    feed(v, edges);
    CHECK(v.violations(V::clockStopped) == 0);

    // Released again, but the clock does not resume.
    v.observe({700e3, Pin::pd, false});
    v.observe({701e3, Pin::pd, true});
    v.advanceTo(701e3 + 62500.0);
    CHECK(v.violations(V::clockStopped) == 0);
    v.advanceTo(701e3 + 62501.0);
    CHECK(v.violations(V::clockStopped) == 1);
}

TEST_CASE("A clock that stops twice is reported twice", "[unit][serial][clock]")
{
    TimingValidator v = prepared();
    std::vector<PinEdge> edges;
    const double frame = 1e9 / 48000.0;
    square(edges, Pin::lrck, frame, 0.0, 100e3);
    square(edges, Pin::lrck, frame, 300e3, 400e3); // after the first stop
    square(edges, Pin::lrck, frame, 600e3, 700e3); // after the second
    sortEdges(edges);
    feed(v, edges);
    CHECK(v.violations(V::clockStopped) == 2);
}
