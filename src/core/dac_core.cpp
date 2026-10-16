#include "ak4522/dac_core.hpp"

#include "core/config_validation.hpp"
#include "core/control_timeline.hpp"
#include "dsp/deemphasis.hpp"
#include "dsp/interpolator.hpp"
#include "dsp/reconstruction.hpp"

#include <algorithm>
#include <atomic>
#include <vector>

namespace ak4522 {

namespace {

// Delay of the linear chip model from input register to analog output, in
// DAC frames: interpolator 11.875 + pipeline 2.625 + reconstruction, with the
// reconstruction cutoff solved for this total (D-F06, P-87, P-89). Measured
// on this implementation by CTest dsp.dac_core_response_*.
constexpr double coreDelayFrames = 14.7;

bool analogOptionsRequested(const AnalogOptions& options) noexcept
{
    return options.noise || options.distortion || options.channelMismatch || options.crosstalk
        || options.dcOffset;
}

void zero(std::span<double> samples) noexcept
{
    std::fill(samples.begin(), samples.end(), 0.0);
}

} // namespace

struct DacCore::Impl {
    DacConfig config{};
    std::size_t maxBlockFrames = 0;
    bool prepared = false;

    detail::ControlTimeline timeline;
    detail::Deemphasis deemphasis;
    detail::Interpolator8x interpolator;
    detail::PipelineDelay pipeline;
    detail::Reconstruction reconstruction;

    // DAC-rate scratch for one chunk.
    std::vector<double> left;
    std::vector<double> right;

    std::atomic<std::uint64_t> chunkedBlocks{0};
    std::atomic<std::uint64_t> rejectedBlocks{0};
    std::atomic<std::uint64_t> ignoredEvents{0};

    void resetState() noexcept
    {
        timeline.reset(config.initialDeemphasis, config.initialSoftMute);
        clearSignalState();
    }

    void clearSignalState() noexcept
    {
        deemphasis.reset();
        interpolator.reset();
        pipeline.reset();
        reconstruction.reset();
    }

    // Runs frames [begin, begin + count) of a segment with constant controls.
    void runChunk(std::span<const Pcm20Frame> input, StereoView<double> output) noexcept
    {
        const std::size_t n = input.size();
        auto l = std::span(left).first(n);
        auto r = std::span(right).first(n);
        for (std::size_t i = 0; i < n; ++i) {
            l[i] = toNormalized(input[i].left);
            r[i] = toNormalized(input[i].right);
        }
        deemphasis.process(l, r);                // P-40: de-emphasis, then mute
        for (std::size_t i = 0; i < n; ++i) {
            const double gain = timeline.mute().nextGain();
            l[i] *= gain;
            r[i] *= gain;
        }
        interpolator.process(l, r, output.left, output.right);
        pipeline.process(output.left, output.right);
        reconstruction.process(output.left, output.right);
    }
};

DacCore::DacCore() : impl_(std::make_unique<Impl>()) {}
DacCore::~DacCore() = default;
DacCore::DacCore(DacCore&&) noexcept = default;
DacCore& DacCore::operator=(DacCore&&) noexcept = default;

Status DacCore::prepare(const DacConfig& config, std::size_t maxBlockFrames)
{
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    impl_->prepared = false;

    if (const Status status = detail::validate(config); status != Status::ok) {
        return status;
    }
    if (maxBlockFrames == 0) {
        return Status::invalidBlockSize;
    }
    // The NominalAnalog profile and analog imperfections arrive in M4.
    if (config.profile != ModelProfile::specification || analogOptionsRequested(config.analog)) {
        return Status::invalidConfiguration;
    }

    impl_->config = config;
    impl_->maxBlockFrames = maxBlockFrames;
    impl_->left.assign(maxBlockFrames, 0.0);
    impl_->right.assign(maxBlockFrames, 0.0);
    impl_->chunkedBlocks = 0;
    impl_->rejectedBlocks = 0;
    impl_->ignoredEvents = 0;
    impl_->deemphasis = detail::Deemphasis{};
    impl_->resetState();
    impl_->prepared = true;
    return Status::ok;
}

void DacCore::reset() noexcept
{
    if (impl_ && impl_->prepared) {
        impl_->resetState();
    }
}

ProcessResult DacCore::process(std::span<const Pcm20Frame> input,
                               std::span<const ControlEvent> events,
                               StereoView<double> output) noexcept
{
    if (!impl_ || !impl_->prepared) {
        if (impl_) {
            impl_->rejectedBlocks.fetch_add(1, std::memory_order_relaxed);
        }
        return {Status::notPrepared, 0};
    }
    Impl& core = *impl_;
    auto reject = [&core](Status status) {
        core.rejectedBlocks.fetch_add(1, std::memory_order_relaxed);
        return ProcessResult{status, 0};
    };
    if (!output.consistent()) {
        return reject(Status::channelSizeMismatch);
    }
    const std::size_t outFrames = input.size() * oversampling;
    if (output.frames() < outFrames) {
        return reject(Status::insufficientOutputCapacity);
    }
    if (input.size() > core.maxBlockFrames) {
        core.chunkedBlocks.fetch_add(1, std::memory_order_relaxed);
    }

    const std::uint64_t ignoredBefore = core.timeline.ignoredEvents();
    const Status status = core.timeline.process(
        input.size(), events, [&core, input, output](const detail::ControlSegment& segment) {
            if (segment.clearState) {
                core.clearSignalState(); // PD edge or startup (P-71)
            }
            if (segment.deemphasisChanged) {
                core.deemphasis.select(segment.deemphasis); // resets on change (P-64)
            }
            const auto segOut = StereoView<double>{
                output.left.subspan(segment.begin * oversampling, segment.length * oversampling),
                output.right.subspan(segment.begin * oversampling, segment.length * oversampling)};
            if (segment.powerDown) {
                zero(segOut.left); // output at VCOM while PD is low (P-71)
                zero(segOut.right);
                return;
            }
            for (std::size_t done = 0; done < segment.length;) {
                const std::size_t n = std::min(core.maxBlockFrames, segment.length - done);
                core.runChunk(input.subspan(segment.begin + done, n),
                              StereoView<double>{segOut.left.subspan(done * oversampling, n * oversampling),
                                                 segOut.right.subspan(done * oversampling, n * oversampling)});
                done += n;
            }
        });
    if (status != Status::ok) {
        return reject(status);
    }
    core.ignoredEvents.fetch_add(core.timeline.ignoredEvents() - ignoredBefore,
                                 std::memory_order_relaxed);
    return {Status::ok, outFrames};
}

bool DacCore::isPrepared() const noexcept
{
    return impl_ && impl_->prepared;
}

double DacCore::outputRate() const noexcept
{
    return isPrepared() ? impl_->config.dacRate * static_cast<double>(oversampling) : 0.0;
}

LatencyInfo DacCore::latency() const noexcept
{
    LatencyInfo info;
    if (!isPrepared()) {
        return info;
    }
    info.dacRate = impl_->config.dacRate;
    info.coreDacFrames = coreDelayFrames;
    info.totalSeconds = coreDelayFrames / impl_->config.dacRate;
    return info;
}

Diagnostics DacCore::diagnostics() const noexcept
{
    Diagnostics d;
    if (!impl_) {
        return d;
    }
    d.chunkedBlocks = impl_->chunkedBlocks.load(std::memory_order_relaxed);
    d.rejectedBlocks = impl_->rejectedBlocks.load(std::memory_order_relaxed);
    d.ignoredEvents = impl_->ignoredEvents.load(std::memory_order_relaxed);
    return d;
}

} // namespace ak4522
