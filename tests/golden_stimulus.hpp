// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

// The fixed stimulus and configuration behind the golden Specification
// profile outputs in tests/data. Shared by the generator
// (tools/cpp/golden_dump.cpp) and the regression test.

#include "ak4522/dac_core.hpp"
#include "ak4522/host_processor.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace ak4522::golden {

// Deterministic PCM: two tones plus a pseudo-random component from a fixed
// linear congruential sequence, independent of library code.
inline std::vector<Pcm20Frame> pcm(std::size_t frames)
{
    std::vector<Pcm20Frame> x(frames);
    std::uint64_t state = 12345;
    for (std::size_t n = 0; n < frames; ++n) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double noise = static_cast<double>(static_cast<std::int64_t>(state >> 44) - (1 << 19)) / (1 << 19);
        const double t = static_cast<double>(n);
        const double l = 0.4 * std::sin(0.0713 * t) + 0.2 * std::sin(0.91 * t) + 0.1 * noise;
        const double r = 0.4 * std::cos(0.0517 * t) + 0.1 * noise;
        x[n] = {Pcm20{static_cast<std::int32_t>(std::lround(l * 524287.0))},
                Pcm20{static_cast<std::int32_t>(std::lround(r * 524287.0))}};
    }
    return x;
}

inline std::vector<ControlEvent> coreEvents()
{
    return {ControlEvent::deemphasis(300, DeemphasisSelect::fs44100), ControlEvent::softMute(700, true),
            ControlEvent::softMute(1200, false), ControlEvent::powerDown(1600, false),
            ControlEvent::powerDown(1650, true)};
}

// DacCore, default configuration: 2000 DAC frames in, left then right out.
inline std::vector<double> runCore()
{
    DacCore core;
    if (core.prepare(DacConfig{}, 256) != Status::ok) {
        return {};
    }
    const auto in = pcm(2000);
    std::vector<double> l(in.size() * DacCore::oversampling), r(l.size());
    if (core.process(in, coreEvents(), {l, r}).status != Status::ok) {
        return {};
    }
    l.insert(l.end(), r.begin(), r.end());
    return l;
}

// HostProcessor at 48 kHz host, 44.1 kHz DAC: 3000 host frames.
inline std::vector<double> runHost()
{
    HostConfig config;
    config.hostRate = 48000.0;
    config.dac.dacRate = 44100.0;
    HostProcessor host;
    if (host.prepare(config) != Status::ok) {
        return {};
    }
    const auto pcmIn = pcm(3000);
    std::vector<double> inL(pcmIn.size()), inR(pcmIn.size());
    for (std::size_t n = 0; n < pcmIn.size(); ++n) {
        inL[n] = toNormalized(pcmIn[n].left);
        inR[n] = toNormalized(pcmIn[n].right);
    }
    std::vector<double> outL(inL.size()), outR(inR.size());
    const std::vector<ControlEvent> events{ControlEvent::softMute(1000, true), ControlEvent::softMute(1500, false)};
    if (host.process(StereoView<const double>{inL, inR}, StereoView<double>{outL, outR}, events) != Status::ok) {
        return {};
    }
    outL.insert(outL.end(), outR.begin(), outR.end());
    return outL;
}

} // namespace ak4522::golden
