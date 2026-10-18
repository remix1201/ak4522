// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// Captures AdcCore output (left channel, normalized) as raw little-endian
// doubles for the Python measurement tools; optionally through the DAC first.
//
//     ak4522_adc_capture <out.f64> [options]
//       --rate HZ --seconds S          ADC rate and length (default 44100, 10)
//       --tone HZ --level DBFS         sine input (default: zero input)
//       --noise                        NominalAnalog with noise only
//       --nominal                      NominalAnalog with every ADC option
//       --single-ended                 single-ended input mode
//       --loopback                     the tone as 20-bit PCM into a NominalAnalog
//                                      DacCore, whose 8x output feeds the ADC

#include "ak4522/adc_core.hpp"
#include "ak4522/dac_core.hpp"
#include "ak4522/pcm.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out.f64> [options]\n", argv[0]);
        return 2;
    }
    ak4522::AdcConfig adc;
    double seconds = 10.0;
    double tone = 0.0;
    double level = -1000.0;
    bool loopback = false;
    for (int i = 2; i < argc; ++i) {
        auto value = [&] { return i + 1 < argc ? std::atof(argv[++i]) : 0.0; };
        if (!std::strcmp(argv[i], "--rate")) adc.adcRate = value();
        else if (!std::strcmp(argv[i], "--seconds")) seconds = value();
        else if (!std::strcmp(argv[i], "--tone")) tone = value();
        else if (!std::strcmp(argv[i], "--level")) level = value();
        else if (!std::strcmp(argv[i], "--noise")) {
            adc.profile = ak4522::ModelProfile::nominalAnalog;
            adc.analog.noise = true;
        }
        else if (!std::strcmp(argv[i], "--nominal")) {
            adc.profile = ak4522::ModelProfile::nominalAnalog;
            adc.analog = ak4522::AnalogOptions::nominal();
        }
        else if (!std::strcmp(argv[i], "--single-ended")) adc.inputMode = ak4522::AdcInputMode::singleEnded;
        else if (!std::strcmp(argv[i], "--loopback")) loopback = true;
        else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    constexpr std::size_t os = ak4522::AdcCore::oversampling;
    constexpr std::size_t block = 1024;
    ak4522::AdcCore core;
    if (core.prepare(adc, block) != ak4522::Status::ok) {
        std::fprintf(stderr, "ADC prepare failed\n");
        return 1;
    }
    ak4522::DacCore dac;
    if (loopback) {
        ak4522::DacConfig d;
        d.dacRate = adc.adcRate;
        d.profile = adc.profile;
        d.analog = adc.analog; // the same seed: the ADC salts its own streams (P-110)
        if (dac.prepare(d, block) != ak4522::Status::ok) {
            std::fprintf(stderr, "DAC prepare failed\n");
            return 1;
        }
    }
    // Skip initialization and HPF settling so the capture is steady state.
    const auto skip = static_cast<std::size_t>(adc.adcRate);
    const auto frames = static_cast<std::size_t>(seconds * adc.adcRate);
    const double amplitude = tone > 0.0 ? std::pow(10.0, level / 20.0) : 0.0;
    std::vector<double> inL(block * os), inR(block * os);
    std::vector<ak4522::Pcm20Frame> pcm(block), out(block);
    ak4522::QuantizeCounters counters;
    std::FILE* file = std::fopen(argv[1], "wb");
    if (!file) {
        return 1;
    }
    std::size_t produced = 0;
    std::size_t position = 0;
    while (produced < skip + frames) {
        if (loopback) {
            for (std::size_t j = 0; j < block; ++j) {
                const double x = amplitude * std::sin(2.0 * 3.14159265358979323846 * tone
                                                      * static_cast<double>(position + j) / adc.adcRate);
                pcm[j] = {ak4522::quantizeToPcm20(x, counters), ak4522::quantizeToPcm20(x, counters)};
            }
            if (dac.process(pcm, {}, {inL, inR}).status != ak4522::Status::ok) {
                return 1;
            }
        } else {
            for (std::size_t n = 0; n < block * os; ++n) {
                inL[n] = inR[n] = amplitude * std::sin(2.0 * 3.14159265358979323846 * tone
                                                       * static_cast<double>(position * os + n)
                                                       / (adc.adcRate * os));
            }
        }
        position += block;
        if (core.process({inL, inR}, {}, out).status != ak4522::Status::ok) {
            return 1;
        }
        for (const auto& f : out) {
            if (produced >= skip && produced < skip + frames) {
                const double v = ak4522::toNormalized(f.left);
                std::fwrite(&v, sizeof v, 1, file);
            }
            ++produced;
        }
    }
    std::fclose(file);
    return 0;
}
