// Writes the impulse response of a C++ DSP component, one value per line with
// full precision, so tools/analyze_response.py measures the implementation
// rather than the design.
//
//     ak4522_impulse_dump interpolator|core-linear <output-file> [dac-frames]
//
// core-linear is the interpolator, pipeline delay, and reconstruction: the
// linear chip model from input register to analog output.

#include "dsp/interpolator.hpp"
#include "dsp/reconstruction.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s interpolator|core-linear <output-file> [dac-frames]\n", argv[0]);
        return 2;
    }
    const std::string_view component = argv[1];
    const std::size_t frames = argc > 3 ? static_cast<std::size_t>(std::strtoul(argv[3], nullptr, 10)) : 64;

    std::vector<double> response;
    if (component == "interpolator") {
        using ak4522::detail::Interpolator8x;
        Interpolator8x interpolator;
        std::vector<double> input(frames, 0.0);
        input.at(0) = 1.0;
        std::vector<double> left(frames * Interpolator8x::factor);
        std::vector<double> right(left.size());
        interpolator.process(input, input, left, right);
        response = left;
    } else if (component == "core-linear") {
        using namespace ak4522::detail;
        Interpolator8x interpolator;
        PipelineDelay pipeline;
        Reconstruction reconstruction;
        std::vector<double> input(frames, 0.0);
        input.at(0) = 1.0;
        std::vector<double> left(frames * Interpolator8x::factor);
        std::vector<double> right(left.size());
        interpolator.process(input, input, left, right);
        pipeline.process(left, right);
        reconstruction.process(left, right);
        response = left;
    } else {
        std::fprintf(stderr, "unknown component: %s\n", argv[1]);
        return 2;
    }

    std::FILE* file = std::fopen(argv[2], "w");
    if (file == nullptr) {
        std::perror(argv[2]);
        return 1;
    }
    for (const double v : response) {
        std::fprintf(file, "%.17g\n", v);
    }
    return std::fclose(file) == 0 ? 0 : 1;
}
