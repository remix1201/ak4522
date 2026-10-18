// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// The ADC model alone: a normalized analog signal at 8x the ADC rate in, one
// 20-bit frame per LRCK cycle out. The input is a 1 kHz sine at 1.0 V peak
// riding on a 0.3 V DC offset. Shows the 516 zero frames after startup, the
// high-pass filter removing the offset, and a PD pulse restarting the
// initialization.
//
//     ak4522_adc_core_example

#include <ak4522/ak4522.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    ak4522::AdcConfig config; // 44.1 kHz, Specification profile, differential input
    ak4522::AdcCore adc;
    const std::size_t block = 441; // ADC frames per call: 10 ms
    if (const auto status = adc.prepare(config, block); status != ak4522::Status::ok) {
        std::fprintf(stderr, "prepare failed: %s\n", ak4522::toString(status).data());
        return 1;
    }
    std::printf("ADC rate %.0f Hz, input rate %.0f Hz, delay %.4f frames\n", config.adcRate, adc.inputRate(),
                adc.latency().coreAdcFrames);

    // Input is in units of 0.3 x VREFH (1.5 V at VREFH = 5 V), so convert
    // from volts. Five seconds lets the 0.9 Hz high-pass filter settle.
    const std::size_t seconds = 5;
    const std::size_t frames = seconds * 44100;
    std::vector<ak4522::Pcm20Frame> codes(frames);
    std::vector<double> inL(block * ak4522::AdcCore::oversampling), inR(inL.size());
    for (std::size_t at = 0; at < frames; at += block) {
        for (std::size_t i = 0; i < inL.size(); ++i) {
            const double t = static_cast<double>(at * ak4522::AdcCore::oversampling + i) / adc.inputRate();
            const double volts = 0.3 + 1.0 * std::sin(2.0 * 3.14159265358979 * 1000.0 * t);
            inL[i] = inR[i] = ak4522::adcInputFromVolts(volts);
        }
        // Pull PD low for one frame at 3 s and release it on the next.
        std::vector<ak4522::ControlEvent> events;
        if (at == 3 * 44100) {
            events = {ak4522::ControlEvent::powerDown(0, false), ak4522::ControlEvent::powerDown(1, true)};
        }
        const auto result = adc.process({inL, inR}, events, std::span(codes).subspan(at, block));
        if (result.status != ak4522::Status::ok) {
            return 1;
        }
    }

    auto zeroRun = [&](std::size_t from) {
        std::size_t n = from;
        while (n < frames && codes[n].left.code == 0) {
            ++n;
        }
        return n - from;
    };
    // Mean and peak code over the last second before the PD pulse.
    double mean = 0.0;
    std::int32_t peak = 0;
    for (std::size_t n = 2 * 44100; n < 3 * 44100; ++n) {
        mean += codes[n].left.code;
        peak = std::max(peak, std::abs(codes[n].left.code));
    }
    mean /= 44100.0;
    std::printf("zero frames after startup: %zu\n", zeroRun(0));
    std::printf("zero frames after the PD pulse: %zu (1 frame low, then 516)\n",
                zeroRun(3 * 44100));
    std::printf("peak code %d of %d (1.0 V of 1.5 V full scale)\n", static_cast<int>(peak),
                static_cast<int>(ak4522::Pcm20::maxCode));
    std::printf("mean code %.1f: the 0.3 V offset is removed\n", mean);
    return zeroRun(0) == 516 && zeroRun(3 * 44100) == 517 && std::abs(mean) < 100.0 ? 0 : 1;
}
