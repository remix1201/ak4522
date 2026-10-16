// The C API gives the same results as the C++ API it wraps (REQ-API-02).

#include "ak4522/ak4522.h"
#include "ak4522/dac_stream.hpp"
#include "ak4522/host_processor.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <vector>

using namespace ak4522;

namespace {

constexpr double pi = 3.14159265358979323846;

std::vector<double> tone(std::size_t frames, double freq, double rate)
{
    std::vector<double> x(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        x[n] = 0.5 * std::sin(2.0 * pi * freq * static_cast<double>(n) / rate);
    }
    return x;
}

struct IntSource {
    std::vector<std::int32_t> data;
    std::size_t position = 0;

    static void pullCpp(void* context, DacPullBlock& b) noexcept
    {
        auto& s = *static_cast<IntSource*>(context);
        for (std::size_t j = 0; j < b.frames; ++j) {
            b.integers.left[j] = b.integers.right[j] = s.data[(s.position + j) % s.data.size()];
        }
        if (s.position == 0 && b.frames > 50) {
            b.events[0] = ControlEvent::deemphasis(50, DeemphasisSelect::fs44100);
            b.eventCount = 1;
        }
        s.position += b.frames;
    }

    static void pullC(void* context, ak4522_pull_block* b)
    {
        auto& s = *static_cast<IntSource*>(context);
        for (std::size_t j = 0; j < b->frames; ++j) {
            b->int_left[j] = b->int_right[j] = s.data[(s.position + j) % s.data.size()];
        }
        if (s.position == 0 && b->frames > 50) {
            b->events[0] = {50, AK4522_EVENT_DEEMPHASIS, AK4522_DEEMPHASIS_44100};
            b->event_count = 1;
        }
        s.position += b->frames;
    }
};

} // namespace

TEST_CASE("C defaults equal the C++ defaults", "[unit][capi]")
{
    ak4522_host_config h;
    ak4522_host_config_default(&h);
    const HostConfig cpp;
    CHECK(h.host_rate == cpp.hostRate);
    CHECK(h.max_block_frames == cpp.maxBlockFrames);
    CHECK(h.dither_seed == cpp.ditherSeed);
    CHECK(h.dac.dac_rate == cpp.dac.dacRate);
    CHECK(h.dac.analog.noise_seed == cpp.dac.analog.noiseSeed);
    CHECK(h.dac.master_clock_hz == 0.0);

    ak4522_stream_config s;
    ak4522_stream_config_default(&s);
    const DacStreamConfig sc;
    CHECK(s.dac_rate_numerator == sc.dacRate.numerator);
    CHECK(s.dac_rate_denominator == sc.dacRate.denominator);
    CHECK(s.push_capacity_frames == sc.pushCapacityFrames);
    CHECK(s.format == static_cast<int32_t>(sc.format));
}

TEST_CASE("C host processing equals HostProcessor", "[unit][capi]")
{
    // Each variant sets one field on both sides, so a field mapped to the
    // wrong C++ member changes the output.
    const int variant = GENERATE(range(0, 14));
    CAPTURE(variant);
    HostConfig cfg;
    ak4522_host_config c;
    ak4522_host_config_default(&c);
    auto nominal = [&] {
        cfg.dac.profile = ModelProfile::nominalAnalog;
        c.dac.profile = AK4522_PROFILE_NOMINAL_ANALOG;
    };
    switch (variant) {
    case 0: break;
    case 1: nominal(); cfg.dac.analog.noise = c.dac.analog.noise = true; break;
    case 2: nominal(); cfg.dac.analog.distortion = c.dac.analog.distortion = true; break;
    case 3: nominal(); cfg.dac.analog.channelMismatch = c.dac.analog.channel_mismatch = true; break;
    case 4: nominal(); cfg.dac.analog.crosstalk = c.dac.analog.crosstalk = true; break;
    case 5: nominal(); cfg.dac.analog.dcOffset = c.dac.analog.dc_offset = true; break;
    case 6:
        nominal();
        cfg.dac.analog.distortion = c.dac.analog.distortion = true;
        cfg.dac.analog.distortionShape = DistortionShape::mostly2nd;
        c.dac.analog.distortion_shape = AK4522_DISTORTION_MOSTLY_2ND;
        break;
    case 7:
        nominal();
        cfg.dac.analog.crosstalk = c.dac.analog.crosstalk = true;
        cfg.dac.analog.crosstalkShape = CrosstalkShape::flat;
        c.dac.analog.crosstalk_shape = AK4522_CROSSTALK_FLAT;
        break;
    case 8:
        nominal();
        cfg.dac.analog.channelMismatch = c.dac.analog.channel_mismatch = true;
        cfg.dac.analog.mismatchMode = MismatchMode::perInstance;
        c.dac.analog.mismatch_mode = AK4522_MISMATCH_PER_INSTANCE;
        cfg.dac.analog.noiseSeed = c.dac.analog.noise_seed = 12345;
        break;
    case 9:
        cfg.dac.initialDeemphasis = DeemphasisSelect::fs32000;
        c.dac.initial_deemphasis = AK4522_DEEMPHASIS_32000;
        break;
    case 10: cfg.dac.initialSoftMute = c.dac.initial_soft_mute = true; break;
    case 11:
        cfg.inputResolution = InputResolution::bits16;
        c.input_resolution = AK4522_RESOLUTION_16;
        break;
    case 12:
        cfg.inputDither = c.input_dither = true;
        cfg.ditherSeed = c.dither_seed = 777;
        break;
    case 13:
        cfg.hostRate = c.host_rate = 96000.0;
        cfg.dac.dacRate = c.dac.dac_rate = 32000.0;
        cfg.maxBlockFrames = c.max_block_frames = 100;
        break;
    }
    const auto in = tone(6000, 997.0, cfg.hostRate);
    HostProcessor cpp;
    REQUIRE(cpp.prepare(cfg) == Status::ok);
    std::vector<double> a(in.size()), ar(in.size());
    const std::vector<ControlEvent> events{ControlEvent::softMute(3000, true)};
    REQUIRE(cpp.process(StereoView<const double>{in, in}, StereoView<double>{a, ar}, events) == Status::ok);

    ak4522_host* host = ak4522_host_create();
    REQUIRE(ak4522_host_prepare(host, &c) == AK4522_OK);
    std::vector<double> b(in.size()), br(in.size());
    const ak4522_event ce{3000, AK4522_EVENT_SOFT_MUTE, 1};
    REQUIRE(ak4522_host_process_f64(host, in.data(), in.data(), b.data(), br.data(), in.size(), &ce, 1) == AK4522_OK);
    CHECK(a == b);
    CHECK(ar == br);

    ak4522_latency l;
    REQUIRE(ak4522_host_latency(host, &l) == AK4522_OK);
    CHECK(l.reported_host_frames == cpp.latency().reportedHostFrames);
    CHECK(l.alignment_host_frames == cpp.latency().alignmentHostFrames);
    ak4522_diagnostics d;
    REQUIRE(ak4522_host_diagnostics(host, &d) == AK4522_OK);
    CHECK(d.clipped_input_samples == cpp.diagnostics().clippedInputSamples);
    ak4522_host_destroy(host);
}

TEST_CASE("C master clock and CMODE reach the validation", "[unit][capi]")
{
    ak4522_host_config c;
    ak4522_host_config_default(&c);
    ak4522_host* host = ak4522_host_create();
    c.dac.cmode = AK4522_CMODE_HIGH;
    c.dac.master_clock_hz = 384.0 * 44100.0;
    CHECK(ak4522_host_prepare(host, &c) == AK4522_OK);
    c.dac.master_clock_hz = 256.0 * 44100.0;
    CHECK(ak4522_host_prepare(host, &c) == AK4522_INVALID_MASTER_CLOCK);
    c.dac.cmode = AK4522_CMODE_LOW;
    CHECK(ak4522_host_prepare(host, &c) == AK4522_OK);
    c.dac.dac_rate = 50000.0;
    c.dac.master_clock_hz = 0.0;
    CHECK(ak4522_host_prepare(host, &c) == AK4522_INVALID_DAC_RATE);
    c.dac.allow_extended_rate = true;
    CHECK(ak4522_host_prepare(host, &c) == AK4522_OK);
    ak4522_host_destroy(host);
}

TEST_CASE("C stream pull equals DacStream", "[unit][capi][stream]")
{
    IntSource srcCpp, srcC;
    for (int j = 0; j < 4000; ++j) {
        srcCpp.data.push_back(static_cast<std::int32_t>(300000.0 * std::sin(0.05 * j)));
    }
    srcC.data = srcCpp.data;

    DacStreamConfig cfg;
    cfg.dacRate = {44100000, 1001};
    cfg.pull = &IntSource::pullCpp;
    cfg.pullContext = &srcCpp;
    DacStream cpp;
    REQUIRE(cpp.prepare(cfg) == Status::ok);
    std::vector<float> a(5000), ar(5000);
    REQUIRE(cpp.render(StereoView<float>{a, ar}) == Status::ok);

    ak4522_stream_config c;
    ak4522_stream_config_default(&c);
    c.dac_rate_numerator = 44100000;
    c.dac_rate_denominator = 1001;
    c.pull = &IntSource::pullC;
    c.pull_context = &srcC;
    ak4522_stream* s = ak4522_stream_create();
    REQUIRE(ak4522_stream_prepare(s, &c) == AK4522_OK);
    std::vector<float> b(5000), br(5000);
    for (std::size_t at = 0; at < b.size(); at += 1000) { // block sizes do not matter
        REQUIRE(ak4522_stream_render_f32(s, b.data() + at, br.data() + at, 1000) == AK4522_OK);
    }
    CHECK(a == b);

    ak4522_diagnostics d;
    REQUIRE(ak4522_stream_diagnostics(s, &d) == AK4522_OK);
    CHECK(d.dropped_events == 0);
    ak4522_stream_destroy(s);
}

TEST_CASE("Malformed events from a C pull source are dropped", "[unit][capi][stream]")
{
    struct Bad {
        static void pull(void*, ak4522_pull_block* b)
        {
            for (std::size_t j = 0; j < b->frames; ++j) {
                b->int_left[j] = b->int_right[j] = 0;
            }
            b->events[0] = {0, 9, 0}; // no such pin
            b->event_count = 1;
        }
    };
    ak4522_stream_config c;
    ak4522_stream_config_default(&c);
    c.pull = &Bad::pull;
    ak4522_stream* s = ak4522_stream_create();
    REQUIRE(ak4522_stream_prepare(s, &c) == AK4522_OK);
    std::vector<double> l(100), r(100);
    REQUIRE(ak4522_stream_render_f64(s, l.data(), r.data(), l.size()) == AK4522_OK);
    ak4522_diagnostics d;
    REQUIRE(ak4522_stream_diagnostics(s, &d) == AK4522_OK);
    CHECK(d.dropped_events > 0);
    ak4522_stream_destroy(s);
}

TEST_CASE("C processing does not allocate", "[unit][capi][rt]")
{
    ak4522_host_config hc;
    ak4522_host_config_default(&hc);
    ak4522_host* host = ak4522_host_create();
    REQUIRE(ak4522_host_prepare(host, &hc) == AK4522_OK);
    ak4522_stream_config sc;
    ak4522_stream_config_default(&sc);
    sc.mode = AK4522_MODE_PUSH;
    ak4522_stream* stream = ak4522_stream_create();
    REQUIRE(ak4522_stream_prepare(stream, &sc) == AK4522_OK);
    std::vector<float> in(1024, 0.1f), out(1024), outR(1024);
    std::vector<std::int32_t> pcm(1024, 1000);
    const ak4522_event e{10, AK4522_EVENT_SOFT_MUTE, 1};
    std::size_t accepted = 0;

    test::AllocationCounter counter;
    const ak4522_status a = ak4522_host_process_f32(host, in.data(), in.data(), out.data(), outR.data(), 1024, &e, 1);
    const ak4522_status b = ak4522_stream_push_i32(stream, pcm.data(), pcm.data(), 1024, &e, 1, &accepted);
    const ak4522_status c = ak4522_stream_render_f32(stream, out.data(), outR.data(), 1024);
    ak4522_diagnostics d;
    static_cast<void>(ak4522_stream_diagnostics(stream, &d));
    CHECK(counter.allocations() == 0);
    CHECK(a == AK4522_OK);
    CHECK(b == AK4522_OK);
    CHECK(c == AK4522_OK);
    ak4522_host_destroy(host);
    ak4522_stream_destroy(stream);
}
