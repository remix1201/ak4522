#pragma once

#include "ak4522/buffers.hpp"
#include "ak4522/config.hpp"
#include "ak4522/diagnostics.hpp"
#include "ak4522/events.hpp"
#include "ak4522/latency.hpp"
#include "ak4522/status.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace ak4522 {

// An exact sample rate in Hz, numerator / denominator (P-100). For example,
// a 112 MHz clock divided by 2304 is {112000000, 2304}. Whole-hertz rates
// use denominator 1.
struct ExactRate {
    std::uint64_t numerator = 44100;
    std::uint64_t denominator = 1;

    [[nodiscard]] constexpr double hz() const noexcept
    {
        return denominator == 0 ? 0.0 : static_cast<double>(numerator) / static_cast<double>(denominator);
    }
};

// Sample format supplied by a DacStream source (REQ-API-01).
enum class DacSampleFormat : std::uint8_t {
    int16,   // [-32768, 32767], scaled to 20-bit weight by 16 (REQ-PCM-02)
    int20,   // [-524288, 524287], used as is (REQ-PCM-01)
    int24,   // [-8388608, 8388607], top 20 bits kept, low 4 dropped (I-P03)
    float32, // normalized, quantized per REQ-PCM-03
    float64, // normalized, quantized per REQ-PCM-03
};

enum class DacStreamMode : std::uint8_t {
    pull, // render() asks a callback for the DAC frames it needs
    push, // a producer pushes DAC frames into a bounded queue that render() drains
};

// One request from render() to a pull-mode source. The source writes exactly
// `frames` DAC frames into the view matching the configured format (the other
// views are empty) and may write events into `events`, setting `eventCount`.
// Event offsets are DAC frames relative to this request, below `frames`, and
// must not decrease; a block with invalid events has all of them dropped and
// counted in Diagnostics::droppedEvents. Integer values outside the format's
// range saturate and are counted as clipped.
struct DacPullBlock {
    std::size_t frames = 0;
    StereoView<std::int32_t> integers; // int16, int20, int24
    StereoView<float> floats;          // float32
    StereoView<double> doubles;        // float64
    std::span<ControlEvent> events;    // capacity: DacStreamConfig::maxEventsPerBlock
    std::size_t eventCount = 0;
};

// Called on the audio thread from render(); must be real-time safe.
using DacPullCallback = void (*)(void* context, DacPullBlock& block) noexcept;

struct DacStreamConfig {
    double hostRate = 48000.0;  // Hz; 44.1, 48, 88.2, 96, 176.4, or 192 kHz (P-32)
    ExactRate dacRate{};        // the source's rate; replaces dac.dacRate
    DacSampleFormat format = DacSampleFormat::int20;
    DacStreamMode mode = DacStreamMode::pull;
    std::size_t maxBlockFrames = 512; // host frames per render before internal chunking

    // Pull mode: the source. The callback is required in pull mode.
    DacPullCallback pull = nullptr;
    void* pullContext = nullptr;
    std::size_t maxEventsPerBlock = 64; // event capacity of each DacPullBlock

    // Push mode: queue capacities.
    std::size_t pushCapacityFrames = 8192; // DAC frames
    std::size_t pushCapacityEvents = 256;

    DacConfig dac{}; // dac.dacRate is ignored; dacRate.hz() is used
};

struct PushResult {
    Status status = Status::ok;
    std::size_t framesAccepted = 0; // frames beyond the free space are dropped
};

// A source at its own DAC rate, such as an instrument or an emulated system,
// played at a host rate (REQ-API-01): DAC frames in, the chip model, output
// SRC, host frames out. There is no input SRC: the source already runs at the
// DAC rate. DAC frame j is presented at host time j * hostRate / dacRate and
// its output appears latency().reportedHostFrames later (P-39, P-101).
//
// Pull mode: render() calls the source for exactly the DAC frames that the
// host frames require (P-101). The source is a stream: the same sequence of
// frames gives the same output for any render block sizes.
//
// Push mode: a producer thread calls push() while the audio thread calls
// render(). The queue is single-producer, single-consumer and wait-free. When
// render() needs frames the queue lacks, it plays zeros and counts the
// missing frames; when push() finds the queue full, it drops the frames that
// do not fit and counts them (P-102). Prime the queue before rendering.
//
// Threading: prepare(), reset(), and the destructor run off the audio thread
// and never concurrently with render() or push(). render() runs on one thread
// at a time; push() runs on one (possibly different) thread at a time.
// latency() is stable between prepares; diagnostics() and the queue queries
// may be called from any thread.
class DacStream {
public:
    DacStream();
    ~DacStream();
    DacStream(DacStream&&) noexcept;
    DacStream& operator=(DacStream&&) noexcept;
    DacStream(const DacStream&) = delete;
    DacStream& operator=(const DacStream&) = delete;

    [[nodiscard]] Status prepare(const DacStreamConfig& config);

    // Clears the chip, the converter, the output alignment, and the push
    // queues. Not a PD transition (P-74).
    void reset() noexcept;

    // Writes output.frames() host frames. Any count, including zero.
    [[nodiscard]] Status render(StereoView<float> output) noexcept;
    [[nodiscard]] Status render(StereoView<double> output) noexcept;

    // Push mode, producer thread. The overload must match the configured
    // format: int32 for the integer formats, float for float32, double for
    // float64. Event offsets are DAC frames relative to `frames`.
    [[nodiscard]] PushResult push(StereoView<const std::int32_t> frames,
                                  std::span<const ControlEvent> events) noexcept;
    [[nodiscard]] PushResult push(StereoView<const float> frames,
                                  std::span<const ControlEvent> events) noexcept;
    [[nodiscard]] PushResult push(StereoView<const double> frames,
                                  std::span<const ControlEvent> events) noexcept;

    // Push mode: frames queued and not yet rendered, and free space.
    [[nodiscard]] std::size_t queuedFrames() const noexcept;
    [[nodiscard]] std::size_t freeFrames() const noexcept;

    // DAC frames that rendering `hostFrames` more host frames will consume,
    // from the current position. Lets a push producer stay ahead.
    [[nodiscard]] std::size_t dacFramesFor(std::size_t hostFrames) const noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] LatencyInfo latency() const noexcept;
    [[nodiscard]] Diagnostics diagnostics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ak4522
