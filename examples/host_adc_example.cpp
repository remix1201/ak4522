// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// HostProcessor on its two ADC paths: a 1 kHz tone at 48 kHz runs through the
// DAC and back through the ADC (path loopback), then through the ADC alone
// (path adc), both at 44.1 kHz. For each path, prints the reported latency,
// how long the output stays silent while the ADC initializes (516 ADC
// frames), and the tone's amplitude once it arrives.
//
//     ak4522_host_adc_example

#include <ak4522/ak4522.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

bool run(ak4522::HostPath path, const char* name)
{
    ak4522::HostConfig config;
    config.hostRate = 48000.0;
    config.dac.dacRate = 44100.0; // the ADC runs at the same rate as the DAC
    config.maxBlockFrames = 512;
    config.path = path;

    ak4522::HostProcessor host;
    if (const auto status = host.prepare(config); status != ak4522::Status::ok) {
        std::fprintf(stderr, "prepare failed: %s\n", ak4522::toString(status).data());
        return false;
    }
    const ak4522::LatencyInfo latency = host.latency();

    // Half a second of a 1 kHz tone at amplitude 0.5, in 512-frame blocks.
    const std::size_t frames = 24000;
    std::vector<double> left(frames), right(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / config.hostRate;
        left[n] = right[n] = 0.5 * std::sin(2.0 * 3.14159265358979 * 1000.0 * t);
    }
    for (std::size_t at = 0; at < frames; at += 512) {
        const std::size_t n = std::min<std::size_t>(512, frames - at);
        auto l = std::span(left).subspan(at, n);
        auto r = std::span(right).subspan(at, n);
        if (host.process(ak4522::StereoView<const double>{l, r}, ak4522::StereoView<double>{l, r}, {})
            != ak4522::Status::ok) {
            return false;
        }
    }

    // The ADC outputs zero for 516 frames after startup (or a PD release)
    // while its analog section initializes; the filters already run.
    std::size_t silent = 0;
    while (silent < frames && left[silent] == 0.0) {
        ++silent;
    }
    double peak = 0.0;
    for (std::size_t n = frames / 2; n < frames; ++n) {
        peak = std::max(peak, std::abs(left[n]));
    }
    std::printf("%s: latency %u host frames (%.3f ms, chip %.4f DAC frames)\n", name,
                latency.reportedHostFrames, latency.totalSeconds * 1e3, latency.coreDacFrames);
    std::printf("%s: silent for the first %zu host frames (%.2f ms)\n", name, silent,
                static_cast<double>(silent) / config.hostRate * 1e3);
    std::printf("%s: tone amplitude %.3f\n", name, peak);
    return peak > 0.45 && peak < 0.55;
}

} // namespace

int main()
{
    const bool loopback = run(ak4522::HostPath::loopback, "loopback");
    const bool adc = run(ak4522::HostPath::adc, "adc");
    std::printf("tone through both paths: %s\n", loopback && adc ? "ok" : "FAILED");
    return loopback && adc ? 0 : 1;
}
