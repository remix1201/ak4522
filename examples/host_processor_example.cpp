// Minimal use of the AK4522 emulator from host code: a 1 kHz tone at 48 kHz
// passes through the emulated DAC running at 44.1 kHz, with SMUTE raised
// halfway through. Prints the reported latency and the output level before
// and after the mute.
//
//     ak4522_host_processor_example

#include <ak4522/ak4522.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    ak4522::HostConfig config;
    config.hostRate = 48000.0;
    config.dac.dacRate = 44100.0;
    config.maxBlockFrames = 512;

    ak4522::HostProcessor host;
    if (const auto status = host.prepare(config); status != ak4522::Status::ok) {
        std::fprintf(stderr, "prepare failed: %s\n", ak4522::toString(status).data());
        return 1;
    }
    const ak4522::LatencyInfo latency = host.latency();
    std::printf("latency: %u host frames (%.3f ms)\n", latency.reportedHostFrames,
                latency.totalSeconds * 1e3);

    // One second of audio processed in 512-frame blocks, as a host would.
    const std::size_t frames = 48000;
    std::vector<float> left(frames), right(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / 48000.0;
        left[n] = right[n] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 1000.0 * t));
    }
    const std::size_t muteAt = frames / 2;
    for (std::size_t at = 0; at < frames; at += 512) {
        const std::size_t n = std::min<std::size_t>(512, frames - at);
        std::vector<ak4522::ControlEvent> events;
        if (muteAt >= at && muteAt < at + n) {
            events.push_back(ak4522::ControlEvent::softMute(static_cast<std::uint32_t>(muteAt - at), true));
        }
        auto l = std::span(left).subspan(at, n);
        auto r = std::span(right).subspan(at, n);
        // In-place processing: input and output are the same buffers.
        if (host.process(ak4522::StereoView<const float>{l, r}, ak4522::StereoView<float>{l, r}, events)
            != ak4522::Status::ok) {
            return 1;
        }
    }

    auto rmsDb = [&](std::size_t from, std::size_t to) {
        double sum = 0.0;
        for (std::size_t n = from; n < to; ++n) {
            const double v = static_cast<double>(left[n]);
            sum += v * v;
        }
        return 10.0 * std::log10(sum / static_cast<double>(to - from) + 1e-300);
    };
    std::printf("level before mute: %.2f dBFS\n", rmsDb(10000, 20000));
    std::printf("level after mute:  %s\n", rmsDb(40000, 48000) < -300.0 ? "silent" : "not silent");
    return 0;
}
