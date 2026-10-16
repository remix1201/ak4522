// CPU benchmark for REQ-PERF-01 (P-93). Times every callback of one stereo
// instance and reports the 50th and 99.9th percentile and the maximum as a
// share of the callback period, plus callbacks that took longer than the
// period. The budget: a clean HostProcessor at 48 kHz host / 44.1 kHz DAC
// stays under 10% of the period at the 99.9th percentile for blocks >= 64.
// Results are specific to the machine, compiler, and build; run a release
// build on an otherwise idle machine.
//
//     ak4522_benchmark [--check] [--seconds S]
//
// --check exits with status 1 if the budget case misses the budget.

#include <ak4522/ak4522.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;

struct Result {
    double p50;   // percent of the callback period
    double p999;
    double max;
    std::size_t overruns; // callbacks longer than the period
    std::size_t calls;
};

Result summarize(std::vector<double>& seconds, double period)
{
    std::sort(seconds.begin(), seconds.end());
    auto at = [&](double q) {
        const auto i = static_cast<std::size_t>(std::ceil(q * static_cast<double>(seconds.size()))) - 1;
        return seconds[std::min(i, seconds.size() - 1)] / period * 100.0;
    };
    Result r{at(0.5), at(0.999), seconds.back() / period * 100.0, 0, seconds.size()};
    r.overruns = static_cast<std::size_t>(
        std::count_if(seconds.begin(), seconds.end(), [&](double s) { return s > period; }));
    return r;
}

// Times `calls` invocations of `callback` after a warm-up.
template <typename Callback>
std::vector<double> time(std::size_t calls, Callback&& callback)
{
    using clock = std::chrono::steady_clock;
    for (int i = 0; i < 200; ++i) {
        callback();
    }
    std::vector<double> seconds(calls);
    for (std::size_t i = 0; i < calls; ++i) {
        const auto start = clock::now();
        callback();
        seconds[i] = std::chrono::duration<double>(clock::now() - start).count();
    }
    return seconds;
}

void print(const char* name, std::size_t block, const Result& r)
{
    std::printf("%-34s block %4zu  p50 %6.3f%%  p99.9 %6.3f%%  max %7.3f%%  over-period %zu of %zu\n", name, block,
                r.p50, r.p999, r.max, r.overruns, r.calls);
}

Result hostCase(const char* name, ak4522::HostConfig config, std::size_t block, double seconds)
{
    config.maxBlockFrames = std::max<std::size_t>(block, 512);
    ak4522::HostProcessor host;
    if (host.prepare(config) != ak4522::Status::ok) {
        std::fprintf(stderr, "%s: prepare failed\n", name);
        return {};
    }
    std::vector<float> in(block), outL(block), outR(block);
    std::size_t position = 0;
    const auto calls = static_cast<std::size_t>(seconds * config.hostRate / static_cast<double>(block));
    auto samples = time(calls, [&] {
        for (std::size_t n = 0; n < block; ++n) {
            in[n] = static_cast<float>(0.5 * std::sin(2.0 * pi * 997.0 * static_cast<double>(position + n)
                                                      / config.hostRate));
        }
        position += block;
        static_cast<void>(host.process(ak4522::StereoView<const float>{in, in},
                                       ak4522::StereoView<float>{outL, outR}, {}));
    });
    const Result r = summarize(samples, static_cast<double>(block) / config.hostRate);
    print(name, block, r);
    return r;
}

struct Tone {
    std::size_t position = 0;
    static void pull(void* context, ak4522::DacPullBlock& b) noexcept
    {
        auto& t = *static_cast<Tone*>(context);
        for (std::size_t j = 0; j < b.frames; ++j) {
            const auto x = static_cast<std::int32_t>(200000.0 * std::sin(0.1 * static_cast<double>(t.position + j)));
            b.integers.left[j] = b.integers.right[j] = x;
        }
        t.position += b.frames;
    }
};

void streamCase(std::size_t block, double seconds)
{
    ak4522::DacStreamConfig config;
    config.hostRate = 48000.0;
    config.dacRate = {112000000, 2304};
    config.dac.allowExtendedRate = true;
    config.maxBlockFrames = std::max<std::size_t>(block, 512);
    Tone tone;
    config.pull = &Tone::pull;
    config.pullContext = &tone;
    ak4522::DacStream stream;
    if (stream.prepare(config) != ak4522::Status::ok) {
        std::fprintf(stderr, "stream: prepare failed\n");
        return;
    }
    std::vector<float> outL(block), outR(block);
    const auto calls = static_cast<std::size_t>(seconds * config.hostRate / static_cast<double>(block));
    auto samples = time(calls, [&] { static_cast<void>(stream.render(ak4522::StereoView<float>{outL, outR})); });
    print("DacStream 48 kHz / 48.611 kHz", block, summarize(samples, static_cast<double>(block) / config.hostRate));
}

} // namespace

int main(int argc, char** argv)
{
    bool check = false;
    double seconds = 30.0; // of audio per case
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) {
            check = true;
        } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::atof(argv[++i]);
        }
    }
    std::printf("ak4522 %s benchmark, %.0f s of audio per case\n", ak4522::versionString().data(), seconds);

    bool pass = true;
    for (const std::size_t block : {std::size_t{64}, std::size_t{128}, std::size_t{256}, std::size_t{512}}) {
        ak4522::HostConfig clean; // 48 kHz host, 44.1 kHz DAC, Specification
        const Result r = hostCase("HostProcessor 48 / 44.1 kHz clean", clean, block, seconds);
        pass = pass && r.calls > 0 && r.p999 < 10.0;
    }
    for (const std::size_t block : {std::size_t{64}, std::size_t{512}}) {
        ak4522::HostConfig nominal;
        nominal.dac.profile = ak4522::ModelProfile::nominalAnalog;
        nominal.dac.analog = ak4522::AnalogOptions::nominal();
        hostCase("HostProcessor 48 / 44.1 kHz nominal", nominal, block, seconds);
        ak4522::HostConfig high;
        high.hostRate = 192000.0;
        high.dac.dacRate = 48000.0;
        hostCase("HostProcessor 192 / 48 kHz clean", high, block, seconds);
        streamCase(block, seconds);
    }
    std::printf("budget (clean 48 / 44.1 kHz, p99.9 < 10%% for blocks >= 64): %s\n", pass ? "met" : "MISSED");
    return check && !pass ? 1 : 0;
}
