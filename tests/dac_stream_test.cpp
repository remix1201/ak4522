// DacStream, a source at its own DAC rate played at a host rate (REQ-API-01,
// REQ-RATE-02, REQ-LAT-03, REQ-RT-01; P-39, P-100 to P-102, I-P03). Expected
// frame counts and timing are computed independently of the implementation.

#include "ak4522/dac_stream.hpp"
#include "support/allocation_counter.hpp"
#include "support/sine_fit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

using namespace ak4522;

namespace {

constexpr double pi = 3.14159265358979323846;

// A pull source over fixed data. Frames past the end are zero. Events are in
// absolute DAC frames.
struct Source {
    std::vector<std::int32_t> il, ir;
    std::vector<double> dl, dr;
    std::vector<ControlEvent> events;
    std::size_t position = 0;
    std::size_t nextEvent = 0;
    std::uint64_t requested = 0;
    bool badEvents = false; // emit one out-of-range event per block

    static void pull(void* context, DacPullBlock& b) noexcept
    {
        auto& s = *static_cast<Source*>(context);
        for (std::size_t j = 0; j < b.frames; ++j) {
            const std::size_t at = s.position + j;
            if (!b.integers.left.empty()) {
                b.integers.left[j] = at < s.il.size() ? s.il[at] : 0;
                b.integers.right[j] = at < s.ir.size() ? s.ir[at] : 0;
            } else if (!b.floats.left.empty()) {
                b.floats.left[j] = at < s.dl.size() ? static_cast<float>(s.dl[at]) : 0.0f;
                b.floats.right[j] = at < s.dr.size() ? static_cast<float>(s.dr[at]) : 0.0f;
            } else {
                b.doubles.left[j] = at < s.dl.size() ? s.dl[at] : 0.0;
                b.doubles.right[j] = at < s.dr.size() ? s.dr[at] : 0.0;
            }
        }
        while (s.nextEvent < s.events.size() && s.events[s.nextEvent].offset < s.position + b.frames) {
            ControlEvent e = s.events[s.nextEvent++];
            e.offset -= static_cast<std::uint32_t>(s.position);
            b.events[b.eventCount++] = e;
        }
        if (s.badEvents && b.frames > 0) {
            b.events[b.eventCount++] = ControlEvent::softMute(static_cast<std::uint32_t>(b.frames), true);
        }
        s.position += b.frames;
        s.requested += b.frames;
    }
};

DacStreamConfig config(double host, ExactRate rate, DacSampleFormat format = DacSampleFormat::int20,
                       std::size_t maxBlock = 512)
{
    DacStreamConfig c;
    c.hostRate = host;
    c.dacRate = rate;
    c.format = format;
    c.maxBlockFrames = maxBlock;
    return c;
}

DacStream pulling(DacStreamConfig c, Source& source)
{
    c.mode = DacStreamMode::pull;
    c.pull = &Source::pull;
    c.pullContext = &source;
    DacStream s;
    REQUIRE(s.prepare(c) == Status::ok);
    return s;
}

struct Stereo {
    std::vector<double> left;
    std::vector<double> right;
};

Stereo render(DacStream& s, std::size_t frames, const std::vector<std::size_t>& sizes)
{
    Stereo out{std::vector<double>(frames), std::vector<double>(frames)};
    std::size_t at = 0, block = 0;
    while (at < frames) {
        const std::size_t n = std::min(sizes[block++ % sizes.size()], frames - at);
        REQUIRE(s.render(StereoView<double>{std::span(out.left).subspan(at, n),
                                            std::span(out.right).subspan(at, n)})
                == Status::ok);
        at += n;
    }
    return out;
}

std::vector<std::size_t> randomSizes(std::uint64_t seed, std::size_t count = 80, std::size_t max = 900)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::size_t> dist(0, max);
    std::vector<std::size_t> sizes(count);
    for (auto& s : sizes) {
        s = dist(rng);
    }
    sizes.push_back(1);
    return sizes;
}

// 20-bit codes of a sine at the DAC rate.
Source sineSource(std::size_t frames, double freq, double rate, double amplitude = 0.5)
{
    Source s;
    s.il.resize(frames);
    s.ir.resize(frames);
    for (std::size_t j = 0; j < frames; ++j) {
        const double x = amplitude * std::sin(2.0 * pi * freq * static_cast<double>(j) / rate);
        s.il[j] = static_cast<std::int32_t>(std::lround(x * 524288.0));
        s.ir[j] = -s.il[j];
    }
    return s;
}

Source randomSource(std::size_t frames, std::int32_t lo, std::int32_t hi, std::uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int32_t> dist(lo, hi);
    Source s;
    s.il.resize(frames);
    s.ir.resize(frames);
    for (std::size_t j = 0; j < frames; ++j) {
        s.il[j] = dist(rng);
        s.ir[j] = dist(rng);
    }
    return s;
}

double maxDifference(const Stereo& a, const Stereo& b)
{
    double d = 0.0;
    for (std::size_t i = 0; i < a.left.size(); ++i) {
        d = std::max({d, std::abs(a.left[i] - b.left[i]), std::abs(a.right[i] - b.right[i])});
    }
    return d;
}

// A fractional DAC rate inside the documented range: 44.1 kHz pulled down
// by 1000/1001, about 44055.944 Hz.
constexpr ExactRate pulledDown{44100000, 1001};

} // namespace

TEST_CASE("Stream prepare validates rates, mode, and capacities", "[unit][stream]")
{
    Source src;
    DacStreamConfig c = config(48000, {44100, 1});
    c.pull = &Source::pull;
    c.pullContext = &src;
    DacStream s;
    CHECK(s.prepare(c) == Status::ok);

    auto with = [&](auto change) {
        DacStreamConfig d = c;
        change(d);
        return s.prepare(d);
    };
    CHECK(with([](auto& d) { d.hostRate = 32000; }) == Status::unsupportedHostRate);
    CHECK(with([](auto& d) { d.dacRate = {0, 1}; }) == Status::invalidDacRate);
    CHECK(with([](auto& d) { d.dacRate = {44100, 0}; }) == Status::invalidDacRate);
    CHECK(with([](auto& d) { d.dacRate = {48001, 1}; }) == Status::invalidDacRate);
    CHECK(with([](auto& d) { d.dacRate = {15999999, 1000}; }) == Status::invalidDacRate);
    // 44100 Hz, but with a denominator above 2^20 (P-100).
    CHECK(with([](auto& d) { d.dacRate = {44100ull * ((1ull << 20) + 1), (1ull << 20) + 1}; })
          == Status::invalidDacRate);
    CHECK(with([](auto& d) { d.dacRate = {96000, 2}; }) == Status::ok);
    CHECK(with([](auto& d) { d.maxBlockFrames = 0; }) == Status::invalidBlockSize);
    CHECK(with([](auto& d) { d.pull = nullptr; }) == Status::invalidConfiguration);
    CHECK(with([](auto& d) { d.maxEventsPerBlock = 0; }) == Status::invalidConfiguration);
    CHECK(with([](auto& d) {
              d.mode = DacStreamMode::push;
              d.pushCapacityFrames = 0;
          })
          == Status::invalidConfiguration);
    CHECK(with([](auto& d) { d.mode = DacStreamMode::push; }) == Status::ok); // no callback needed
    CHECK_FALSE(with([](auto& d) { d.dac.profile = ModelProfile::measured; }) == Status::ok);
    CHECK_FALSE(s.isPrepared());
}

TEST_CASE("A fractional DAC rate is consumed without drift", "[unit][stream]")
{
    // After T host frames the stream has consumed exactly ceil(T N / (D H))
    // DAC frames: the first DAC frame at or after host time T (P-101).
    const double host = GENERATE(44100.0, 48000.0, 96000.0, 192000.0);
    CAPTURE(host);
    Source src;
    DacStream s = pulling(config(host, pulledDown, DacSampleFormat::int20, 256), src);
    const auto sizes = randomSizes(7);
    std::uint64_t total = 0;
    std::vector<double> l(1000), r(1000);
    for (int round = 0; round < 400; ++round) {
        const std::size_t n = sizes[static_cast<std::size_t>(round) % sizes.size()];
        REQUIRE(s.render(StereoView<double>{std::span(l).first(n), std::span(r).first(n)}) == Status::ok);
        total += n;
        const std::uint64_t den = pulledDown.denominator * static_cast<std::uint64_t>(host);
        REQUIRE(src.requested == (total * pulledDown.numerator + den - 1) / den);
    }
    CHECK(s.diagnostics().chunkedBlocks > 0);
}

TEST_CASE("Stream output does not depend on render block sizes", "[unit][stream][streaming]")
{
    const double host = GENERATE(48000.0, 88200.0);
    CAPTURE(host);
    const std::size_t frames = static_cast<std::size_t>(host * 0.25);
    Source a = sineSource(frames, 997.0, pulledDown.hz());
    a.events = {ControlEvent::softMute(3000, true), ControlEvent::deemphasis(5000, DeemphasisSelect::fs44100),
                ControlEvent::softMute(6000, false)};
    Source b = a;
    DacStream one = pulling(config(host, pulledDown), a);
    DacStream many = pulling(config(host, pulledDown), b);
    const Stereo x = render(one, frames, {frames});
    const Stereo y = render(many, frames, randomSizes(11));
    CHECK(maxDifference(x, y) <= 1e-12); // gate 8
}

TEST_CASE("Stream output timing matches the reported latency", "[dsp][stream]")
{
    // DAC frame j is presented at host time j H / fs; a sine through the
    // stream must lag that by the reported latency (P-39, gate 3).
    struct Case {
        double host;
        ExactRate rate;
    };
    const Case c = GENERATE(Case{48000, pulledDown}, Case{96000, pulledDown}, Case{44100, {48000, 1}},
                            Case{48000, {437500, 10}});
    const double fs = c.rate.hz();
    CAPTURE(c.host, fs);
    const double edge = 20.0 / 44.1 * std::min(c.host, fs);
    double sum = 0.0;
    double expected = 0.0;
    for (int k = 1; k <= 6; ++k) {
        const double freq = edge * k / 7.0;
        const std::size_t frames = static_cast<std::size_t>(c.host * 0.25);
        Source src = sineSource(static_cast<std::size_t>(fs * 0.3), freq, fs);
        DacStream s = pulling(config(c.host, c.rate), src);
        expected = s.latency().reportedHostFrames;
        const Stereo out = render(s, frames, {512});
        const std::size_t skip = static_cast<std::size_t>(expected) + 2000;
        const auto fit = test::fitSine(std::span(out.left).subspan(skip), freq, c.host,
                                       static_cast<double>(skip) / c.host);
        const double period = c.host / freq;
        double d = -fit.phase / (2.0 * pi) * period;
        d += std::round((expected - d) / period) * period;
        CAPTURE(freq, d, expected);
        CHECK(std::abs(fit.amplitude - 0.5) < 0.5 * 0.03); // within the +/-0.2 dB response
        CHECK(std::abs(d - expected) < 0.02 * std::max(1.0, c.host / fs));
        sum += d;
    }
    CHECK(std::abs(sum / 6.0 - expected) <= 0.01 * std::max(1.0, c.host / fs));
}

TEST_CASE("Reported stream latency is buffering plus aligned core delay", "[unit][stream]")
{
    Source src;
    DacStream s = pulling(config(48000, pulledDown), src);
    const LatencyInfo l = s.latency();
    const double hostPerDac = 48000.0 / pulledDown.hz();
    CHECK(l.dacRate == pulledDown.hz());
    CHECK(l.hostRate == 48000.0);
    CHECK(l.inputSrcHostFrames == 0.0);
    const double total = l.bufferingHostFrames + 14.7 * hostPerDac + l.alignmentHostFrames;
    CHECK(std::abs(total - l.reportedHostFrames) < 1e-9);
    CHECK(l.alignmentHostFrames <= 0.0);
    CHECK(l.alignmentHostFrames > -hostPerDac);
}

TEST_CASE("Integer and float formats map to the same 20-bit codes", "[unit][stream][pcm]")
{
    const std::size_t frames = 6000;
    const auto run = [&](Source& src, DacSampleFormat format) {
        DacStream s = pulling(config(48000, {44100, 1}, format), src);
        return render(s, frames, {300});
    };
    SECTION("16-bit values carry 20-bit weight (REQ-PCM-02)")
    {
        Source s16 = randomSource(6000, -32768, 32767, 3);
        Source s20 = s16;
        for (auto* v : {&s20.il, &s20.ir}) {
            for (auto& x : *v) {
                x *= 16;
            }
        }
        CHECK(run(s16, DacSampleFormat::int16).left == run(s20, DacSampleFormat::int20).left);
    }
    SECTION("24-bit values keep their top 20 bits (I-P03)")
    {
        Source s24 = randomSource(6000, -8388608, 8388607, 4);
        Source s20 = s24;
        for (auto* v : {&s20.il, &s20.ir}) {
            for (auto& x : *v) {
                x = static_cast<std::int32_t>(std::floor(static_cast<double>(x) / 16.0));
            }
        }
        const Stereo a = run(s24, DacSampleFormat::int24);
        const Stereo b = run(s20, DacSampleFormat::int20);
        CHECK(a.left == b.left);
        CHECK(a.right == b.right);
    }
    SECTION("Float input is quantized per REQ-PCM-03")
    {
        Source s20 = randomSource(6000, -524288, 524287, 5);
        Source sf = s20;
        for (std::size_t j = 0; j < frames; ++j) {
            sf.dl.push_back(s20.il[j] / 524288.0); // exact in float and double
            sf.dr.push_back(s20.ir[j] / 524288.0);
        }
        Source sd = sf;
        const Stereo ref = run(s20, DacSampleFormat::int20);
        CHECK(run(sf, DacSampleFormat::float32).left == ref.left);
        CHECK(run(sd, DacSampleFormat::float64).left == ref.left);
    }
}

TEST_CASE("Out-of-range integers saturate and are counted", "[unit][stream][pcm]")
{
    Source over;
    over.il = {40000, -40000, 100};
    over.ir = {0, 0, 0};
    Source clamped;
    clamped.il = {32767, -32768, 100};
    clamped.ir = {0, 0, 0};
    DacStream a = pulling(config(48000, {44100, 1}, DacSampleFormat::int16), over);
    DacStream b = pulling(config(48000, {44100, 1}, DacSampleFormat::int16), clamped);
    CHECK(render(a, 2000, {2000}).left == render(b, 2000, {2000}).left);
    CHECK(a.diagnostics().clippedInputSamples == 2);
    CHECK(b.diagnostics().clippedInputSamples == 0);
}

TEST_CASE("Invalid events from a pull source are dropped and counted", "[unit][stream]")
{
    Source clean = sineSource(8000, 500.0, 44100.0);
    Source bad = clean;
    bad.badEvents = true;
    DacStream a = pulling(config(48000, {44100, 1}), clean);
    DacStream b = pulling(config(48000, {44100, 1}), bad);
    CHECK(render(a, 4000, {256}).left == render(b, 4000, {256}).left);
    CHECK(b.diagnostics().droppedEvents > 0);
}

TEST_CASE("Push and pull give the same output", "[unit][stream][push]")
{
    const double host = 48000;
    const std::size_t frames = 12000;
    Source src = sineSource(20000, 997.0, pulledDown.hz());
    src.events = {ControlEvent::softMute(4000, true), ControlEvent::softMute(4600, false)};
    Source copy = src;
    DacStream pull = pulling(config(host, pulledDown), copy);
    const Stereo expected = render(pull, frames, {frames});

    DacStreamConfig c = config(host, pulledDown);
    c.mode = DacStreamMode::push;
    c.pushCapacityFrames = 4096;
    DacStream push;
    REQUIRE(push.prepare(c) == Status::ok);
    Stereo out{std::vector<double>(frames), std::vector<double>(frames)};
    const auto sizes = randomSizes(13, 60, 700);
    std::size_t at = 0, block = 0, fed = 0, nextEvent = 0;
    while (at < frames) {
        const std::size_t n = std::min(sizes[block++ % sizes.size()], frames - at);
        const std::size_t need = push.dacFramesFor(n);
        std::vector<ControlEvent> events;
        while (nextEvent < src.events.size() && src.events[nextEvent].offset < fed + need) {
            ControlEvent e = src.events[nextEvent++];
            e.offset -= static_cast<std::uint32_t>(fed);
            events.push_back(e);
        }
        const PushResult r = push.push(StereoView<const std::int32_t>{std::span(src.il).subspan(fed, need),
                                                                      std::span(src.ir).subspan(fed, need)},
                                       events);
        REQUIRE(r.status == Status::ok);
        REQUIRE(r.framesAccepted == need);
        fed += need;
        REQUIRE(push.queuedFrames() == need);
        REQUIRE(push.render(StereoView<double>{std::span(out.left).subspan(at, n),
                                               std::span(out.right).subspan(at, n)})
                == Status::ok);
        REQUIRE(push.queuedFrames() == 0);
        at += n;
    }
    CHECK(out.left == expected.left);
    CHECK(out.right == expected.right);
    CHECK(push.diagnostics().sourceUnderrunFrames == 0);
}

TEST_CASE("Push from another thread matches pull", "[unit][stream][push]")
{
    const std::size_t frames = 24000;
    Source src = sineSource(30000, 1500.0, 44100.0);
    Source copy = src;
    DacStream pull = pulling(config(48000, {44100, 1}), copy);
    const Stereo expected = render(pull, frames, {frames});

    DacStreamConfig c = config(48000, {44100, 1});
    c.mode = DacStreamMode::push;
    c.pushCapacityFrames = 1024;
    DacStream push;
    REQUIRE(push.prepare(c) == Status::ok);
    const std::size_t total = push.dacFramesFor(frames); // exactly what the renders consume
    REQUIRE(total <= src.il.size());
    std::thread producer([&] {
        std::size_t fed = 0;
        while (fed < total) {
            const std::size_t n = std::min<std::size_t>({97, push.freeFrames(), total - fed});
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            const PushResult r = push.push(StereoView<const std::int32_t>{std::span(src.il).subspan(fed, n),
                                                                          std::span(src.ir).subspan(fed, n)},
                                           {});
            fed += r.framesAccepted;
        }
    });
    Stereo out{std::vector<double>(frames), std::vector<double>(frames)};
    for (std::size_t at = 0; at < frames;) {
        const std::size_t n = std::min<std::size_t>(128, frames - at);
        while (push.queuedFrames() < push.dacFramesFor(n)) {
            std::this_thread::yield();
        }
        REQUIRE(push.render(StereoView<double>{std::span(out.left).subspan(at, n),
                                               std::span(out.right).subspan(at, n)})
                == Status::ok);
        at += n;
    }
    producer.join();
    CHECK(out.left == expected.left);
    CHECK(push.diagnostics().sourceUnderrunFrames == 0);
    CHECK(push.diagnostics().sourceOverrunFrames == 0);
}

TEST_CASE("Push underruns play zeros and overruns drop frames", "[unit][stream][push]")
{
    DacStreamConfig c = config(48000, {44100, 1});
    c.mode = DacStreamMode::push;
    c.pushCapacityFrames = 64;
    c.pushCapacityEvents = 4;
    DacStream s;
    REQUIRE(s.prepare(c) == Status::ok);

    const std::size_t need = s.dacFramesFor(500);
    std::vector<double> l(500, 1.0), r(500, 1.0);
    REQUIRE(s.render(StereoView<double>{l, r}) == Status::ok);
    CHECK(s.diagnostics().sourceUnderrunFrames == need);
    CHECK(std::all_of(l.begin(), l.end(), [](double x) { return x == 0.0; }));

    std::vector<std::int32_t> data(100, 1000);
    const std::vector<ControlEvent> events{ControlEvent::softMute(10, true), ControlEvent::softMute(80, false)};
    const PushResult p = s.push(StereoView<const std::int32_t>{data, data}, events);
    CHECK(p.status == Status::ok);
    CHECK(p.framesAccepted == 64);
    CHECK(s.queuedFrames() == 64);
    CHECK(s.freeFrames() == 0);
    CHECK(s.diagnostics().sourceOverrunFrames == 36);
    CHECK(s.diagnostics().droppedEvents == 1); // the event at frame 80 was not accepted
}

TEST_CASE("Push rejects mismatched calls", "[unit][stream][push]")
{
    std::vector<std::int32_t> ints(8);
    std::vector<float> floats(8), shorter(7);
    Source src;
    DacStream pull = pulling(config(48000, {44100, 1}), src);
    CHECK(pull.push(StereoView<const std::int32_t>{ints, ints}, {}).status == Status::modeMismatch);

    DacStreamConfig c = config(48000, {44100, 1}, DacSampleFormat::int24);
    c.mode = DacStreamMode::push;
    DacStream s;
    REQUIRE(s.prepare(c) == Status::ok);
    CHECK(s.push(StereoView<const float>{floats, floats}, {}).status == Status::formatMismatch);
    CHECK(s.push(StereoView<const std::int32_t>{ints, std::span<const std::int32_t>(ints).first(7)}, {}).status
          == Status::channelSizeMismatch);
    const std::vector<ControlEvent> late{ControlEvent::softMute(8, true)};
    CHECK(s.push(StereoView<const std::int32_t>{ints, ints}, late).status == Status::eventOutOfRange);
    CHECK(s.push(StereoView<const std::int32_t>{ints, ints}, {}).framesAccepted == 8);
    CHECK(s.diagnostics().rejectedBlocks == 3);

    DacStream unprepared;
    std::vector<double> out(4);
    CHECK(unprepared.render(StereoView<double>{out, out}) == Status::notPrepared);
    CHECK(unprepared.push(StereoView<const float>{floats, floats}, {}).status == Status::notPrepared);
    static_cast<void>(shorter);
}

TEST_CASE("Stream reset returns to the state after prepare", "[unit][stream]")
{
    Source src = sineSource(10000, 300.0, 44100.0);
    src.events = {ControlEvent::softMute(100, true)};
    DacStream s = pulling(config(48000, {44100, 1}), src);
    const Stereo first = render(s, 4000, {256});
    s.reset();
    src.position = 0;
    src.nextEvent = 0;
    const Stereo second = render(s, 4000, {256});
    CHECK(first.left == second.left);
}

TEST_CASE("Rendering and pushing do not allocate", "[unit][stream][rt]")
{
    Source src = sineSource(10000, 300.0, 44100.0);
    src.events = {ControlEvent::softMute(5, true), ControlEvent::powerDown(1000, false)};
    DacStream pull = pulling(config(48000, pulledDown, DacSampleFormat::int20, 256), src);

    DacStreamConfig c = config(48000, pulledDown, DacSampleFormat::float32, 256);
    c.mode = DacStreamMode::push;
    DacStream push;
    REQUIRE(push.prepare(c) == Status::ok);
    std::vector<float> in(1500, 0.25f), outL(2048), outR(2048);
    const std::vector<ControlEvent> events{ControlEvent::softMute(3, true)};

    test::AllocationCounter counter;
    const Status a = pull.render(StereoView<float>{outL, outR});
    const PushResult b = push.push(StereoView<const float>{in, in}, events);
    const Status d = push.render(StereoView<float>{outL, outR});
    pull.reset();
    static_cast<void>(pull.latency());
    static_cast<void>(push.diagnostics());
    static_cast<void>(push.dacFramesFor(100));
    CHECK(counter.allocations() == 0);
    CHECK(a == Status::ok);
    CHECK(b.status == Status::ok);
    CHECK(d == Status::ok);
}

TEST_CASE("Optional dither makes a sub-LSB level audible on average", "[unit][stream][pcm]")
{
    // 0.3 LSB of DC: quantized away without dither; with TPDF dither the
    // average output keeps it (P-12).
    const double level = 0.3 / 524288.0;
    auto run = [&](bool dither, std::uint64_t seed, DacSampleFormat format) {
        Source src;
        src.dl.assign(30000, level);
        src.dr.assign(30000, level);
        src.il.assign(30000, 0);
        src.ir.assign(30000, 0);
        DacStreamConfig c = config(44100, {44100, 1}, format);
        c.inputDither = dither;
        c.ditherSeed = seed;
        DacStream s = pulling(c, src);
        return render(s, 28000, {512});
    };
    auto mean = [](const std::vector<double>& x) {
        double sum = 0.0;
        for (std::size_t i = 2000; i < x.size(); ++i) {
            sum += x[i];
        }
        return sum / static_cast<double>(x.size() - 2000);
    };
    const Stereo plain = run(false, 1, DacSampleFormat::float64);
    CHECK(mean(plain.left) == 0.0);
    const Stereo dithered = run(true, 1, DacSampleFormat::float64);
    CHECK(std::abs(mean(dithered.left) / level - 1.0) < 0.1);
    CHECK(std::abs(mean(dithered.right) / level - 1.0) < 0.1);
    CHECK(dithered.left != dithered.right); // independent streams per channel

    CHECK(run(true, 1, DacSampleFormat::float64).left == dithered.left); // seeded, reproducible
    CHECK(run(true, 2, DacSampleFormat::float64).left != dithered.left);
    CHECK(run(true, 1, DacSampleFormat::float32).left != plain.left);
    // Integer input is exact and never dithered.
    CHECK(run(true, 1, DacSampleFormat::int20).left == run(false, 1, DacSampleFormat::int20).left);
}

TEST_CASE("Dithered push and pull give the same output", "[unit][stream][push]")
{
    std::vector<double> data(6000);
    for (std::size_t j = 0; j < data.size(); ++j) {
        data[j] = 1e-6 * std::sin(0.01 * static_cast<double>(j));
    }
    Source src;
    src.dl = src.dr = data;
    DacStreamConfig c = config(48000, {44100, 1}, DacSampleFormat::float64);
    c.inputDither = true;
    DacStream pull = pulling(c, src);
    const Stereo expected = render(pull, 4000, {4000});

    c.mode = DacStreamMode::push;
    DacStream push;
    REQUIRE(push.prepare(c) == Status::ok);
    const std::size_t need = push.dacFramesFor(4000);
    REQUIRE(push.push(StereoView<const double>{std::span(data).first(need), std::span(data).first(need)}, {})
                .framesAccepted
            == need);
    const Stereo out = render(push, 4000, {4000});
    CHECK(out.left == expected.left);
}

TEST_CASE("The source is not called when a render needs no frame", "[unit][stream]")
{
    // At 192 kHz with a 44.1 kHz source, a one-frame render often needs no new DAC frame.
    struct Counting {
        std::size_t calls = 0, zero = 0;
        static void pull(void* c, DacPullBlock& b) noexcept
        {
            auto& s = *static_cast<Counting*>(c);
            ++s.calls;
            s.zero += b.frames == 0 ? 1 : 0;
            for (std::size_t j = 0; j < b.frames; ++j) {
                b.integers.left[j] = b.integers.right[j] = 0;
            }
        }
    };
    Counting src;
    DacStreamConfig c = config(192000, {44100, 1});
    c.pull = &Counting::pull;
    c.pullContext = &src;
    DacStream s;
    REQUIRE(s.prepare(c) == Status::ok);
    std::vector<double> l(1), r(1);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(s.render(StereoView<double>{l, r}) == Status::ok);
    }
    CHECK(src.zero == 0);
    CHECK(src.calls > 0);
    CHECK(src.calls < 1000); // about 230 frames were needed for 1000 host frames
}
