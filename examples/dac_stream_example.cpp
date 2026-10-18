// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// A source at its own DAC rate, played at a host rate with DacStream in pull
// mode. The source stands in for an instrument or an emulated system: a
// 440 Hz square wave of 16-bit samples at 112 MHz / 2304 (about 48 611 Hz),
// which needs the opt-in extended rate range. Prints the latency and the
// output level, then the level after a soft mute requested by the source.
//
//     ak4522_dac_stream_example

#include <ak4522/ak4522.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// Produces DAC frames on request. Runs on the audio thread: no allocation,
// no locks.
struct SquareWave {
    double rate;
    double freq = 440.0;
    std::size_t position = 0;
    std::size_t muteAt = 0; // DAC frame of an SMUTE event, 0 for none

    static void pull(void* context, ak4522::DacPullBlock& block) noexcept
    {
        auto& s = *static_cast<SquareWave*>(context);
        for (std::size_t j = 0; j < block.frames; ++j) {
            const double phase = std::fmod(static_cast<double>(s.position + j) * s.freq / s.rate, 1.0);
            const std::int32_t sample = phase < 0.5 ? 8000 : -8000; // 16-bit values
            block.integers.left[j] = sample;
            block.integers.right[j] = sample;
        }
        if (s.muteAt >= s.position && s.muteAt < s.position + block.frames) {
            block.events[block.eventCount++] =
                ak4522::ControlEvent::softMute(static_cast<std::uint32_t>(s.muteAt - s.position), true);
        }
        s.position += block.frames;
    }
};

double peak(const std::vector<float>& x)
{
    double p = 0.0;
    for (const float v : x) {
        p = std::max(p, std::abs(static_cast<double>(v)));
    }
    return p;
}

} // namespace

int main()
{
    ak4522::DacStreamConfig config;
    config.hostRate = 48000.0;
    config.dacRate = {112000000, 2304};
    config.format = ak4522::DacSampleFormat::int16;
    config.dac.allowExtendedRate = true; // above 48 kHz: not documented chip behavior
    SquareWave source{config.dacRate.hz()};
    source.muteAt = 24000;
    config.pull = &SquareWave::pull;
    config.pullContext = &source;

    ak4522::DacStream stream;
    if (const auto status = stream.prepare(config); status != ak4522::Status::ok) {
        std::fprintf(stderr, "prepare failed: %s\n", ak4522::toString(status).data());
        return 1;
    }
    const ak4522::LatencyInfo latency = stream.latency();
    std::printf("DAC rate %.3f Hz%s; latency %u host frames (%.3f ms)\n", latency.dacRate,
                latency.outsideDocumentedRate ? " (extended range)" : "", latency.reportedHostFrames,
                latency.totalSeconds * 1e3);

    // The host's audio callback: 512 frames at a time.
    std::vector<float> left(512), right(512);
    double before = 0.0;
    double after = 0.0;
    for (int block = 0; block < 100; ++block) {
        if (stream.render(ak4522::StereoView<float>{left, right}) != ak4522::Status::ok) {
            return 1;
        }
        if (block == 20) {
            before = peak(left);
        }
        if (block == 99) {
            after = peak(left);
        }
    }
    std::printf("before mute: peak %.3f\n", before);
    std::printf("after mute:  %s\n", after == 0.0 ? "silent" : "not silent");
    return after == 0.0 ? 0 : 1;
}
