// Captures HostProcessor output as raw little-endian doubles (left channel)
// for the Python measurement tools.
//
//     ak4522_capture <out.f64> [options]
//       --host HZ --dac HZ --seconds S     rates and length (default 44100 44100 10)
//       --tone HZ --level DBFS             sine input (default: zero input)
//       --noise                            NominalAnalog profile with noise
//       --distortion [equal|odd|even]      NominalAnalog distortion with a shape
//       --nominal                          NominalAnalog with every option except DC offset
//       --mute                             SMUTE high from the first frame
//       --bits16 --dither                  host input resolution and dither

#include "ak4522/host_processor.hpp"

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
    ak4522::HostConfig config;
    config.hostRate = 44100.0;
    config.dac.dacRate = 44100.0;
    double seconds = 10.0;
    double tone = 0.0;
    double level = -1000.0;
    bool mute = false;
    for (int i = 2; i < argc; ++i) {
        auto value = [&] { return i + 1 < argc ? std::atof(argv[++i]) : 0.0; };
        if (!std::strcmp(argv[i], "--host")) config.hostRate = value();
        else if (!std::strcmp(argv[i], "--dac")) config.dac.dacRate = value();
        else if (!std::strcmp(argv[i], "--seconds")) seconds = value();
        else if (!std::strcmp(argv[i], "--tone")) tone = value();
        else if (!std::strcmp(argv[i], "--level")) level = value();
        else if (!std::strcmp(argv[i], "--noise")) {
            config.dac.profile = ak4522::ModelProfile::nominalAnalog;
            config.dac.analog.noise = true;
        }
        else if (!std::strcmp(argv[i], "--distortion")) {
            config.dac.profile = ak4522::ModelProfile::nominalAnalog;
            config.dac.analog.distortion = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const char* shape = argv[++i];
                config.dac.analog.distortionShape = !std::strcmp(shape, "odd")
                    ? ak4522::DistortionShape::mostly3rd
                    : !std::strcmp(shape, "even") ? ak4522::DistortionShape::mostly2nd
                                                  : ak4522::DistortionShape::equal2nd3rd;
            }
        }
        else if (!std::strcmp(argv[i], "--nominal")) {
            config.dac.profile = ak4522::ModelProfile::nominalAnalog;
            config.dac.analog = ak4522::AnalogOptions::nominal();
        }
        else if (!std::strcmp(argv[i], "--mute")) mute = true;
        else if (!std::strcmp(argv[i], "--bits16")) config.inputResolution = ak4522::InputResolution::bits16;
        else if (!std::strcmp(argv[i], "--dither")) config.inputDither = true;
        else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (mute) {
        config.dac.initialSoftMute = true;
    }
    ak4522::HostProcessor host;
    if (host.prepare(config) != ak4522::Status::ok) {
        std::fprintf(stderr, "prepare failed\n");
        return 1;
    }
    // Skip the latency and settling so the capture is steady state.
    const std::size_t skip = host.latency().reportedHostFrames + 4096;
    const std::size_t frames = static_cast<std::size_t>(seconds * config.hostRate);
    const double amplitude = tone > 0.0 ? std::pow(10.0, level / 20.0) : 0.0;
    std::vector<double> in(512), outL(512), outR(512);
    std::FILE* file = std::fopen(argv[1], "wb");
    if (file == nullptr) {
        std::perror(argv[1]);
        return 1;
    }
    for (std::size_t at = 0; at < skip + frames; at += 512) {
        const std::size_t n = std::min<std::size_t>(512, skip + frames - at);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(at + i) / config.hostRate;
            in[i] = amplitude * std::sin(2.0 * 3.14159265358979323846 * tone * t);
        }
        auto inSpan = std::span(in).first(n);
        if (host.process(ak4522::StereoView<const double>{inSpan, inSpan},
                         ak4522::StereoView<double>{std::span(outL).first(n), std::span(outR).first(n)}, {})
            != ak4522::Status::ok) {
            return 1;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (at + i >= skip) {
                std::fwrite(&outL[i], sizeof(double), 1, file);
            }
        }
    }
    return std::fclose(file) == 0 ? 0 : 1;
}
