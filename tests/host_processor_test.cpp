// HostProcessor, the plugin-facing host-rate path (REQ-RATE-01 to
// REQ-RATE-03, REQ-CTRL-01, REQ-LAT-01, REQ-LAT-03, REQ-VST-01, REQ-RT-01;
// P-10, P-12, P-39, P-41, P-74, P-84). Timing is measured from sine-fit
// phases, independent of the implementation.

#include "ak4522/host_processor.hpp"
#include "support/allocation_counter.hpp"
#include "support/sine_fit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using namespace ak4522;

namespace {

constexpr double pi = 3.14159265358979323846;

HostConfig config(double host, double dac, std::size_t maxBlock = 512)
{
    HostConfig c;
    c.hostRate = host;
    c.dac.dacRate = dac;
    c.maxBlockFrames = maxBlock;
    return c;
}

HostProcessor prepared(const HostConfig& c)
{
    HostProcessor p;
    REQUIRE(p.prepare(c) == Status::ok);
    return p;
}

struct Stereo {
    std::vector<double> left;
    std::vector<double> right;
};

Stereo sine(std::size_t frames, double freq, double rate, double amplitude = 0.5)
{
    Stereo s{std::vector<double>(frames), std::vector<double>(frames)};
    for (std::size_t n = 0; n < frames; ++n) {
        s.left[n] = amplitude * std::sin(2.0 * pi * freq * static_cast<double>(n) / rate);
        s.right[n] = s.left[n];
    }
    return s;
}

// Runs the processor in blocks of the given sizes; events are absolute host
// frames and are split into the blocks that contain them.
Stereo run(HostProcessor& p, const Stereo& in, const std::vector<std::size_t>& sizes,
           const std::vector<ControlEvent>& absolute = {})
{
    Stereo out{std::vector<double>(in.left.size()), std::vector<double>(in.left.size())};
    std::size_t at = 0, block = 0, next = 0;
    while (at < in.left.size()) {
        const std::size_t n = std::min(sizes[block++ % sizes.size()], in.left.size() - at);
        std::vector<ControlEvent> local;
        while (next < absolute.size() && absolute[next].offset < at + n) {
            ControlEvent e = absolute[next++];
            e.offset -= static_cast<std::uint32_t>(at);
            local.push_back(e);
        }
        const Status s = p.process(
            StereoView<const double>{std::span(in.left).subspan(at, n), std::span(in.right).subspan(at, n)},
            StereoView<double>{std::span(out.left).subspan(at, n), std::span(out.right).subspan(at, n)}, local);
        REQUIRE(s == Status::ok);
        at += n;
    }
    return out;
}

// Phase delay in host frames of a sine through the processor, resolved to
// the period nearest `expected`.
double phaseDelay(HostProcessor& p, double host, double freq, double expected)
{
    p.reset();
    const auto in = sine(static_cast<std::size_t>(host * 0.25), freq, host);
    const auto out = run(p, in, {512});
    const std::size_t skip = static_cast<std::size_t>(expected) + 2000;
    const auto fit = test::fitSine(std::span(out.left).subspan(skip), freq, host,
                                   static_cast<double>(skip) / host);
    // out = sin(w (n - d)) => phase = -w d (mod 2 pi).
    const double period = host / freq;
    double d = -fit.phase / (2.0 * pi) * period;
    d += std::round((expected - d) / period) * period;
    return d;
}

struct Rates {
    double host;
    double dac;
};

const std::vector<Rates> matrix{
    {44100, 44100}, {48000, 48000}, {48000, 44100}, {44100, 48000}, {96000, 44100},
    {88200, 32000}, {192000, 48000}, {176400, 16000}, {44100, 37800}, {192000, 24000},
};

} // namespace

TEST_CASE("Prepare validates host and chip configuration", "[unit][host]")
{
    HostProcessor p;
    CHECK(p.prepare(config(32000, 44100)) == Status::unsupportedHostRate);
    CHECK(p.prepare(config(48000, 96000)) == Status::invalidDacRate);
    CHECK(p.prepare(config(48000, 44100, 0)) == Status::invalidBlockSize);
    CHECK_FALSE(p.isPrepared());
    CHECK(p.prepare(config(48000, 44100)) == Status::ok);
    CHECK(p.isPrepared());
}

TEST_CASE("Process rejects bad calls", "[unit][host]")
{
    HostProcessor unprepared;
    std::vector<double> a(16), b(16);
    CHECK(unprepared.process(StereoView<const double>{a, b}, StereoView<double>{a, b}, {})
          == Status::notPrepared);
    HostProcessor p = prepared(config(48000, 44100));
    std::vector<double> c(15);
    CHECK(p.process(StereoView<const double>{a, b}, StereoView<double>{c, c}, {})
          == Status::insufficientOutputCapacity);
    CHECK(p.process(StereoView<const double>{a, c}, StereoView<double>{a, b}, {})
          == Status::channelSizeMismatch);
    const std::vector<ControlEvent> late{ControlEvent::softMute(16, true)};
    CHECK(p.process(StereoView<const double>{a, b}, StereoView<double>{a, b}, late)
          == Status::eventOutOfRange);
    CHECK(p.diagnostics().rejectedBlocks == 3);
}

TEST_CASE("Reported latency is whole frames with fractional alignment", "[unit][host]")
{
    const Rates r = GENERATE(Catch::Generators::from_range(matrix));
    CAPTURE(r.host, r.dac);
    HostProcessor p = prepared(config(r.host, r.dac));
    const LatencyInfo l = p.latency();
    CHECK(l.hostRate == r.host);
    CHECK(l.dacRate == r.dac);
    CHECK(l.coreDacFrames == 14.7);
    CHECK(l.alignmentHostFrames <= 0.0);
    CHECK(l.alignmentHostFrames > -1.0);
    // Buffering plus the core delay plus the alignment is the reported whole.
    const double total = l.bufferingHostFrames + 14.7 * r.host / r.dac + l.alignmentHostFrames;
    CHECK(std::abs(total - l.reportedHostFrames) < 1e-9);
    CHECK(std::abs(l.totalSeconds * r.host - l.reportedHostFrames) < 1e-9);
}

TEST_CASE("Output timing matches the reported latency within 0.01 frame", "[dsp][host]")
{
    // P-84: least-squares phase-delay fit over passband frequencies.
    const Rates r = GENERATE(Catch::Generators::from_range(matrix));
    CAPTURE(r.host, r.dac);
    HostProcessor p = prepared(config(r.host, r.dac));
    const double expected = p.latency().reportedHostFrames;
    const double edge = 20.0 / 44.1 * std::min(r.host, r.dac);
    double sum = 0.0;
    int count = 0;
    for (int k = 1; k <= 6; ++k) {
        const double freq = edge * k / 7.0;
        const double d = phaseDelay(p, r.host, freq, expected);
        CAPTURE(freq, d);
        // Individual frequencies may differ by the reconstruction dispersion
        // (0.013 DAC frames); the passband average is the reference.
        CHECK(std::abs(d - expected) < 0.02 * std::max(1.0, r.host / r.dac));
        sum += d;
        ++count;
    }
    CHECK(std::abs(sum / count - expected) <= 0.01 * std::max(1.0, r.host / r.dac));
}

TEST_CASE("Exactly N frames out per N in, without underruns", "[dsp][host][streaming]")
{
    const Rates r = GENERATE(Catch::Generators::from_range(matrix));
    CAPTURE(r.host, r.dac);
    HostProcessor p = prepared(config(r.host, r.dac, 256));
    const auto in = sine(static_cast<std::size_t>(r.host * 0.3), 997.0, r.host);
    std::mt19937_64 rng(5);
    std::uniform_int_distribution<std::size_t> dist(0, 700);
    std::vector<std::size_t> sizes(60);
    for (auto& s : sizes) {
        s = dist(rng);
    }
    sizes.push_back(1);
    static_cast<void>(run(p, in, sizes));
    CHECK(p.diagnostics().hostUnderruns == 0);
    CHECK(p.diagnostics().chunkedBlocks > 0); // sizes above 256 were split
}

TEST_CASE("A passband tone keeps its level; DAC images reach high host rates", "[dsp][host]")
{
    const Rates r = GENERATE(Rates{44100, 44100}, Rates{48000, 44100}, Rates{96000, 32000});
    CAPTURE(r.host, r.dac);
    HostProcessor p = prepared(config(r.host, r.dac));
    // At the 1 kHz response reference (D-F09). The design has unity gain
    // there at a 44.1 kHz DAC; at other DAC rates 1 kHz falls elsewhere in
    // the passband ripple.
    const double freq = 1000.0;
    const auto in = sine(static_cast<std::size_t>(r.host * 0.25), freq, r.host);
    const auto out = run(p, in, {512});
    const std::size_t skip = p.latency().reportedHostFrames + 2000;
    const auto fit = test::fitSine(std::span(out.left).subspan(skip), freq, r.host);
    const double tolerance = r.dac == 44100.0 ? 0.01 : 0.06;
    CHECK(std::abs(20.0 * std::log10(fit.amplitude / 0.5)) < tolerance);
    if (r.host / 2.0 < 24.1 / 44.1 * r.dac) {
        // No DAC image reaches the host band: only 20-bit quantization remains.
        CHECK(20.0 * std::log10(fit.residualRms) < -100.0);
    } else {
        // The host band reaches the DAC images (O-01): the first image, at
        // fs - f, is present at no more than the 43 dB stopband level (D-F05).
        const auto image = test::fitSine(std::span(out.left).subspan(skip), r.dac - freq, r.host);
        const double level = 20.0 * std::log10(image.amplitude / fit.amplitude);
        CAPTURE(level);
        CHECK(level <= -43.0);
        CHECK(level > -100.0);
    }
}

TEST_CASE("Output is independent of block partitioning, with events", "[dsp][host][streaming]")
{
    const Rates r = GENERATE(Rates{48000, 44100}, Rates{44100, 44100}, Rates{96000, 37800});
    CAPTURE(r.host, r.dac);
    const auto in = sine(static_cast<std::size_t>(r.host * 0.2), 1234.0, r.host, 0.7);
    const std::vector<ControlEvent> events{
        ControlEvent::deemphasis(1000, DeemphasisSelect::fs44100), ControlEvent::softMute(3000, true),
        ControlEvent::softMute(3001, false), ControlEvent::powerDown(6000, false),
        ControlEvent::powerDown(6100, true)};
    HostProcessor a = prepared(config(r.host, r.dac, 512));
    const auto reference = run(a, in, {in.left.size()}, events);
    for (const auto& [sizes, maxBlock] : std::vector<std::pair<std::vector<std::size_t>, std::size_t>>{
             {{1}, 512}, {{0, 17, 512, 3, 1000}, 512}, {{4096}, 64}}) {
        HostProcessor b = prepared(config(r.host, r.dac, maxBlock));
        const auto out = run(b, in, sizes, events);
        CHECK(out.left == reference.left);
        CHECK(out.right == reference.right);
    }
}

TEST_CASE("Soft mute takes effect aligned with the reported latency", "[dsp][host][mute]")
{
    const Rates r = GENERATE(Rates{48000, 44100}, Rates{44100, 44100}, Rates{192000, 48000});
    CAPTURE(r.host, r.dac);
    const auto in = sine(static_cast<std::size_t>(r.host * 0.2), 500.0, r.host);
    const std::uint32_t at = 4000;
    HostProcessor plain = prepared(config(r.host, r.dac));
    HostProcessor muted = prepared(config(r.host, r.dac));
    const auto a = run(plain, in, {300});
    const auto b = run(muted, in, {300}, {ControlEvent::softMute(at, true)});
    const std::size_t latency = muted.latency().reportedHostFrames;
    // The ramp starts at the event's DAC frame; filters spread it by their
    // half-lengths (interpolator 11.9 frames and the converters) around the
    // aligned time.
    const std::size_t spread = static_cast<std::size_t>(std::ceil(16.0 * r.host / r.dac)) + 80;
    for (std::size_t n = 0; n + spread < at + latency; ++n) {
        REQUIRE(a.left[n] == b.left[n]);
    }
    CHECK(a.left[at + latency + spread] != b.left[at + latency + spread]);
    // After the 1024-frame ramp and the filter tails: exact silence.
    const std::size_t silent = at + latency + static_cast<std::size_t>(1024.0 * r.host / r.dac) + 2 * spread;
    REQUIRE(silent < b.left.size());
    CHECK(std::all_of(b.left.begin() + static_cast<std::ptrdiff_t>(silent), b.left.end(),
                      [](double v) { return v == 0.0; }));
}

TEST_CASE("A host event acts at the first DAC frame at or after its time", "[unit][host]")
{
    // P-41 at 48 kHz host, 44.1 kHz DAC: DAC frame j represents host time
    // j * 160 / 147, so host frame 3200 is exactly DAC frame 2940. Rounding up
    // maps 3199 and 3200 to frame 2940 and 3201 to 2941; rounding down would
    // instead group 3200 with 3201. Same frame means identical output.
    const auto in = sine(12000, 700.0, 48000.0);
    auto withMuteAt = [&](std::uint32_t n) {
        HostProcessor h = prepared(config(48000, 44100));
        return run(h, in, {333}, {ControlEvent::softMute(n, true)}).left;
    };
    const auto before = withMuteAt(3199);
    const auto exact = withMuteAt(3200);
    const auto after = withMuteAt(3201);
    CHECK(before == exact);
    CHECK(exact != after);
}

TEST_CASE("Reset discards events still pending in the queue", "[unit][host]")
{
    // An event in the last frame of a block waits for the input converter's
    // look-ahead, so it is still queued when reset arrives.
    const auto in = sine(3000, 400.0, 48000.0);
    HostProcessor p = prepared(config(48000, 44100));
    std::vector<double> l(64, 0.1), r(64, 0.1), ol(64), orr(64);
    REQUIRE(p.process(StereoView<const double>{l, r}, StereoView<double>{ol, orr},
                      std::vector<ControlEvent>{ControlEvent::softMute(63, true)}) == Status::ok);
    p.reset();
    HostProcessor fresh = prepared(config(48000, 44100));
    CHECK(run(p, in, {256}).left == run(fresh, in, {256}).left);
}

TEST_CASE("Events beyond the pending capacity are dropped and counted", "[unit][host]")
{
    HostConfig c = config(48000, 44100);
    c.maxPendingEvents = 2;
    HostProcessor p = prepared(c);
    std::vector<double> buf(64, 0.0);
    std::vector<ControlEvent> many;
    for (std::uint32_t i = 0; i < 5; ++i) {
        many.push_back(ControlEvent::softMute(10 + i, i % 2 == 0));
    }
    CHECK(p.process(StereoView<const double>{buf, buf}, StereoView<double>{buf, buf}, many) == Status::ok);
    CHECK(p.diagnostics().droppedEvents == 3);
}

TEST_CASE("Float and double processing agree", "[unit][host]")
{
    const auto in = sine(8000, 440.0, 48000.0, 0.6);
    HostProcessor d = prepared(config(48000, 44100));
    const auto outD = run(d, in, {256});
    HostProcessor f = prepared(config(48000, 44100));
    std::vector<float> inF(in.left.size());
    std::transform(in.left.begin(), in.left.end(), inF.begin(), [](double v) { return static_cast<float>(v); });
    std::vector<float> outL(inF.size()), outR(inF.size());
    for (std::size_t at = 0; at < inF.size(); at += 256) {
        const std::size_t n = std::min<std::size_t>(256, inF.size() - at);
        REQUIRE(f.process(StereoView<const float>{std::span(inF).subspan(at, n), std::span(inF).subspan(at, n)},
                          StereoView<float>{std::span(outL).subspan(at, n), std::span(outR).subspan(at, n)}, {})
                == Status::ok);
    }
    double worst = 0.0;
    for (std::size_t n = 0; n < outL.size(); ++n) {
        worst = std::max(worst, std::abs(static_cast<double>(outL[n]) - outD.left[n]));
    }
    // Float input differs below 2^-24, far below the 20-bit LSB, so the
    // quantized PCM is almost always identical.
    CHECK(worst < 1e-5);
}

TEST_CASE("In-place processing gives the same output", "[unit][host]")
{
    const auto in = sine(5000, 700.0, 44100.0);
    HostProcessor a = prepared(config(44100, 48000));
    const auto expected = run(a, in, {500});
    HostProcessor b = prepared(config(44100, 48000));
    Stereo buf = in;
    for (std::size_t at = 0; at < buf.left.size(); at += 500) {
        const std::size_t n = std::min<std::size_t>(500, buf.left.size() - at);
        auto l = std::span(buf.left).subspan(at, n);
        auto r = std::span(buf.right).subspan(at, n);
        REQUIRE(b.process(StereoView<const double>{l, r}, StereoView<double>{l, r}, {}) == Status::ok);
    }
    CHECK(buf.left == expected.left);
}

TEST_CASE("16-bit input resolution drops detail below the 16-bit LSB", "[unit][host]")
{
    // 1e-5 is between half a 20-bit LSB (9.5e-7) and half a 16-bit LSB (1.5e-5).
    const auto in = sine(6000, 1000.0, 44100.0, 1e-5);
    HostConfig c20 = config(44100, 44100);
    HostConfig c16 = c20;
    c16.inputResolution = InputResolution::bits16;
    HostProcessor p20 = prepared(c20);
    HostProcessor p16 = prepared(c16);
    const auto out20 = run(p20, in, {512});
    const auto out16 = run(p16, in, {512});
    CHECK(std::any_of(out20.left.begin(), out20.left.end(), [](double v) { return v != 0.0; }));
    CHECK(std::all_of(out16.left.begin(), out16.left.end(), [](double v) { return v == 0.0; }));
}

TEST_CASE("Dither is reproducible from its seed and off by default", "[unit][host]")
{
    const auto in = sine(6000, 1000.0, 44100.0, 1e-5);
    HostConfig c = config(44100, 44100);
    c.inputResolution = InputResolution::bits16;
    c.inputDither = true;
    HostProcessor a = prepared(c);
    HostProcessor b = prepared(c);
    const auto outA = run(a, in, {512});
    const auto outB = run(b, in, {512});
    CHECK(outA.left == outB.left);
    // With dither, the sub-LSB tone is no longer quantized away entirely.
    CHECK(std::any_of(outA.left.begin(), outA.left.end(), [](double v) { return v != 0.0; }));
    c.ditherSeed ^= 1;
    HostProcessor other = prepared(c);
    CHECK(run(other, in, {512}).left != outA.left);
}

TEST_CASE("Non-finite and clipped input is counted", "[unit][host]")
{
    HostProcessor p = prepared(config(44100, 44100));
    std::vector<double> l{0.0, std::numeric_limits<double>::quiet_NaN(), 2.0, -3.0};
    std::vector<double> r{std::numeric_limits<double>::infinity(), 0.0, 0.0, 0.0};
    std::vector<double> ol(4), orr(4);
    REQUIRE(p.process(StereoView<const double>{l, r}, StereoView<double>{ol, orr}, {}) == Status::ok);
    CHECK(p.diagnostics().nonFiniteInputSamples == 2);
    CHECK(p.diagnostics().clippedInputSamples == 2);
}

TEST_CASE("Reset returns to the state after prepare", "[unit][host]")
{
    const auto in = sine(4000, 300.0, 48000.0);
    HostProcessor p = prepared(config(48000, 44100));
    const auto first = run(p, in, {256}, {ControlEvent::softMute(100, true)});
    p.reset();
    const auto second = run(p, in, {256}, {ControlEvent::softMute(100, true)});
    CHECK(first.left == second.left);
}

TEST_CASE("Host processing does not allocate", "[unit][host][rt]")
{
    HostProcessor p = prepared(config(48000, 44100, 512));
    std::vector<double> in(2048, 0.1), outL(2048), outR(2048);
    const std::vector<ControlEvent> events{ControlEvent::softMute(5, true), ControlEvent::powerDown(1000, false)};
    test::AllocationCounter counter;
    const Status s = p.process(StereoView<const double>{in, in}, StereoView<double>{outL, outR}, events);
    p.reset();
    static_cast<void>(p.latency());
    static_cast<void>(p.diagnostics());
    CHECK(counter.allocations() == 0);
    CHECK(s == Status::ok);
}
