// Uses the installed C++ API: a HostProcessor and a pull-mode DacStream.

#include <ak4522/ak4522.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

void silence(void*, ak4522::DacPullBlock& block) noexcept
{
    for (std::size_t j = 0; j < block.frames; ++j) {
        block.integers.left[j] = block.integers.right[j] = 0;
    }
}

} // namespace

int main()
{
    ak4522::HostProcessor host;
    if (host.prepare(ak4522::HostConfig{}) != ak4522::Status::ok) {
        return 1;
    }
    std::vector<float> in(256, 0.25f), out(256);
    if (host.process({in, in}, {out, out}, {}) != ak4522::Status::ok) {
        return 1;
    }

    ak4522::DacStreamConfig config;
    config.pull = &silence;
    ak4522::DacStream stream;
    if (stream.prepare(config) != ak4522::Status::ok || stream.render(ak4522::StereoView<float>{out, out}) != ak4522::Status::ok) {
        return 1;
    }
    std::printf("library %s, latency %u frames\n", ak4522::versionString().data(), host.latency().reportedHostFrames);
    std::printf("consumer ok\n");
    return 0;
}
