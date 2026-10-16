// DacCore, the public chip model (REQ-ARCH-02, REQ-ARCH-03, REQ-RT-01,
// REQ-RT-02, REQ-LAT-01; P-40, P-71, P-72). The frequency-response, image,
// and delay gates are measured on DacCore's output by CTest
// dsp.dac_core_response_*; these tests cover wiring, control behavior,
// streaming, and real-time safety.

#include "ak4522/dac_core.hpp"
#include "core/soft_mute.hpp"
#include "dsp/deemphasis.hpp"
#include "dsp/interpolator.hpp"
#include "dsp/reconstruction.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace ak4522;

namespace {

constexpr std::size_t os = DacCore::oversampling;

struct Out {
    std::vector<double> left;
    std::vector<double> right;
};

DacCore prepared(const DacConfig& config = {}, std::size_t maxBlock = 512)
{
    DacCore core;
    REQUIRE(core.prepare(config, maxBlock) == Status::ok);
    return core;
}

Out run(DacCore& core, const std::vector<Pcm20Frame>& input, const std::vector<ControlEvent>& events = {})
{
    Out out{std::vector<double>(input.size() * os), std::vector<double>(input.size() * os)};
    const ProcessResult result = core.process(input, events, {out.left, out.right});
    REQUIRE(result.status == Status::ok);
    REQUIRE(result.framesWritten == input.size() * os);
    return out;
}

std::vector<Pcm20Frame> randomPcm(std::size_t frames, std::uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int32_t> code(Pcm20::minCode, Pcm20::maxCode);
    std::vector<Pcm20Frame> x(frames);
    for (auto& f : x) {
        f = {Pcm20{code(rng)}, Pcm20{code(rng)}};
    }
    return x;
}

// The linear chain built directly from the internal components, as an
// independent check of DacCore's wiring.
std::vector<double> referenceChain(const std::vector<Pcm20Frame>& input, DeemphasisSelect dem)
{
    detail::Deemphasis deemphasis;
    deemphasis.select(dem);
    detail::Interpolator8x interpolator;
    detail::PipelineDelay pipeline;
    detail::Reconstruction reconstruction;
    std::vector<double> l(input.size());
    std::vector<double> r(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        l[i] = toNormalized(input[i].left);
        r[i] = toNormalized(input[i].right);
    }
    deemphasis.process(l, r);
    std::vector<double> ol(input.size() * os);
    std::vector<double> orr(ol.size());
    interpolator.process(l, r, ol, orr);
    pipeline.process(ol, orr);
    reconstruction.process(ol, orr);
    return ol;
}

// As referenceChain, with SMUTE going high at muteFrame: de-emphasis is
// applied before the mute gain (P-40).
std::vector<double> referenceChainWithMute(const std::vector<Pcm20Frame>& input, DeemphasisSelect dem,
                                           std::size_t muteFrame)
{
    detail::Deemphasis deemphasis;
    deemphasis.select(dem);
    detail::SoftMute mute;
    mute.reset(false);
    std::vector<double> l(input.size());
    std::vector<double> r(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        l[i] = toNormalized(input[i].left);
        r[i] = toNormalized(input[i].right);
    }
    deemphasis.process(l, r);
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (i == muteFrame) {
            mute.setPin(true);
        }
        const double g = mute.nextGain();
        l[i] *= g;
        r[i] *= g;
    }
    detail::Interpolator8x interpolator;
    detail::PipelineDelay pipeline;
    detail::Reconstruction reconstruction;
    std::vector<double> ol(input.size() * os);
    std::vector<double> orr(ol.size());
    interpolator.process(l, r, ol, orr);
    pipeline.process(ol, orr);
    reconstruction.process(ol, orr);
    return ol;
}

bool allZero(std::span<const double> v)
{
    return std::all_of(v.begin(), v.end(), [](double x) { return x == 0.0; });
}

} // namespace

TEST_CASE("Prepare validates the configuration", "[unit][core]")
{
    DacCore core;
    DacConfig config;
    config.dacRate = 96000.0;
    CHECK(core.prepare(config, 64) == Status::invalidDacRate);
    CHECK_FALSE(core.isPrepared());
    CHECK(core.prepare(DacConfig{}, 0) == Status::invalidBlockSize);

    config = DacConfig{};
    config.profile = ModelProfile::measured;
    CHECK(core.prepare(config, 64) == Status::invalidConfiguration);

    // The Specification profile has no imperfections (REQ-SCOPE-03).
    config = DacConfig{};
    config.analog.noise = true;
    CHECK(core.prepare(config, 64) == Status::invalidConfiguration);
    // NominalAnalog accepts the options implemented so far.
    config.profile = ModelProfile::nominalAnalog;
    CHECK(core.prepare(config, 64) == Status::ok);
    config.analog.distortion = true; // not yet implemented
    CHECK(core.prepare(config, 64) == Status::invalidConfiguration);

    CHECK(core.prepare(DacConfig{}, 64) == Status::ok);
    CHECK(core.isPrepared());
    CHECK(core.outputRate() == 352800.0);
}

TEST_CASE("Process rejects bad calls without side effects", "[unit][core]")
{
    DacCore unprepared;
    std::vector<double> l(80), r(80);
    const std::vector<Pcm20Frame> ten(10);
    CHECK(unprepared.process(ten, {}, {l, r}).status == Status::notPrepared);

    DacCore core = prepared();
    std::vector<double> shortL(79), shortR(79);
    CHECK(core.process(ten, {}, {shortL, shortR}).status == Status::insufficientOutputCapacity);
    std::vector<double> otherR(81);
    CHECK(core.process(ten, {}, {l, otherR}).status == Status::channelSizeMismatch);
    const std::vector<ControlEvent> bad{ControlEvent::softMute(10, true)};
    CHECK(core.process(ten, bad, {l, r}).status == Status::eventOutOfRange);
    CHECK(core.diagnostics().rejectedBlocks == 3);

    // The rejected mute event was not applied: a later block is unmuted.
    std::vector<Pcm20Frame> dc(400, {Pcm20{1 << 18}, Pcm20{1 << 18}});
    const Out out = run(core, dc);
    CHECK(out.left.back() > 0.49);
}

TEST_CASE("Output equals the component chain for each de-emphasis selection", "[unit][core]")
{
    const DeemphasisSelect dem = GENERATE(DeemphasisSelect::off, DeemphasisSelect::fs44100,
                                          DeemphasisSelect::fs32000);
    CAPTURE(static_cast<int>(dem));
    DacConfig config;
    config.initialDeemphasis = dem;
    DacCore core = prepared(config);
    const auto input = randomPcm(700, 21);
    CHECK(run(core, input).left == referenceChain(input, dem));
}

TEST_CASE("De-emphasis is applied before the mute ramp", "[unit][core][mute][deemph]")
{
    // A time-varying gain and a filter do not commute, so this distinguishes
    // the documented order (P-40) from the reverse.
    DacConfig config;
    config.initialDeemphasis = DeemphasisSelect::fs44100;
    DacCore core = prepared(config);
    const auto input = randomPcm(1600, 31);
    const Out out = run(core, input, {ControlEvent::softMute(200, true)});
    CHECK(out.left == referenceChainWithMute(input, DeemphasisSelect::fs44100, 200));
}

TEST_CASE("Zero-frame blocks are valid", "[unit][core]")
{
    DacCore core = prepared();
    std::vector<double> l, r;
    const ProcessResult result = core.process({}, {}, {l, r});
    CHECK(result.status == Status::ok);
    CHECK(result.framesWritten == 0);
}

TEST_CASE("Soft mute silences the analog output after the ramp and filter delay", "[unit][core][mute]")
{
    DacCore core = prepared();
    std::vector<Pcm20Frame> dc(3000, {Pcm20{1 << 18}, Pcm20{-(1 << 18)}});
    const Out out = run(core, dc, {ControlEvent::softMute(100, true)});
    // Before the ramp starts (allowing for the 14.7-frame delay) the output
    // has settled at the input level.
    CHECK(std::abs(out.left[90 * os] - 0.5) < 0.01);
    // 1024 ramp frames, then the FIR length and the sections' decay: exact
    // zeros well before the end of the block.
    const std::size_t silentFrom = (100 + 1024 + 30) * os + 3000;
    CHECK(allZero(std::span(out.left).subspan(silentFrom)));
    CHECK(allZero(std::span(out.right).subspan(silentFrom)));
    // The ramp is not instantaneous at the output (D-M05).
    CHECK(std::abs(out.left[(100 + 512) * os]) > 0.1);
}

TEST_CASE("Power-down outputs zero and restarts from a clean state", "[unit][core]")
{
    const auto input = randomPcm(2000, 22);
    DacCore core = prepared();
    const Out out = run(core, input,
                        {ControlEvent::powerDown(500, false), ControlEvent::powerDown(900, true)});
    CHECK(allZero(std::span(out.left).subspan(500 * os, 400 * os)));
    CHECK(allZero(std::span(out.right).subspan(500 * os, 400 * os)));

    // After release the output equals a fresh core fed the input from that frame.
    DacCore fresh = prepared();
    const std::vector<Pcm20Frame> tail(input.begin() + 900, input.end());
    const Out expected = run(fresh, tail);
    CHECK(std::vector<double>(out.left.begin() + 900 * os, out.left.end()) == expected.left);
}

TEST_CASE("SMUTE held high through power-down starts muted on release", "[unit][core][mute]")
{
    DacCore core = prepared();
    std::vector<Pcm20Frame> dc(600, {Pcm20{1 << 18}, Pcm20{1 << 18}});
    const Out out = run(core, dc,
                        {ControlEvent::powerDown(0, false), ControlEvent::softMute(10, true),
                         ControlEvent::powerDown(20, true)});
    CHECK(allZero(out.left)); // muted from release, no ramp (P-72)
}

TEST_CASE("De-emphasis change applies at its frame", "[unit][core][deemph]")
{
    const auto input = randomPcm(1200, 23);
    DacCore core = prepared();
    const Out out = run(core, input, {ControlEvent::deemphasis(600, DeemphasisSelect::fs48000)});
    // Before the change, and until the change reaches the output, the result
    // matches the OFF chain exactly.
    const auto off = referenceChain(input, DeemphasisSelect::off);
    CHECK(std::equal(out.left.begin(), out.left.begin() + 600 * os, off.begin()));
    CHECK_FALSE(std::equal(out.left.begin() + 600 * os, out.left.end(), off.begin() + 600 * os));
}

TEST_CASE("Output is independent of block partitioning and chunk size", "[unit][core][streaming]")
{
    const auto input = randomPcm(4000, 24);
    const std::vector<ControlEvent> absolute{
        ControlEvent::deemphasis(300, DeemphasisSelect::fs44100), ControlEvent::softMute(1000, true),
        ControlEvent::softMute(1500, false), ControlEvent::powerDown(2500, false),
        ControlEvent::powerDown(2600, true), ControlEvent::deemphasis(2600, DeemphasisSelect::off)};

    auto partitioned = [&](const std::vector<std::size_t>& sizes, std::size_t maxBlock) {
        DacCore core = prepared({}, maxBlock);
        Out out{std::vector<double>(input.size() * os), std::vector<double>(input.size() * os)};
        std::size_t at = 0, next = 0, block = 0;
        while (at < input.size()) {
            const std::size_t n = std::min(sizes[block++ % sizes.size()], input.size() - at);
            std::vector<ControlEvent> local;
            while (next < absolute.size() && absolute[next].offset < at + n) {
                ControlEvent e = absolute[next++];
                e.offset -= static_cast<std::uint32_t>(at);
                local.push_back(e);
            }
            const auto result = core.process(std::span(input).subspan(at, n), local,
                                             {std::span(out.left).subspan(at * os, n * os),
                                              std::span(out.right).subspan(at * os, n * os)});
            REQUIRE(result.status == Status::ok);
            at += n;
        }
        return out;
    };

    const Out reference = partitioned({input.size()}, input.size());
    for (const auto& [sizes, maxBlock] : std::vector<std::pair<std::vector<std::size_t>, std::size_t>>{
             {{1}, 512}, {{0, 3, 64, 511, 1000}, 512}, {{4000}, 7}, {{333, 1}, 64}}) {
        const Out out = partitioned(sizes, maxBlock);
        CHECK(out.left == reference.left);
        CHECK(out.right == reference.right);
    }
}

TEST_CASE("Blocks larger than the prepared maximum are chunked and counted", "[unit][core]")
{
    DacCore core = prepared({}, 16);
    const auto input = randomPcm(100, 25);
    static_cast<void>(run(core, input));
    CHECK(core.diagnostics().chunkedBlocks == 1);
}

TEST_CASE("Reset returns to the state after prepare", "[unit][core]")
{
    const auto input = randomPcm(500, 26);
    DacConfig config;
    config.initialDeemphasis = DeemphasisSelect::fs32000;
    DacCore core = prepared(config);
    static_cast<void>(run(core, randomPcm(300, 27), {ControlEvent::softMute(5, true),
                                                    ControlEvent::deemphasis(50, DeemphasisSelect::off)}));
    core.reset();
    DacCore fresh = prepared(config);
    CHECK(run(core, input).left == run(fresh, input).left);
}

TEST_CASE("Repeated pin levels are counted as ignored", "[unit][core]")
{
    DacCore core = prepared();
    static_cast<void>(run(core, randomPcm(10, 28),
                          {ControlEvent::softMute(0, false), ControlEvent::powerDown(1, true)}));
    CHECK(core.diagnostics().ignoredEvents == 2);
}

TEST_CASE("Latency reports the 14.7-frame core delay", "[unit][core]")
{
    DacConfig config;
    config.dacRate = 48000.0;
    DacCore core = prepared(config);
    const LatencyInfo latency = core.latency();
    CHECK(latency.dacRate == 48000.0);
    CHECK(latency.coreDacFrames == 14.7);
    CHECK(std::abs(latency.totalSeconds - 306.25e-6) < 1e-12); // I-F05
}

TEST_CASE("A moved core keeps working and the source is unprepared", "[unit][core]")
{
    DacCore source = prepared();
    DacCore target = std::move(source);
    CHECK(target.isPrepared());
    CHECK_FALSE(source.isPrepared()); // NOLINT(bugprone-use-after-move)
    static_cast<void>(run(target, randomPcm(10, 29)));
}

TEST_CASE("Full-scale input stays finite and bounded", "[unit][core]")
{
    DacCore core = prepared();
    std::vector<Pcm20Frame> extremes(2000);
    for (std::size_t i = 0; i < extremes.size(); ++i) {
        const Pcm20 v{(i % 2 == 0) ? Pcm20::maxCode : Pcm20::minCode};
        extremes[i] = {v, v};
    }
    const Out out = run(core, extremes);
    for (const double v : out.left) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) < 4.0); // FIR overshoot bound well above any real peak
    }
}

TEST_CASE("Processing does not allocate", "[unit][core][rt]")
{
    DacCore core = prepared({}, 128);
    const auto input = randomPcm(1000, 30);
    std::vector<double> l(input.size() * os), r(input.size() * os);
    const std::vector<ControlEvent> events{ControlEvent::softMute(10, true),
                                           ControlEvent::deemphasis(20, DeemphasisSelect::fs48000),
                                           ControlEvent::powerDown(500, false),
                                           ControlEvent::powerDown(600, true)};
    ak4522::test::AllocationCounter counter;
    const ProcessResult result = core.process(input, events, {l, r});
    core.reset();
    static_cast<void>(core.diagnostics());
    static_cast<void>(core.latency());
    CHECK(counter.allocations() == 0);
    CHECK(result.status == Status::ok);
}
