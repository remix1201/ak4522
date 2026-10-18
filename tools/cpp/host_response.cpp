// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// Measures the HostProcessor (wrapper) response at given frequencies with
// least-squares sine fits and prints CSV: frequency, gain in dB relative to
// 1 kHz, and phase delay in host frames. The reported latency is the first
// line.
//
//     ak4522_host_response <host-rate> <dac-rate> <freq> [freq ...]

#include "ak4522/host_processor.hpp"
#include "support/sine_fit.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;

ak4522::test::SineFit measure(ak4522::HostProcessor& p, double host, double freq, std::size_t latency)
{
    p.reset();
    const std::size_t frames = static_cast<std::size_t>(host / 2);
    std::vector<double> in(frames), out(frames), right(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        in[n] = 0.5 * std::sin(2.0 * pi * freq * static_cast<double>(n) / host);
    }
    for (std::size_t at = 0; at < frames; at += 512) {
        const std::size_t n = std::min<std::size_t>(512, frames - at);
        static_cast<void>(p.process(
            ak4522::StereoView<const double>{std::span(in).subspan(at, n), std::span(in).subspan(at, n)},
            ak4522::StereoView<double>{std::span(out).subspan(at, n), std::span(right).subspan(at, n)}, {}));
    }
    const std::size_t skip = latency + 4000;
    return ak4522::test::fitSine(std::span(out).subspan(skip), freq, host, static_cast<double>(skip) / host);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <host-rate> <dac-rate> <freq> [freq ...]\n", argv[0]);
        return 2;
    }
    ak4522::HostConfig config;
    config.hostRate = std::atof(argv[1]);
    config.dac.dacRate = std::atof(argv[2]);
    ak4522::HostProcessor p;
    if (p.prepare(config) != ak4522::Status::ok) {
        std::fprintf(stderr, "prepare failed\n");
        return 1;
    }
    const std::size_t latency = p.latency().reportedHostFrames;
    std::printf("latency_frames,%zu\n", latency);
    const auto reference = measure(p, config.hostRate, 1000.0, latency);
    for (int i = 3; i < argc; ++i) {
        const double f = std::atof(argv[i]);
        const auto fit = measure(p, config.hostRate, f, latency);
        const double period = config.hostRate / f;
        double delay = -fit.phase / (2.0 * pi) * period;
        delay += std::round((static_cast<double>(latency) - delay) / period) * period;
        std::printf("%.6f,%.6f,%.6f\n", f, 20.0 * std::log10(fit.amplitude / reference.amplitude), delay);
    }
    return 0;
}
