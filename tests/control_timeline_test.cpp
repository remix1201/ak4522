// Control timeline (REQ-CTRL-01, REQ-RESET-01, REQ-RESET-02; D-R01, D-R04,
// D-R05; P-41, P-42, P-52, P-53, P-64, P-70 to P-72).

#include "core/control_timeline.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <random>
#include <vector>

using namespace ak4522;
using detail::ControlSegment;
using detail::ControlTimeline;

namespace {

// Per-frame result of running the timeline, as DacCore will see it.
struct FrameState {
    bool powerDown;
    DeemphasisSelect deemphasis;
    double gain;          // mute gain; zero during power-down
    bool clearState;      // set on the first frame of a segment that clears state
    bool deemphasisReset; // set on the first frame of a segment with a new selection

    friend bool operator==(const FrameState&, const FrameState&) = default;
};

Status run(ControlTimeline& timeline, std::size_t frames, const std::vector<ControlEvent>& events,
           std::vector<FrameState>& out, std::vector<ControlSegment>* segments = nullptr)
{
    return timeline.process(frames, events, [&](const ControlSegment& segment) {
        if (segments != nullptr) {
            segments->push_back(segment);
        }
        for (std::size_t i = 0; i < segment.length; ++i) {
            const double gain = segment.powerDown ? 0.0 : timeline.mute().nextGain();
            out.push_back({segment.powerDown, segment.deemphasis, gain,
                           i == 0 && segment.clearState, i == 0 && segment.deemphasisChanged});
        }
    });
}

ControlTimeline startedTimeline(DeemphasisSelect dem = DeemphasisSelect::off, bool smute = false)
{
    ControlTimeline timeline;
    timeline.reset(dem, smute);
    return timeline;
}

} // namespace

TEST_CASE("A block with no events is one segment", "[unit][control]")
{
    ControlTimeline timeline = startedTimeline();
    std::vector<FrameState> frames;
    std::vector<ControlSegment> segments;
    REQUIRE(run(timeline, 64, {}, frames, &segments) == Status::ok);
    REQUIRE(segments.size() == 1);
    CHECK(segments[0].begin == 0);
    CHECK(segments[0].length == 64);
    CHECK(segments[0].clearState);        // first block after startup
    CHECK(segments[0].deemphasisChanged); // initial selection is fresh

    segments.clear();
    REQUIRE(run(timeline, 64, {}, frames, &segments) == Status::ok);
    CHECK_FALSE(segments[0].clearState);
    CHECK_FALSE(segments[0].deemphasisChanged);
}

TEST_CASE("Zero-frame blocks visit nothing", "[unit][control]")
{
    ControlTimeline timeline = startedTimeline();
    std::vector<FrameState> frames;
    std::vector<ControlSegment> segments;
    CHECK(run(timeline, 0, {}, frames, &segments) == Status::ok);
    CHECK(segments.empty());
}

TEST_CASE("Segments split exactly at event offsets", "[unit][control]")
{
    ControlTimeline timeline = startedTimeline();
    std::vector<FrameState> frames;
    std::vector<ControlSegment> segments;
    const std::vector<ControlEvent> events{
        ControlEvent::softMute(0, true),
        ControlEvent::deemphasis(10, DeemphasisSelect::fs44100),
        ControlEvent::softMute(10, false),
        ControlEvent::softMute(63, true),
    };
    REQUIRE(run(timeline, 64, events, frames, &segments) == Status::ok);
    REQUIRE(segments.size() == 3);
    CHECK((segments[0].begin == 0 && segments[0].length == 10));
    CHECK((segments[1].begin == 10 && segments[1].length == 53));
    CHECK((segments[2].begin == 63 && segments[2].length == 1));
    CHECK(segments[1].deemphasisChanged);
    CHECK(segments[1].deemphasis == DeemphasisSelect::fs44100);
    CHECK_FALSE(segments[2].deemphasisChanged);
}

TEST_CASE("Invalid event lists are rejected without side effects", "[unit][control]")
{
    const auto [events, expected] = GENERATE(table<std::vector<ControlEvent>, Status>({
        {{ControlEvent::softMute(64, true)}, Status::eventOutOfRange},
        {{ControlEvent::softMute(5, true), ControlEvent::softMute(4, false)}, Status::eventsNotSorted},
        {{ControlEvent{0, ControlType::softMute, 2}}, Status::invalidConfiguration},
        {{ControlEvent{0, ControlType::deemphasis, 4}}, Status::invalidConfiguration},
        {{ControlEvent{0, static_cast<ControlType>(7), 0}}, Status::invalidConfiguration},
        {{ControlEvent::softMute(1, true), ControlEvent::powerDown(70, false)}, Status::eventOutOfRange},
    }));
    ControlTimeline timeline = startedTimeline();
    std::vector<FrameState> frames;
    std::vector<ControlSegment> segments;
    CHECK(run(timeline, 64, events, frames, &segments) == expected);
    CHECK(segments.empty());
    CHECK(timeline.mute().position() == 1024); // the valid first event was not applied
    CHECK_FALSE(timeline.powerDown());
}

TEST_CASE("Same-frame events apply as PD, then DEM, then SMUTE in any array order", "[unit][control]")
{
    std::vector<ControlEvent> events{
        ControlEvent::powerDown(5, true),
        ControlEvent::deemphasis(5, DeemphasisSelect::fs48000),
        ControlEvent::softMute(5, true),
    };
    std::sort(events.begin(), events.end(), [](const ControlEvent& a, const ControlEvent& b) {
        return a.type < b.type;
    });

    std::vector<std::vector<FrameState>> results;
    do {
        ControlTimeline timeline = startedTimeline();
        std::vector<FrameState> frames;
        const std::vector<ControlEvent> head{ControlEvent::powerDown(0, false)};
        REQUIRE(run(timeline, 5, head, frames) == Status::ok);
        REQUIRE(run(timeline, 20, std::vector<ControlEvent>(events), frames) == Status::ok);
        results.push_back(frames);
    } while (std::next_permutation(events.begin(), events.end(),
                                   [](const ControlEvent& a, const ControlEvent& b) {
                                       return a.type < b.type;
                                   }));

    REQUIRE(results.size() == 6);
    for (const auto& result : results) {
        CHECK(result == results.front());
    }
    // PD release latched SMUTE low (unity); the SMUTE event then started the
    // ramp on the same frame; DEM applies from that frame.
    const auto& r = results.front();
    CHECK_FALSE(r[10].powerDown);
    CHECK(r[10].gain == 1023.0 / 1024.0);
    CHECK(r[10].deemphasis == DeemphasisSelect::fs48000);
}

TEST_CASE("Power-down outputs zero and clears state on both edges", "[unit][control]")
{
    ControlTimeline timeline = startedTimeline();
    std::vector<FrameState> frames;
    REQUIRE(run(timeline, 1, {}, frames) == Status::ok);
    frames.clear();

    const std::vector<ControlEvent> events{ControlEvent::powerDown(10, false),
                                           ControlEvent::powerDown(20, true)};
    REQUIRE(run(timeline, 30, events, frames) == Status::ok);
    for (std::size_t i = 0; i < 30; ++i) {
        CAPTURE(i);
        const bool inReset = i >= 10 && i < 20;
        CHECK(frames[i].powerDown == inReset);
        CHECK(frames[i].gain == (inReset ? 0.0 : 1.0));
        CHECK(frames[i].clearState == (i == 10 || i == 20));
    }
}

TEST_CASE("The DAC runs on the frame PD is released, with no 516-frame delay", "[unit][control]")
{
    // D-R05: the 516-cycle initialization belongs to the ADC only.
    ControlTimeline timeline = startedTimeline();
    std::vector<FrameState> frames;
    const std::vector<ControlEvent> events{ControlEvent::powerDown(0, false),
                                           ControlEvent::powerDown(3, true)};
    REQUIRE(run(timeline, 600, events, frames) == Status::ok);
    CHECK(frames[2].powerDown);
    CHECK_FALSE(frames[3].powerDown);
    CHECK(frames[3].gain == 1.0);
}

TEST_CASE("Pin changes during reset are latched on release without a ramp", "[unit][control]")
{
    ControlTimeline timeline = startedTimeline(DeemphasisSelect::off, false);
    std::vector<FrameState> frames;
    const std::vector<ControlEvent> events{
        ControlEvent::powerDown(0, false),
        ControlEvent::softMute(4, true),
        ControlEvent::deemphasis(6, DeemphasisSelect::fs32000),
        ControlEvent::powerDown(8, true),
    };
    REQUIRE(run(timeline, 12, events, frames) == Status::ok);
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(frames[i].deemphasis == DeemphasisSelect::off); // not applied during reset
    }
    // P-72: SMUTE high at release starts fully muted; no ramp from unity.
    CHECK(frames[8].gain == 0.0);
    CHECK(frames[8].deemphasis == DeemphasisSelect::fs32000);
    CHECK(frames[8].deemphasisReset);
    CHECK(frames[8].clearState);
}

TEST_CASE("Startup latches the configured pin levels", "[unit][control]")
{
    ControlTimeline timeline = startedTimeline(DeemphasisSelect::fs44100, true);
    std::vector<FrameState> frames;
    REQUIRE(run(timeline, 2, {}, frames) == Status::ok);
    CHECK(frames[0].gain == 0.0);
    CHECK(frames[0].deemphasis == DeemphasisSelect::fs44100);
    CHECK(frames[0].clearState);
    CHECK(frames[0].deemphasisReset);
}

TEST_CASE("Repeated pin levels are ignored and counted", "[unit][control]")
{
    ControlTimeline timeline = startedTimeline(DeemphasisSelect::off, false);
    std::vector<FrameState> frames;
    std::vector<ControlSegment> segments;
    const std::vector<ControlEvent> events{
        ControlEvent::powerDown(1, true),                  // already released
        ControlEvent::deemphasis(2, DeemphasisSelect::off), // already off
        ControlEvent::softMute(3, false),                  // already low
        ControlEvent::softMute(4, true),
        ControlEvent::softMute(5, true),                   // repeat while ramping
    };
    REQUIRE(run(timeline, 10, events, frames, &segments) == Status::ok);
    CHECK(timeline.ignoredEvents() == 4);
    // Only the first segment after startup carries a fresh selection; the
    // ignored DEM repeat at frame 2 must not mark one.
    for (const auto& segment : segments) {
        CAPTURE(segment.begin);
        CHECK(segment.deemphasisChanged == (segment.begin == 0));
    }
    // The repeat at frame 5 did not restart the ramp.
    CHECK(frames[4].gain == 1023.0 / 1024.0);
    CHECK(frames[5].gain == 1022.0 / 1024.0);
}

TEST_CASE("Output is independent of block partitioning", "[unit][control][streaming]")
{
    constexpr std::size_t total = 5000;
    // Absolute-frame events, including same-frame groups and edges.
    const std::vector<ControlEvent> absolute{
        ControlEvent::softMute(0, true),
        ControlEvent::deemphasis(100, DeemphasisSelect::fs48000),
        ControlEvent::softMute(600, false),
        ControlEvent::powerDown(900, false),
        ControlEvent::softMute(950, true),
        ControlEvent::powerDown(1000, true),
        ControlEvent::softMute(1000, false),
        ControlEvent::deemphasis(1000, DeemphasisSelect::off),
        ControlEvent::softMute(2047, true),
        ControlEvent::softMute(2048, false),
        ControlEvent::softMute(4999, true),
    };

    auto runPartitioned = [&](const std::vector<std::size_t>& sizes) {
        ControlTimeline timeline = startedTimeline();
        std::vector<FrameState> frames;
        std::size_t frame = 0;
        std::size_t index = 0;
        std::size_t block = 0;
        while (frame < total) {
            const std::size_t length = std::min(sizes[block++ % sizes.size()], total - frame);
            std::vector<ControlEvent> local;
            while (index < absolute.size() && absolute[index].offset < frame + length) {
                ControlEvent event = absolute[index++];
                event.offset -= static_cast<std::uint32_t>(frame);
                local.push_back(event);
            }
            REQUIRE(run(timeline, length, local, frames) == Status::ok);
            frame += length;
        }
        return frames;
    };

    const auto reference = runPartitioned({total});
    REQUIRE(reference.size() == total);
    CHECK(runPartitioned({1}) == reference);
    CHECK(runPartitioned({0, 3, 1000, 1, 47}) == reference);
    std::mt19937_64 rng(7);
    std::uniform_int_distribution<std::size_t> dist(0, 400);
    for (int trial = 0; trial < 5; ++trial) {
        std::vector<std::size_t> sizes(40);
        for (auto& size : sizes) {
            size = dist(rng);
        }
        sizes.push_back(1);
        CHECK(runPartitioned(sizes) == reference);
    }
}

TEST_CASE("Control processing does not allocate", "[unit][control][rt]")
{
    ControlTimeline timeline = startedTimeline();
    const std::vector<ControlEvent> events{
        ControlEvent::softMute(0, true), ControlEvent::powerDown(100, false),
        ControlEvent::powerDown(200, true), ControlEvent::deemphasis(300, DeemphasisSelect::fs32000)};
    std::size_t visited = 0;

    ak4522::test::AllocationCounter counter;
    const Status status = timeline.process(512, events, [&](const ControlSegment& segment) {
        visited += segment.length;
        if (!segment.powerDown) {
            for (std::size_t i = 0; i < segment.length; ++i) {
                static_cast<void>(timeline.mute().nextGain());
            }
        }
    });
    CHECK(counter.allocations() == 0);
    CHECK(status == Status::ok);
    CHECK(visited == 512);
}
