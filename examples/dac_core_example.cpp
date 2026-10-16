// The chip model alone: 20-bit PCM frames at the DAC rate in, the
// reconstructed signal at 8x the DAC rate out. For callers that do their own
// rate conversion. Feeds one full-scale impulse and reports where the output
// peaks, which shows the core's 14.7-frame delay.
//
//     ak4522_dac_core_example

#include <ak4522/ak4522.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    ak4522::DacConfig config; // 44.1 kHz, Specification profile
    ak4522::DacCore core;
    const std::size_t frames = 64;
    if (const auto status = core.prepare(config, frames); status != ak4522::Status::ok) {
        std::fprintf(stderr, "prepare failed: %s\n", ak4522::toString(status).data());
        return 1;
    }

    std::vector<ak4522::Pcm20Frame> input(frames);
    input[0] = {ak4522::Pcm20{ak4522::Pcm20::maxCode}, ak4522::Pcm20{ak4522::Pcm20::maxCode}};
    std::vector<double> left(frames * ak4522::DacCore::oversampling), right(left.size());
    const ak4522::ProcessResult result = core.process(input, {}, {left, right});
    if (result.status != ak4522::Status::ok) {
        return 1;
    }

    std::size_t peak = 0;
    for (std::size_t i = 1; i < left.size(); ++i) {
        if (std::abs(left[i]) > std::abs(left[peak])) {
            peak = i;
        }
    }
    const double peakFrames = static_cast<double>(peak) / ak4522::DacCore::oversampling;
    std::printf("output rate %.0f Hz; impulse peak at %.3f DAC frames (nominal delay %.1f)\n", core.outputRate(),
                peakFrames, core.latency().coreDacFrames);
    return std::abs(peakFrames - 14.7) < 1.0 ? 0 : 1;
}
