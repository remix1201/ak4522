// DacStream in push mode: a producer thread (standing in for an emulator that
// runs on its own thread) pushes DAC frames into the stream's bounded queue,
// and the audio thread renders host frames from it. The producer stays ahead
// using freeFrames(); the audio thread never waits. Reports underruns.
//
//     ak4522_dac_stream_push_example

#include <ak4522/ak4522.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    ak4522::DacStreamConfig config;
    config.hostRate = 48000.0;
    config.dacRate = {32000, 1};
    config.format = ak4522::DacSampleFormat::float32;
    config.mode = ak4522::DacStreamMode::push;
    config.pushCapacityFrames = 4096;
    ak4522::DacStream stream;
    if (const auto status = stream.prepare(config); status != ak4522::Status::ok) {
        std::fprintf(stderr, "prepare failed: %s\n", ak4522::toString(status).data());
        return 1;
    }

    // Producer: a 500 Hz tone in chunks of 64 frames.
    std::atomic<bool> running{true};
    std::thread producer([&] {
        std::vector<float> chunk(64);
        std::size_t position = 0;
        while (running.load()) {
            if (stream.freeFrames() < chunk.size()) {
                std::this_thread::yield();
                continue;
            }
            for (std::size_t j = 0; j < chunk.size(); ++j) {
                chunk[j] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 500.0
                                                             * static_cast<double>(position + j) / 32000.0));
            }
            position += stream.push(ak4522::StereoView<const float>{chunk, chunk}, {}).framesAccepted;
        }
    });

    // Prime the queue before the first callback, then render as a host would.
    while (stream.queuedFrames() < 2048) {
        std::this_thread::yield();
    }
    std::vector<float> left(256), right(256);
    for (int block = 0; block < 400; ++block) {
        if (stream.render(ak4522::StereoView<float>{left, right}) != ak4522::Status::ok) {
            return 1;
        }
        // A host calls back about every 5.3 ms for 256 frames at 48 kHz.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    running.store(false);
    producer.join();

    const ak4522::Diagnostics d = stream.diagnostics();
    std::printf("rendered %d host frames; underrun DAC frames: %llu\n", 400 * 256,
                static_cast<unsigned long long>(d.sourceUnderrunFrames));
    return d.sourceUnderrunFrames == 0 ? 0 : 1;
}
