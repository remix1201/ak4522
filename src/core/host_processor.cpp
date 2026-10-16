#include "ak4522/host_processor.hpp"

#include "ak4522/dac_core.hpp"
#include "ak4522/pcm.hpp"
#include "core/config_validation.hpp"
#include "core/control_timeline.hpp"
#include "core/dither.hpp"
#include "core/host_alignment.hpp"
#include "core/wide_math.hpp"
#include "dsp/resampler.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numeric>
#include <vector>

namespace ak4522 {

namespace {

constexpr std::size_t os = DacCore::oversampling;

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
    AdcCore adc;
    bool adcIn = false;  // path adc: host audio is the ADC's analog input
    bool adcOut = false; // paths adc and loopback: the ADC's codes go to the host
    std::size_t outOs = os; // output converter input samples per frame
    detail::Resampler inputSrc;
    detail::Resampler outputSrc;
    bool inputBypass = false;
    std::int64_t hostPerDacNum = 1; // host frames per DAC frame, P1 / Q1
    std::int64_t hostPerDacDen = 1;

    std::size_t prefill = 0;        // D: zeros queued at reset
    std::int64_t latencyWhole = 0;  // K: whole host frames of signal delay
    double outputOffset = 0.0;      // phi, in output-converter input samples
    double coreFrames = 14.7;       // chip delay of the path in frames (P-111)

    // Scratch, sized at prepare.
    std::vector<double> inL, inR;
    std::vector<double> dacL, dacR;
    std::vector<Pcm20Frame> pcm;
    std::vector<double> hiL, hiR;
    std::vector<double> outL, outR;
    std::vector<Pcm20Frame> adcFrames;
    std::vector<double> adcL, adcR;     // the ADC's codes, normalized
    std::vector<double> stageL, stageR; // path adc: 8x samples awaiting a whole frame
    std::size_t staged = 0;
    std::vector<ControlEvent> chunkEvents;
    detail::StereoFifo fifo;
    ExampleOutputCircuit circuit;

    std::vector<Pending> pending; // ring of capacity maxPendingEvents
    std::size_t pendingRead = 0;
    std::size_t pendingCount = 0;

    std::int64_t hostConsumed = 0;
    std::int64_t dacProduced = 0;
    QuantizeCounters quantize{};
    detail::TpdfDither dither;

    std::atomic<std::uint64_t> nonFinite{0};
    std::atomic<std::uint64_t> clipped{0};
    std::atomic<std::uint64_t> chunked{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> underruns{0};

    void seedDither() noexcept
    {
        dither.seed(config.ditherSeed);
    }

    void resetStreams() noexcept
    {
        core.reset();
        adc.reset();
        staged = 0;
        inputSrc.reset();
        outputSrc.reset();
        fifo.clear();
        circuit.reset();
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

    Pcm20 quantizeSample(double x, std::uint64_t& state) noexcept
    {
        const bool sixteen = config.inputResolution == InputResolution::bits16;
        if (config.inputDither && std::isfinite(x)) {
            // TPDF of +/-1 LSB at the target resolution (P-12).
            x += detail::TpdfDither::next(state, sixteen ? 0x1.0p-15 : 0x1.0p-19);
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

        // k frames at the DAC (and ADC) rate this chunk.
        std::size_t k = n;
        if (adcIn) {
            // Path adc: host audio to 8x the rate, whole frames only.
            const std::size_t s = inputSrc.process(std::span(inL).first(n), std::span(inR).first(n),
                                                   std::span(stageL).subspan(staged), std::span(stageR).subspan(staged));
            staged += s;
            k = staged / os;
        } else {
            double* dl = inL.data();
            double* dr = inR.data();
            if (!inputBypass) {
                k = inputSrc.process(std::span(inL).first(n), std::span(inR).first(n), dacL, dacR);
                dl = dacL.data();
                dr = dacR.data();
            }
            for (std::size_t j = 0; j < k; ++j) {
                pcm[j] = {quantizeSample(dl[j], dither.left), quantizeSample(dr[j], dither.right)};
            }
        }

        // Events whose frame falls in this chunk; the converters share the timeline.
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
        const auto ev = std::span<const ControlEvent>(chunkEvents).first(events);
        std::span<const double> toOutL;
        std::span<const double> toOutR;
        if (adcIn) {
            static_cast<void>(adc.process({std::span(stageL).first(k * os), std::span(stageR).first(k * os)}, ev,
                                          std::span(adcFrames).first(k)));
            // Keep the incomplete frame's samples for the next chunk.
            std::copy(stageL.begin() + static_cast<std::ptrdiff_t>(k * os),
                      stageL.begin() + static_cast<std::ptrdiff_t>(staged), stageL.begin());
            std::copy(stageR.begin() + static_cast<std::ptrdiff_t>(k * os),
                      stageR.begin() + static_cast<std::ptrdiff_t>(staged), stageR.begin());
            staged -= k * os;
        } else {
            static_cast<void>(core.process(std::span(pcm).first(k), ev,
                                           {std::span(hiL).first(k * os), std::span(hiR).first(k * os)}));
            if (adcOut) {
                // Path loopback: the DAC's analog output is the ADC's input (REQ-ADC-06).
                static_cast<void>(adc.process({std::span(hiL).first(k * os), std::span(hiR).first(k * os)}, ev,
                                              std::span(adcFrames).first(k)));
            }
        }
        if (adcOut) {
            for (std::size_t j = 0; j < k; ++j) {
                adcL[j] = toNormalized(adcFrames[j].left);
                adcR[j] = toNormalized(adcFrames[j].right);
            }
            toOutL = std::span(adcL).first(k);
            toOutR = std::span(adcR).first(k);
        } else {
            toOutL = std::span(hiL).first(k * os);
            toOutR = std::span(hiR).first(k * os);
        }
        const std::size_t m = outputSrc.process(toOutL, toOutR, outL, outR);
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
            if (config.exampleOutputCircuit) {
                circuit.processFrame(l, r);
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

    h.adcIn = config.path == HostPath::adc;
    h.adcOut = config.path != HostPath::dac;
    h.outOs = h.adcOut ? 1 : os;

    // Input side: bypassed when the rates are equal (REQ-RATE-01). Path adc
    // converts to 8x the rate: the host audio is the analog input (P-111).
    h.inputBypass = !h.adcIn && host == dac;
    std::size_t dacCapacity = config.maxBlockFrames;
    std::size_t stageCapacity = 0;
    if (h.inputBypass) {
        h.hostPerDacNum = 1;
        h.hostPerDacDen = 1;
    } else {
        const double target = h.adcIn ? dac * static_cast<double>(os) : dac;
        if (const Status s = h.inputSrc.prepare({host, target}); s != Status::ok) {
            return s;
        }
        // Host frames per input-converter output: P / Q; per frame, os P / Q on path adc.
        const std::int64_t num = h.inputSrc.ratioNumerator() * (h.adcIn ? static_cast<std::int64_t>(os) : 1);
        const std::int64_t den = h.inputSrc.ratioDenominator();
        const std::int64_t g = std::gcd(num, den);
        h.hostPerDacNum = num / g;
        h.hostPerDacDen = den / g;
        const std::size_t outputs = h.inputSrc.maxOutputsFor(config.maxBlockFrames);
        if (h.adcIn) {
            stageCapacity = outputs + os;
            dacCapacity = stageCapacity / os + 1;
        } else {
            dacCapacity = outputs;
        }
    }

    if (const Status s = h.core.prepare(config.dac, dacCapacity); s != Status::ok) {
        return s;
    }
    if (h.adcOut) {
        AdcConfig ac;
        ac.adcRate = config.dac.dacRate;
        ac.allowExtendedRate = config.dac.allowExtendedRate;
        ac.cmode = config.dac.cmode;
        ac.masterClockHz = config.dac.masterClockHz;
        ac.profile = config.dac.profile;
        ac.analog = config.dac.analog;
        ac.analog.dcOffset = false; // the DAC's offset may be on; the ADC takes none (P-110)
        ac.inputMode = config.adcInputMode;
        if (const Status s = h.adc.prepare(ac, dacCapacity); s != Status::ok) {
            return s;
        }
    }
    if (const Status s = h.circuit.prepare(host, config.exampleCircuit); s != Status::ok) {
        return s;
    }

    // Latency alignment (P-39): the core delays by 14.7 DAC frames, which is
    // 14.7 * P1 / Q1 host frames. K is its whole part; the output converter
    // starts phi 8x samples late so the remaining fraction cancels exactly.
    // Core delay: DAC 14.7 frames, ADC 29.3125 = 469/16, both 3521/80 (P-111).
    const std::uint64_t delayNum = config.path == HostPath::dac ? detail::coreDelayNum
        : config.path == HostPath::adc                         ? 469
                                                                : 3521;
    const std::uint64_t delayDen = config.path == HostPath::dac ? detail::coreDelayDen
        : config.path == HostPath::adc                         ? 16
                                                                : 80;
    h.coreFrames = static_cast<double>(delayNum) / static_cast<double>(delayDen);
    const detail::OutputAlignment align = detail::alignOutput(
        static_cast<std::uint64_t>(h.hostPerDacNum), static_cast<std::uint64_t>(h.hostPerDacDen), h.outOs,
        delayNum, delayDen);
    h.latencyWhole = align.latencyWhole;
    h.outputOffset = align.outputOffset;
    const double hostPerDac = static_cast<double>(h.hostPerDacNum) / static_cast<double>(h.hostPerDacDen);

    // Output side: 8x DAC rate (or the ADC's 1x codes) to host rate, never
    // bypassed so the alignment offset always applies.
    detail::ResamplerSpec out{dac * static_cast<double>(h.outOs), host};
    out.startOffset = h.outputOffset;
    if (const Status s = h.outputSrc.prepare(out); s != Status::ok) {
        return s;
    }

    const double w1 = h.inputBypass ? 0.0 : static_cast<double>(h.inputSrc.halfWidth());
    const double w2 = static_cast<double>(h.outputSrc.halfWidth());
    h.prefill = detail::outputPrefill(w1, w2, h.outputOffset, hostPerDac, h.outOs);

    const std::size_t maxBlock = config.maxBlockFrames;
    h.inL.assign(maxBlock, 0.0);
    h.inR.assign(maxBlock, 0.0);
    h.dacL.assign(dacCapacity, 0.0);
    h.dacR.assign(dacCapacity, 0.0);
    h.pcm.assign(dacCapacity, Pcm20Frame{});
    h.hiL.assign(dacCapacity * os, 0.0);
    h.hiR.assign(dacCapacity * os, 0.0);
    h.adcFrames.assign(h.adcOut ? dacCapacity : 0, Pcm20Frame{});
    h.adcL.assign(h.adcOut ? dacCapacity : 0, 0.0);
    h.adcR.assign(h.adcOut ? dacCapacity : 0, 0.0);
    h.stageL.assign(stageCapacity, 0.0);
    h.stageR.assign(stageCapacity, 0.0);
    const std::size_t outCapacity = h.outputSrc.maxOutputsFor(dacCapacity * h.outOs);
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
    info.coreDacFrames = h.coreFrames; // DAC 14.7; loopback 44.0125; ADC 29.3125 (P-111)
    info.alignmentHostFrames = -h.outputOffset * hostPerDac / static_cast<double>(h.outOs);
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
    if (h.adcOut) {
        const Diagnostics a = h.adc.diagnostics();
        d.adcClippedSamples = a.clippedInputSamples;
        d.rejectedBlocks += a.rejectedBlocks;
    }
    return d;
}

} // namespace ak4522
