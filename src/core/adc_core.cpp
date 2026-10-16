#include "ak4522/adc_core.hpp"

#include "core/config_validation.hpp"
#include "core/control_timeline.hpp"
#include "dsp/adc_highpass.hpp"
#include "dsp/decimator.hpp"

#include <algorithm>
#include <atomic>
#include <vector>

namespace ak4522 {

namespace {

constexpr std::uint32_t initializationFrames = 516; // AD-10, P-108

} // namespace

struct AdcCore::Impl {
    AdcConfig config{};
    std::size_t maxBlockFrames = 0;
    bool prepared = false;

    detail::ControlTimeline timeline;
    detail::Decimator8x decimator;
    detail::AdcHighPass highpass;
    std::uint32_t initRemaining = 0; // zero-output frames left after PD release

    std::vector<double> left; // ADC-rate scratch
    std::vector<double> right;

    QuantizeCounters quantize{};
    std::atomic<std::uint64_t> clipped{0};
    std::atomic<std::uint64_t> chunkedBlocks{0};
    std::atomic<std::uint64_t> rejectedBlocks{0};

    void resetState() noexcept
    {
        // The DEM and SMUTE levels do not matter to the ADC.
        timeline.reset(DeemphasisSelect::off, false);
        decimator.reset();
        highpass.reset();
        initRemaining = initializationFrames;
    }

    // One segment of constant PD state.
    void runSegment(const detail::ControlSegment& seg, StereoView<const double> input,
                    std::span<Pcm20Frame> output) noexcept
    {
        const std::size_t n = seg.length;
        auto out = output.subspan(seg.begin, n);
        if (seg.clearState) {
            // A PD edge: clear the filters (P-108). On release, initialization starts.
            decimator.reset();
            highpass.reset();
            initRemaining = initializationFrames;
        }
        if (seg.powerDown) {
            std::fill(out.begin(), out.end(), Pcm20Frame{}); // "0" data (AD-10)
            return;
        }
        for (std::size_t done = 0; done < n;) {
            const std::size_t m = std::min(maxBlockFrames, n - done);
            const std::size_t from = (seg.begin + done) * oversampling;
            auto l = std::span(left).first(m);
            auto r = std::span(right).first(m);
            decimator.process(input.left.subspan(from, m * oversampling),
                              input.right.subspan(from, m * oversampling), l, r);
            highpass.process(l, r); // after the decimator (AD-13)
            for (std::size_t i = 0; i < m; ++i) {
                Pcm20Frame& frame = out[done + i];
                if (initRemaining > 0) {
                    --initRemaining; // the filters run; the output is still "0" (AD-10, P-108)
                    frame = Pcm20Frame{};
                    continue;
                }
                frame = {quantizeToPcm20(l[i], quantize), quantizeToPcm20(r[i], quantize)};
            }
            done += m;
        }
    }
};

AdcCore::AdcCore() : impl_(std::make_unique<Impl>()) {}
AdcCore::~AdcCore() = default;
AdcCore::AdcCore(AdcCore&&) noexcept = default;
AdcCore& AdcCore::operator=(AdcCore&&) noexcept = default;

Status AdcCore::prepare(const AdcConfig& config, std::size_t maxBlockFrames)
{
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    Impl& a = *impl_;
    a.prepared = false;
    if (const Status s = detail::validate(config); s != Status::ok) {
        return s;
    }
    if (maxBlockFrames == 0) {
        return Status::invalidBlockSize;
    }
    a.config = config;
    a.maxBlockFrames = maxBlockFrames;
    a.left.assign(maxBlockFrames, 0.0);
    a.right.assign(maxBlockFrames, 0.0);
    a.highpass.prepare(config.adcRate);
    a.quantize = {};
    a.clipped = 0;
    a.chunkedBlocks = 0;
    a.rejectedBlocks = 0;
    a.resetState();
    a.prepared = true;
    return Status::ok;
}

void AdcCore::reset() noexcept
{
    if (impl_ && impl_->prepared) {
        impl_->resetState();
    }
}

ProcessResult AdcCore::process(StereoView<const double> input, std::span<const ControlEvent> events,
                               std::span<Pcm20Frame> output) noexcept
{
    if (!impl_ || !impl_->prepared) {
        if (impl_) {
            impl_->rejectedBlocks.fetch_add(1, std::memory_order_relaxed);
        }
        return {Status::notPrepared, 0};
    }
    Impl& a = *impl_;
    auto reject = [&a](Status s) {
        a.rejectedBlocks.fetch_add(1, std::memory_order_relaxed);
        return ProcessResult{s, 0};
    };
    if (!input.consistent()) {
        return reject(Status::channelSizeMismatch);
    }
    const std::size_t frames = output.size();
    if (input.frames() != frames * oversampling) {
        return reject(Status::insufficientOutputCapacity);
    }
    if (const Status s = detail::ControlTimeline::validate(frames, events); s != Status::ok) {
        return reject(s);
    }
    if (frames > a.maxBlockFrames) {
        a.chunkedBlocks.fetch_add(1, std::memory_order_relaxed);
    }

    const std::uint64_t clippedBefore = a.quantize.clipped;
    const Status status = a.timeline.process(frames, events, [&a, input, output](const detail::ControlSegment& seg) {
        a.runSegment(seg, input, output);
    });
    if (status != Status::ok) {
        return reject(status);
    }
    a.clipped.fetch_add(a.quantize.clipped - clippedBefore, std::memory_order_relaxed);
    return {Status::ok, frames};
}

bool AdcCore::isPrepared() const noexcept
{
    return impl_ && impl_->prepared;
}

double AdcCore::inputRate() const noexcept
{
    return isPrepared() ? impl_->config.adcRate * static_cast<double>(oversampling) : 0.0;
}

AdcLatencyInfo AdcCore::latency() const noexcept
{
    AdcLatencyInfo info;
    if (!isPrepared()) {
        return info;
    }
    info.adcRate = impl_->config.adcRate;
    info.coreAdcFrames = detail::Decimator8x::delaySamples / static_cast<double>(oversampling);
    info.totalSeconds = info.coreAdcFrames / info.adcRate;
    return info;
}

Diagnostics AdcCore::diagnostics() const noexcept
{
    Diagnostics d;
    if (!impl_) {
        return d;
    }
    d.clippedInputSamples = impl_->clipped.load(std::memory_order_relaxed);
    d.chunkedBlocks = impl_->chunkedBlocks.load(std::memory_order_relaxed);
    d.rejectedBlocks = impl_->rejectedBlocks.load(std::memory_order_relaxed);
    d.ignoredEvents = impl_->timeline.ignoredEvents(); // repeated PD levels (P-53)
    return d;
}

} // namespace ak4522
