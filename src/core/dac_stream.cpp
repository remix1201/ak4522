#include "ak4522/dac_stream.hpp"

#include "ak4522/dac_core.hpp"
#include "ak4522/pcm.hpp"
#include "core/config_validation.hpp"
#include "core/control_timeline.hpp"
#include "core/host_alignment.hpp"
#include "core/wide_math.hpp"
#include "dsp/resampler.hpp"

#include <algorithm>
#include <atomic>
#include <numeric>
#include <type_traits>
#include <vector>

namespace ak4522 {

namespace {

constexpr std::size_t os = DacCore::oversampling;

// A pushed event, timed by the producer's frame count since reset.
struct QueuedEvent {
    std::uint64_t frame;
    ControlType type;
    std::uint8_t value;
};

// Integer input to 20-bit code (REQ-PCM-01, REQ-PCM-02, I-P03). Values
// outside the format's range saturate (P-102).
Pcm20 fromInteger(std::int32_t x, DacSampleFormat format, QuantizeCounters& counters) noexcept
{
    std::int32_t lo = Pcm20::minCode;
    std::int32_t hi = Pcm20::maxCode;
    if (format == DacSampleFormat::int16) {
        lo = -32768;
        hi = 32767;
    } else if (format == DacSampleFormat::int24) {
        lo = -8388608;
        hi = 8388607;
    }
    if (x < lo || x > hi) {
        ++counters.clipped;
        x = std::clamp(x, lo, hi);
    }
    switch (format) {
    case DacSampleFormat::int16: return Pcm20{x * 16};
    case DacSampleFormat::int24: return Pcm20{x >> 4}; // arithmetic shift drops the low 4 bits
    default: return Pcm20{x};
    }
}

Pcm20Frame convert(std::int32_t l, std::int32_t r, DacSampleFormat format, QuantizeCounters& c) noexcept
{
    return {fromInteger(l, format, c), fromInteger(r, format, c)};
}

Pcm20Frame convert(float l, float r, DacSampleFormat, QuantizeCounters& c) noexcept
{
    return {quantizeToPcm20(static_cast<double>(l), c), quantizeToPcm20(static_cast<double>(r), c)};
}

Pcm20Frame convert(double l, double r, DacSampleFormat, QuantizeCounters& c) noexcept
{
    return {quantizeToPcm20(l, c), quantizeToPcm20(r, c)};
}

template <typename T>
bool formatAccepts(DacSampleFormat format) noexcept
{
    if constexpr (std::is_same_v<T, std::int32_t>) {
        return format == DacSampleFormat::int16 || format == DacSampleFormat::int20
            || format == DacSampleFormat::int24;
    } else if constexpr (std::is_same_v<T, float>) {
        return format == DacSampleFormat::float32;
    } else {
        return format == DacSampleFormat::float64;
    }
}

} // namespace

struct DacStream::Impl {
    DacStreamConfig config{};
    bool prepared = false;

    DacCore core;
    detail::Resampler outputSrc;
    std::uint64_t dacPerHostNum = 1; // DAC frames per host frame, reduced
    std::uint64_t dacPerHostDen = 1;

    std::size_t prefill = 0;
    std::int64_t latencyWhole = 0;
    double outputOffset = 0.0;

    // Scratch, sized at prepare.
    std::vector<std::int32_t> intL, intR;
    std::vector<float> floatL, floatR;
    std::vector<double> doubleL, doubleR;
    std::vector<Pcm20Frame> pcm;
    std::vector<ControlEvent> blockEvents;
    std::vector<double> hiL, hiR;
    std::vector<double> outL, outR;
    detail::StereoFifo fifo;

    // Consumer position. Atomic only so dacFramesFor() may read it elsewhere.
    std::atomic<std::uint64_t> hostRendered{0};
    std::atomic<std::uint64_t> dacConsumed{0};

    // Push queues: single producer, single consumer. Counts only increase
    // between resets; slot = count % capacity.
    std::vector<Pcm20Frame> ring;
    std::vector<QueuedEvent> eventRing;
    std::atomic<std::uint64_t> framesWritten{0};
    std::atomic<std::uint64_t> framesRead{0};
    std::atomic<std::uint64_t> eventsWritten{0};
    std::atomic<std::uint64_t> eventsRead{0};

    std::atomic<std::uint64_t> nonFinite{0};
    std::atomic<std::uint64_t> clipped{0};
    std::atomic<std::uint64_t> chunked{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> underrunFrames{0};
    std::atomic<std::uint64_t> overrunFrames{0};

    void addCounters(const QuantizeCounters& c) noexcept
    {
        if (c.nonFinite != 0) {
            nonFinite.fetch_add(c.nonFinite, std::memory_order_relaxed);
        }
        if (c.clipped != 0) {
            clipped.fetch_add(c.clipped, std::memory_order_relaxed);
        }
    }

    void resetStreams() noexcept
    {
        core.reset();
        outputSrc.reset();
        fifo.clear();
        for (std::size_t i = 0; i < prefill; ++i) {
            fifo.push(0.0, 0.0);
        }
        hostRendered.store(0, std::memory_order_relaxed);
        dacConsumed.store(0, std::memory_order_relaxed);
        framesWritten.store(0, std::memory_order_relaxed);
        framesRead.store(0, std::memory_order_relaxed);
        eventsWritten.store(0, std::memory_order_relaxed);
        eventsRead.store(0, std::memory_order_relaxed);
    }

    // DAC frames needed once `hostFrames` host frames have been rendered:
    // the first DAC frame at or after that host time (P-101).
    [[nodiscard]] std::uint64_t dacNeededBy(std::uint64_t hostFrames) const noexcept
    {
        return detail::mulDivCeil(hostFrames, dacPerHostNum, dacPerHostDen);
    }

    // Pull mode: asks the source for k frames and converts them.
    std::size_t pullFrames(std::size_t k) noexcept
    {
        DacPullBlock block;
        block.frames = k;
        switch (config.format) {
        case DacSampleFormat::float32:
            block.floats = {std::span(floatL).first(k), std::span(floatR).first(k)};
            break;
        case DacSampleFormat::float64:
            block.doubles = {std::span(doubleL).first(k), std::span(doubleR).first(k)};
            break;
        default:
            block.integers = {std::span(intL).first(k), std::span(intR).first(k)};
            break;
        }
        block.events = blockEvents;
        config.pull(config.pullContext, block);

        QuantizeCounters c;
        for (std::size_t j = 0; j < k; ++j) {
            switch (config.format) {
            case DacSampleFormat::float32: pcm[j] = convert(floatL[j], floatR[j], config.format, c); break;
            case DacSampleFormat::float64: pcm[j] = convert(doubleL[j], doubleR[j], config.format, c); break;
            default: pcm[j] = convert(intL[j], intR[j], config.format, c); break;
            }
        }
        addCounters(c);

        std::size_t events = block.eventCount;
        if (events > blockEvents.size()
            || detail::ControlTimeline::validate(k, std::span(blockEvents).first(events)) != Status::ok) {
            dropped.fetch_add(std::min(events, blockEvents.size()), std::memory_order_relaxed);
            events = 0;
        }
        return events;
    }

    // Push mode: takes up to k queued frames, zeros for the rest (P-102).
    std::size_t takeFrames(std::size_t k) noexcept
    {
        const std::uint64_t read = framesRead.load(std::memory_order_relaxed);
        const std::uint64_t available = framesWritten.load(std::memory_order_acquire) - read;
        const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(k, available));
        const std::size_t capacity = ring.size();
        for (std::size_t j = 0; j < take; ++j) {
            pcm[j] = ring[static_cast<std::size_t>((read + j) % capacity)];
        }
        for (std::size_t j = take; j < k; ++j) {
            pcm[j] = Pcm20Frame{};
        }
        if (take < k) {
            underrunFrames.fetch_add(k - take, std::memory_order_relaxed);
        }

        // Events were published before their frames, so every event for a
        // taken frame is visible.
        std::uint64_t evRead = eventsRead.load(std::memory_order_relaxed);
        const std::uint64_t evWritten = eventsWritten.load(std::memory_order_acquire);
        std::size_t events = 0;
        while (evRead < evWritten) {
            const QueuedEvent& e = eventRing[static_cast<std::size_t>(evRead % eventRing.size())];
            if (e.frame >= read + take) {
                break;
            }
            if (events < blockEvents.size()) {
                blockEvents[events++] = {static_cast<std::uint32_t>(e.frame - read), e.type, e.value};
            } else {
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
            ++evRead;
        }
        eventsRead.store(evRead, std::memory_order_release);
        framesRead.store(read + take, std::memory_order_release);
        return events;
    }

    // Renders one chunk of at most maxBlockFrames host frames.
    template <typename Sample>
    void chunk(std::span<Sample> outLeft, std::span<Sample> outRight) noexcept
    {
        const std::size_t n = outLeft.size();
        const std::uint64_t rendered = hostRendered.load(std::memory_order_relaxed);
        const std::uint64_t consumed = dacConsumed.load(std::memory_order_relaxed);
        const auto k = static_cast<std::size_t>(dacNeededBy(rendered + n) - consumed);

        const std::size_t events = config.mode == DacStreamMode::pull ? pullFrames(k) : takeFrames(k);
        static_cast<void>(core.process(std::span(pcm).first(k), std::span(blockEvents).first(events),
                                       {std::span(hiL).first(k * os), std::span(hiR).first(k * os)}));
        const std::size_t m = outputSrc.process(std::span(hiL).first(k * os), std::span(hiR).first(k * os),
                                                outL, outR);
        for (std::size_t i = 0; i < m; ++i) {
            fifo.push(outL[i], outR[i]);
        }
        for (std::size_t i = 0; i < n; ++i) {
            double l = 0.0;
            double r = 0.0;
            if (fifo.count > 0) {
                fifo.pop(l, r);
            }
            outLeft[i] = static_cast<Sample>(l);
            outRight[i] = static_cast<Sample>(r);
        }
        hostRendered.store(rendered + n, std::memory_order_relaxed);
        dacConsumed.store(consumed + k, std::memory_order_relaxed);
    }

    template <typename Sample>
    Status render(StereoView<Sample> output) noexcept
    {
        if (!prepared) {
            rejected.fetch_add(1, std::memory_order_relaxed);
            return Status::notPrepared;
        }
        if (!output.consistent()) {
            rejected.fetch_add(1, std::memory_order_relaxed);
            return Status::channelSizeMismatch;
        }
        const std::size_t frames = output.frames();
        if (frames > config.maxBlockFrames) {
            chunked.fetch_add(1, std::memory_order_relaxed);
        }
        for (std::size_t at = 0; at < frames;) {
            const std::size_t n = std::min(config.maxBlockFrames, frames - at);
            chunk<Sample>(output.left.subspan(at, n), output.right.subspan(at, n));
            at += n;
        }
        return Status::ok;
    }

    template <typename T>
    PushResult push(StereoView<const T> frames, std::span<const ControlEvent> events) noexcept
    {
        auto reject = [this](Status s) {
            rejected.fetch_add(1, std::memory_order_relaxed);
            return PushResult{s, 0};
        };
        if (!prepared) {
            return reject(Status::notPrepared);
        }
        if (config.mode != DacStreamMode::push) {
            return reject(Status::modeMismatch);
        }
        if (!formatAccepts<T>(config.format)) {
            return reject(Status::formatMismatch);
        }
        if (!frames.consistent()) {
            return reject(Status::channelSizeMismatch);
        }
        const std::size_t n = frames.frames();
        if (const Status s = detail::ControlTimeline::validate(n, events); s != Status::ok) {
            return reject(s);
        }

        const std::uint64_t write = framesWritten.load(std::memory_order_relaxed);
        const std::uint64_t space = ring.size() - (write - framesRead.load(std::memory_order_acquire));
        const auto accept = static_cast<std::size_t>(std::min<std::uint64_t>(n, space));
        if (accept < n) {
            overrunFrames.fetch_add(n - accept, std::memory_order_relaxed);
        }

        // Events first, so the consumer sees them no later than their frames.
        std::uint64_t evWrite = eventsWritten.load(std::memory_order_relaxed);
        const std::uint64_t evRead = eventsRead.load(std::memory_order_acquire);
        for (const ControlEvent& e : events) {
            if (e.offset >= accept || evWrite - evRead >= eventRing.size()) {
                dropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            eventRing[static_cast<std::size_t>(evWrite % eventRing.size())] = {write + e.offset, e.type, e.value};
            ++evWrite;
        }
        eventsWritten.store(evWrite, std::memory_order_release);

        QuantizeCounters c;
        for (std::size_t j = 0; j < accept; ++j) {
            ring[static_cast<std::size_t>((write + j) % ring.size())]
                = convert(frames.left[j], frames.right[j], config.format, c);
        }
        addCounters(c);
        framesWritten.store(write + accept, std::memory_order_release);
        return {Status::ok, accept};
    }
};

DacStream::DacStream() : impl_(std::make_unique<Impl>()) {}
DacStream::~DacStream() = default;
DacStream::DacStream(DacStream&&) noexcept = default;
DacStream& DacStream::operator=(DacStream&&) noexcept = default;

Status DacStream::prepare(const DacStreamConfig& config)
{
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    Impl& s = *impl_;
    s.prepared = false;
    if (const Status v = detail::validate(config); v != Status::ok) {
        return v;
    }
    s.config = config;
    s.config.dac.dacRate = config.dacRate.hz();
    const auto host = static_cast<std::uint64_t>(config.hostRate); // whole hertz (P-32)

    // DAC frames per host frame: (N / D) / H, reduced (P-100).
    std::uint64_t num = config.dacRate.numerator;
    std::uint64_t den = config.dacRate.denominator * host;
    const std::uint64_t g = std::gcd(num, den);
    num /= g;
    den /= g;
    s.dacPerHostNum = num;
    s.dacPerHostDen = den;

    const std::size_t dacCapacity = static_cast<std::size_t>(detail::mulDivCeil(config.maxBlockFrames, num, den)) + 1;
    if (const Status v = s.core.prepare(s.config.dac, dacCapacity); v != Status::ok) {
        return v;
    }

    const detail::OutputAlignment align = detail::alignOutput(den, num, os);
    s.latencyWhole = align.latencyWhole;
    s.outputOffset = align.outputOffset;
    const double hostPerDac = static_cast<double>(den) / static_cast<double>(num);

    detail::ResamplerSpec out{s.config.dac.dacRate * static_cast<double>(os), config.hostRate};
    out.startOffset = s.outputOffset;
    out.ratioNumerator = static_cast<std::int64_t>(num * os);
    out.ratioDenominator = static_cast<std::int64_t>(den);
    if (const Status v = s.outputSrc.prepare(out); v != Status::ok) {
        return v;
    }
    s.prefill = detail::outputPrefill(0.0, static_cast<double>(s.outputSrc.halfWidth()), s.outputOffset,
                                      hostPerDac, os);

    const std::size_t maxBlock = config.maxBlockFrames;
    const bool pull = config.mode == DacStreamMode::pull;
    const bool isFloat = config.format == DacSampleFormat::float32;
    const bool isDouble = config.format == DacSampleFormat::float64;
    const bool isInt = !isFloat && !isDouble;
    s.intL.assign(pull && isInt ? dacCapacity : 0, 0);
    s.intR.assign(pull && isInt ? dacCapacity : 0, 0);
    s.floatL.assign(pull && isFloat ? dacCapacity : 0, 0.0f);
    s.floatR.assign(pull && isFloat ? dacCapacity : 0, 0.0f);
    s.doubleL.assign(pull && isDouble ? dacCapacity : 0, 0.0);
    s.doubleR.assign(pull && isDouble ? dacCapacity : 0, 0.0);
    s.pcm.assign(dacCapacity, Pcm20Frame{});
    s.blockEvents.assign(pull ? config.maxEventsPerBlock : config.pushCapacityEvents, ControlEvent{});
    s.hiL.assign(dacCapacity * os, 0.0);
    s.hiR.assign(dacCapacity * os, 0.0);
    const std::size_t outCapacity = s.outputSrc.maxOutputsFor(dacCapacity * os);
    s.outL.assign(outCapacity, 0.0);
    s.outR.assign(outCapacity, 0.0);
    s.fifo.allocate(s.prefill + maxBlock + 2 * outCapacity + 16);
    s.ring.assign(pull ? 0 : config.pushCapacityFrames, Pcm20Frame{});
    s.eventRing.assign(pull ? 0 : config.pushCapacityEvents, QueuedEvent{});

    s.nonFinite = 0;
    s.clipped = 0;
    s.chunked = 0;
    s.rejected = 0;
    s.dropped = 0;
    s.underrunFrames = 0;
    s.overrunFrames = 0;
    s.resetStreams();
    s.prepared = true;
    return Status::ok;
}

void DacStream::reset() noexcept
{
    if (impl_ && impl_->prepared) {
        impl_->resetStreams();
    }
}

Status DacStream::render(StereoView<float> output) noexcept
{
    return impl_ ? impl_->render<float>(output) : Status::notPrepared;
}

Status DacStream::render(StereoView<double> output) noexcept
{
    return impl_ ? impl_->render<double>(output) : Status::notPrepared;
}

PushResult DacStream::push(StereoView<const std::int32_t> frames, std::span<const ControlEvent> events) noexcept
{
    return impl_ ? impl_->push<std::int32_t>(frames, events) : PushResult{Status::notPrepared, 0};
}

PushResult DacStream::push(StereoView<const float> frames, std::span<const ControlEvent> events) noexcept
{
    return impl_ ? impl_->push<float>(frames, events) : PushResult{Status::notPrepared, 0};
}

PushResult DacStream::push(StereoView<const double> frames, std::span<const ControlEvent> events) noexcept
{
    return impl_ ? impl_->push<double>(frames, events) : PushResult{Status::notPrepared, 0};
}

std::size_t DacStream::queuedFrames() const noexcept
{
    if (!isPrepared() || impl_->config.mode != DacStreamMode::push) {
        return 0;
    }
    const std::uint64_t read = impl_->framesRead.load(std::memory_order_acquire);
    return static_cast<std::size_t>(impl_->framesWritten.load(std::memory_order_acquire) - read);
}

std::size_t DacStream::freeFrames() const noexcept
{
    if (!isPrepared() || impl_->config.mode != DacStreamMode::push) {
        return 0;
    }
    return impl_->ring.size() - queuedFrames();
}

std::size_t DacStream::dacFramesFor(std::size_t hostFrames) const noexcept
{
    if (!isPrepared()) {
        return 0;
    }
    const Impl& s = *impl_;
    const std::uint64_t rendered = s.hostRendered.load(std::memory_order_relaxed);
    const std::uint64_t consumed = s.dacConsumed.load(std::memory_order_relaxed);
    return static_cast<std::size_t>(s.dacNeededBy(rendered + hostFrames) - consumed);
}

bool DacStream::isPrepared() const noexcept
{
    return impl_ && impl_->prepared;
}

LatencyInfo DacStream::latency() const noexcept
{
    LatencyInfo info;
    if (!isPrepared()) {
        return info;
    }
    const Impl& s = *impl_;
    const double hostPerDac = static_cast<double>(s.dacPerHostDen) / static_cast<double>(s.dacPerHostNum);
    info.dacRate = s.config.dac.dacRate;
    info.hostRate = s.config.hostRate;
    info.inputSrcHostFrames = 0.0; // no input converter
    info.outputSrcHostFrames = 0.0;
    info.coreDacFrames = detail::coreDelayFrames;
    info.alignmentHostFrames = -s.outputOffset * hostPerDac / static_cast<double>(os);
    info.bufferingHostFrames = static_cast<std::uint32_t>(s.prefill);
    info.reportedHostFrames = static_cast<std::uint32_t>(s.prefill) + static_cast<std::uint32_t>(s.latencyWhole);
    info.totalSeconds = static_cast<double>(info.reportedHostFrames) / s.config.hostRate;
    return info;
}

Diagnostics DacStream::diagnostics() const noexcept
{
    Diagnostics d;
    if (!impl_) {
        return d;
    }
    const Impl& s = *impl_;
    const Diagnostics core = s.core.diagnostics();
    d.nonFiniteInputSamples = s.nonFinite.load(std::memory_order_relaxed);
    d.clippedInputSamples = s.clipped.load(std::memory_order_relaxed);
    d.chunkedBlocks = s.chunked.load(std::memory_order_relaxed);
    d.rejectedBlocks = s.rejected.load(std::memory_order_relaxed) + core.rejectedBlocks;
    d.ignoredEvents = core.ignoredEvents;
    d.droppedEvents = s.dropped.load(std::memory_order_relaxed);
    d.sourceUnderrunFrames = s.underrunFrames.load(std::memory_order_relaxed);
    d.sourceOverrunFrames = s.overrunFrames.load(std::memory_order_relaxed);
    return d;
}

} // namespace ak4522
