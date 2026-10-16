#include "ak4522/host_processor.hpp"

#include "ak4522/dac_core.hpp"
#include "ak4522/pcm.hpp"
#include "core/config_validation.hpp"
#include "core/control_timeline.hpp"
#include "core/host_alignment.hpp"
#include "core/wide_math.hpp"
#include "dsp/resampler.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace ak4522 {

namespace {

using detail::coreDelayFrames;
constexpr std::size_t os = DacCore::oversampling;

// splitmix64, used to seed and to step the dither generators (P-91).
std::uint64_t splitmix(std::uint64_t& state) noexcept
{
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double uniform(std::uint64_t& state) noexcept
{
    return static_cast<double>(splitmix(state) >> 11) * 0x1.0p-53; // [0, 1)
}

struct Pending {
    std::int64_t dacFrame;
    ControlType type;
    std::uint8_t value;
};

} // namespace

struct HostProcessor::Impl {
    HostConfig config{};
    bool prepared = false;

    DacCore core;
    detail::Resampler inputSrc;
    detail::Resampler outputSrc;
    bool inputBypass = false;
    std::int64_t hostPerDacNum = 1; // host frames per DAC frame, P1 / Q1
    std::int64_t hostPerDacDen = 1;

    std::size_t prefill = 0;        // D: zeros queued at reset
    std::int64_t latencyWhole = 0;  // K: whole host frames of signal delay
    double outputOffset = 0.0;      // phi, in 8x samples

    // Scratch, sized at prepare.
    std::vector<double> inL, inR;
    std::vector<double> dacL, dacR;
    std::vector<Pcm20Frame> pcm;
    std::vector<double> hiL, hiR;
    std::vector<double> outL, outR;
    std::vector<ControlEvent> chunkEvents;
    detail::StereoFifo fifo;

    std::vector<Pending> pending; // ring of capacity maxPendingEvents
    std::size_t pendingRead = 0;
    std::size_t pendingCount = 0;

    std::int64_t hostConsumed = 0;
    std::int64_t dacProduced = 0;
    QuantizeCounters quantize{};
    std::uint64_t ditherLeft = 0;
    std::uint64_t ditherRight = 0;

    std::atomic<std::uint64_t> nonFinite{0};
    std::atomic<std::uint64_t> clipped{0};
    std::atomic<std::uint64_t> chunked{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> underruns{0};

    void seedDither() noexcept
    {
        std::uint64_t s = config.ditherSeed;
        ditherLeft = splitmix(s);
        ditherRight = splitmix(s);
    }

    void resetStreams() noexcept
    {
        core.reset();
        inputSrc.reset();
        outputSrc.reset();
        fifo.clear();
        for (std::size_t i = 0; i < prefill; ++i) {
            fifo.push(0.0, 0.0);
        }
        pendingRead = 0;
        pendingCount = 0;
        hostConsumed = 0;
        dacProduced = 0;
    }

    // First DAC frame whose input time is at or after host frame n (P-41):
    // DAC frame j represents host time j * P1 / Q1.
    [[nodiscard]] std::int64_t dacFrameFor(std::int64_t hostFrame) const noexcept
    {
        return static_cast<std::int64_t>(detail::mulDivCeil(static_cast<std::uint64_t>(hostFrame),
                                                            static_cast<std::uint64_t>(hostPerDacDen),
                                                            static_cast<std::uint64_t>(hostPerDacNum)));
    }

    void enqueue(const ControlEvent& event) noexcept
    {
        if (pendingCount == pending.size()) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const std::int64_t frame = dacFrameFor(hostConsumed + static_cast<std::int64_t>(event.offset));
        pending[(pendingRead + pendingCount) % pending.size()] = {frame, event.type, event.value};
        ++pendingCount;
    }

    Pcm20 quantizeSample(double x, std::uint64_t& dither) noexcept
    {
        const bool sixteen = config.inputResolution == InputResolution::bits16;
        if (config.inputDither && std::isfinite(x)) {
            // TPDF of +/-1 LSB at the target resolution (P-12).
            const double lsb = sixteen ? 0x1.0p-15 : 0x1.0p-19;
            x += (uniform(dither) + uniform(dither) - 1.0) * lsb;
        }
        return sixteen ? quantizeTo16Bit(x, quantize) : quantizeToPcm20(x, quantize);
    }

    // Processes one chunk of at most maxBlockFrames host frames.
    template <typename Sample>
    void chunk(std::span<const Sample> left, std::span<const Sample> right,
               std::span<Sample> outLeft, std::span<Sample> outRight) noexcept
    {
        const std::size_t n = left.size();
        for (std::size_t i = 0; i < n; ++i) {
            inL[i] = static_cast<double>(left[i]);
            inR[i] = static_cast<double>(right[i]);
        }

        std::size_t k = n;
        double* dl = inL.data();
        double* dr = inR.data();
        if (!inputBypass) {
            k = inputSrc.process(std::span(inL).first(n), std::span(inR).first(n), dacL, dacR);
            dl = dacL.data();
            dr = dacR.data();
        }
        for (std::size_t j = 0; j < k; ++j) {
            pcm[j] = {quantizeSample(dl[j], ditherLeft), quantizeSample(dr[j], ditherRight)};
        }

        // Events whose DAC frame falls in this chunk.
        std::size_t events = 0;
        while (pendingCount > 0) {
            const Pending& p = pending[pendingRead];
            if (p.dacFrame >= dacProduced + static_cast<std::int64_t>(k)) {
                break;
            }
            chunkEvents[events++] = {static_cast<std::uint32_t>(p.dacFrame - dacProduced), p.type, p.value};
            pendingRead = (pendingRead + 1) % pending.size();
            --pendingCount;
        }
        static_cast<void>(core.process(std::span(pcm).first(k), std::span(chunkEvents).first(events),
                                       {std::span(hiL).first(k * os), std::span(hiR).first(k * os)}));

        const std::size_t m = outputSrc.process(std::span(hiL).first(k * os), std::span(hiR).first(k * os),
                                                outL, outR);
        for (std::size_t i = 0; i < m; ++i) {
            fifo.push(outL[i], outR[i]);
        }
        hostConsumed += static_cast<std::int64_t>(n);
        dacProduced += static_cast<std::int64_t>(k);

        for (std::size_t i = 0; i < n; ++i) {
            double l = 0.0;
            double r = 0.0;
            if (fifo.count > 0) {
                fifo.pop(l, r);
            } else {
                underruns.fetch_add(1, std::memory_order_relaxed);
            }
            outLeft[i] = static_cast<Sample>(l);
            outRight[i] = static_cast<Sample>(r);
        }
    }

    template <typename Sample>
    Status process(StereoView<const Sample> input, StereoView<Sample> output,
                   std::span<const ControlEvent> events) noexcept
    {
        auto reject = [this](Status s) {
            rejected.fetch_add(1, std::memory_order_relaxed);
            return s;
        };
        if (!prepared) {
            return reject(Status::notPrepared);
        }
        if (!input.consistent() || !output.consistent()) {
            return reject(Status::channelSizeMismatch);
        }
        const std::size_t frames = input.frames();
        if (output.frames() < frames) {
            return reject(Status::insufficientOutputCapacity);
        }
        if (const Status s = detail::ControlTimeline::validate(frames, events); s != Status::ok) {
            return reject(s);
        }
        if (frames > config.maxBlockFrames) {
            chunked.fetch_add(1, std::memory_order_relaxed);
        }

        const QuantizeCounters before = quantize;
        std::size_t nextEvent = 0;
        for (std::size_t at = 0; at < frames;) {
            const std::size_t n = std::min(config.maxBlockFrames, frames - at);
            while (nextEvent < events.size() && events[nextEvent].offset < at + n) {
                ControlEvent e = events[nextEvent++];
                e.offset -= static_cast<std::uint32_t>(at);
                enqueue(e);
            }
            // enqueue maps offsets relative to hostConsumed, which is the
            // start of this chunk.
            chunk<Sample>(input.left.subspan(at, n), input.right.subspan(at, n),
                          output.left.subspan(at, n), output.right.subspan(at, n));
            at += n;
        }
        nonFinite.fetch_add(quantize.nonFinite - before.nonFinite, std::memory_order_relaxed);
        clipped.fetch_add(quantize.clipped - before.clipped, std::memory_order_relaxed);
        return Status::ok;
    }
};

HostProcessor::HostProcessor() : impl_(std::make_unique<Impl>()) {}
HostProcessor::~HostProcessor() = default;
HostProcessor::HostProcessor(HostProcessor&&) noexcept = default;
HostProcessor& HostProcessor::operator=(HostProcessor&&) noexcept = default;

Status HostProcessor::prepare(const HostConfig& config)
{
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    Impl& h = *impl_;
    h.prepared = false;
    if (const Status s = detail::validate(config); s != Status::ok) {
        return s;
    }
    h.config = config;
    const double host = config.hostRate;
    const double dac = config.dac.dacRate;

    // Input side: bypassed when the rates are equal (REQ-RATE-01).
    h.inputBypass = host == dac;
    std::size_t dacCapacity = config.maxBlockFrames;
    if (h.inputBypass) {
        h.hostPerDacNum = 1;
        h.hostPerDacDen = 1;
    } else {
        if (const Status s = h.inputSrc.prepare({host, dac}); s != Status::ok) {
            return s;
        }
        h.hostPerDacNum = h.inputSrc.ratioNumerator();
        h.hostPerDacDen = h.inputSrc.ratioDenominator();
        dacCapacity = h.inputSrc.maxOutputsFor(config.maxBlockFrames);
    }

    if (const Status s = h.core.prepare(config.dac, dacCapacity); s != Status::ok) {
        return s;
    }

    // Latency alignment (P-39): the core delays by 14.7 DAC frames, which is
    // 14.7 * P1 / Q1 host frames. K is its whole part; the output converter
    // starts phi 8x samples late so the remaining fraction cancels exactly.
    const detail::OutputAlignment align = detail::alignOutput(
        static_cast<std::uint64_t>(h.hostPerDacNum), static_cast<std::uint64_t>(h.hostPerDacDen), os);
    h.latencyWhole = align.latencyWhole;
    h.outputOffset = align.outputOffset;
    const double hostPerDac = static_cast<double>(h.hostPerDacNum) / static_cast<double>(h.hostPerDacDen);

    // Output side: 8x DAC rate to host rate, never bypassed so the alignment
    // offset always applies. Passband up to 20/44.1 of the host rate.
    detail::ResamplerSpec out{dac * static_cast<double>(os), host};
    out.startOffset = h.outputOffset;
    if (const Status s = h.outputSrc.prepare(out); s != Status::ok) {
        return s;
    }

    const double w1 = h.inputBypass ? 0.0 : static_cast<double>(h.inputSrc.halfWidth());
    const double w2 = static_cast<double>(h.outputSrc.halfWidth());
    h.prefill = detail::outputPrefill(w1, w2, h.outputOffset, hostPerDac, os);

    const std::size_t maxBlock = config.maxBlockFrames;
    h.inL.assign(maxBlock, 0.0);
    h.inR.assign(maxBlock, 0.0);
    h.dacL.assign(dacCapacity, 0.0);
    h.dacR.assign(dacCapacity, 0.0);
    h.pcm.assign(dacCapacity, Pcm20Frame{});
    h.hiL.assign(dacCapacity * os, 0.0);
    h.hiR.assign(dacCapacity * os, 0.0);
    const std::size_t outCapacity = h.outputSrc.maxOutputsFor(dacCapacity * os);
    h.outL.assign(outCapacity, 0.0);
    h.outR.assign(outCapacity, 0.0);
    h.fifo.allocate(h.prefill + maxBlock + 2 * outCapacity + 16);
    h.pending.assign(config.maxPendingEvents, Pending{});
    h.chunkEvents.assign(config.maxPendingEvents, ControlEvent{});

    h.nonFinite = 0;
    h.clipped = 0;
    h.chunked = 0;
    h.rejected = 0;
    h.dropped = 0;
    h.underruns = 0;
    h.quantize = {};
    h.seedDither();
    h.resetStreams();
    h.prepared = true;
    return Status::ok;
}

void HostProcessor::reset() noexcept
{
    if (impl_ && impl_->prepared) {
        impl_->resetStreams(); // the dither stream continues (P-74)
    }
}

Status HostProcessor::process(StereoView<const float> input, StereoView<float> output,
                              std::span<const ControlEvent> events) noexcept
{
    return impl_ ? impl_->process<float>(input, output, events) : Status::notPrepared;
}

Status HostProcessor::process(StereoView<const double> input, StereoView<double> output,
                              std::span<const ControlEvent> events) noexcept
{
    return impl_ ? impl_->process<double>(input, output, events) : Status::notPrepared;
}

bool HostProcessor::isPrepared() const noexcept
{
    return impl_ && impl_->prepared;
}

LatencyInfo HostProcessor::latency() const noexcept
{
    LatencyInfo info;
    if (!isPrepared()) {
        return info;
    }
    const Impl& h = *impl_;
    const double hostPerDac = static_cast<double>(h.hostPerDacNum) / static_cast<double>(h.hostPerDacDen);
    info.dacRate = h.config.dac.dacRate;
    info.hostRate = h.config.hostRate;
    info.outsideDocumentedRate = !withinDocumentedDacRange(info.dacRate);
    // Both converters are symmetric about the times their outputs represent,
    // so they add no group delay; their look-ahead is in the buffering.
    info.inputSrcHostFrames = 0.0;
    info.outputSrcHostFrames = 0.0;
    info.coreDacFrames = coreDelayFrames;
    info.alignmentHostFrames = -h.outputOffset * hostPerDac / static_cast<double>(os);
    info.bufferingHostFrames = static_cast<std::uint32_t>(h.prefill);
    info.reportedHostFrames = static_cast<std::uint32_t>(h.prefill) + static_cast<std::uint32_t>(h.latencyWhole);
    info.totalSeconds = static_cast<double>(info.reportedHostFrames) / h.config.hostRate;
    return info;
}

Diagnostics HostProcessor::diagnostics() const noexcept
{
    Diagnostics d;
    if (!impl_) {
        return d;
    }
    const Impl& h = *impl_;
    const Diagnostics core = h.core.diagnostics();
    d.nonFiniteInputSamples = h.nonFinite.load(std::memory_order_relaxed);
    d.clippedInputSamples = h.clipped.load(std::memory_order_relaxed);
    d.chunkedBlocks = h.chunked.load(std::memory_order_relaxed);
    d.rejectedBlocks = h.rejected.load(std::memory_order_relaxed) + core.rejectedBlocks;
    d.ignoredEvents = core.ignoredEvents;
    d.droppedEvents = h.dropped.load(std::memory_order_relaxed);
    d.hostUnderruns = h.underruns.load(std::memory_order_relaxed);
    return d;
}

} // namespace ak4522
