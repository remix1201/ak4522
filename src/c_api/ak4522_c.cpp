// C API over HostProcessor and DacStream (REQ-API-02). Converts C structs to
// the C++ types; no behavior of its own beyond argument checks.

#include "ak4522/ak4522.h"

#include "ak4522/dac_stream.hpp"
#include "ak4522/host_processor.hpp"
#include "ak4522/serial.hpp"
#include "ak4522/version.hpp"

#include <new>
#include <vector>

namespace {

using namespace ak4522;

// The C values must match the C++ enums.
static_assert(AK4522_MODE_MISMATCH == static_cast<int>(Status::modeMismatch));
static_assert(AK4522_EVENTS_NOT_SORTED == static_cast<int>(Status::eventsNotSorted));
static_assert(AK4522_INVALID_DAC_RATE == static_cast<int>(Status::invalidDacRate));
static_assert(AK4522_CMODE_OPEN == static_cast<int>(CmodePin::open));
static_assert(AK4522_DEEMPHASIS_32000 == static_cast<int>(DeemphasisSelect::fs32000));
static_assert(AK4522_DEEMPHASIS_OFF == static_cast<int>(DeemphasisSelect::off));
static_assert(AK4522_PROFILE_MEASURED == static_cast<int>(ModelProfile::measured));
static_assert(AK4522_DISTORTION_MOSTLY_2ND == static_cast<int>(DistortionShape::mostly2nd));
static_assert(AK4522_CROSSTALK_FLAT == static_cast<int>(CrosstalkShape::flat));
static_assert(AK4522_MISMATCH_PER_INSTANCE == static_cast<int>(MismatchMode::perInstance));
static_assert(AK4522_RESOLUTION_16 == static_cast<int>(InputResolution::bits16));
static_assert(AK4522_FORMAT_FLOAT64 == static_cast<int>(DacSampleFormat::float64));
static_assert(AK4522_MODE_PUSH == static_cast<int>(DacStreamMode::push));
static_assert(AK4522_EVENT_SOFT_MUTE == static_cast<int>(ControlType::softMute));
static_assert(AK4522_SERIAL_MODE3_I2S == static_cast<int>(SerialFormat::mode3I2s));
static_assert(AK4522_SERIAL_MODE0_RIGHT_JUSTIFIED_16 == static_cast<int>(SerialFormat::mode0RightJustified16));
static_assert(AK4522_SERIAL_SCLK_RATIO_TOO_LOW == static_cast<int>(SerialEvent::sclkRatioTooLow));
static_assert(AK4522_SERIAL_FRAME_COMPLETE == static_cast<int>(SerialEvent::frameComplete));
static_assert(AK4522_PIN_PD == static_cast<int>(Pin::pd));
static_assert(AK4522_PIN_LRCK == static_cast<int>(Pin::lrck));
static_assert(AK4522_TIMING_CLOCK_STOPPED == static_cast<int>(TimingViolation::clockStopped));
static_assert(AK4522_TIMING_MCLK_NOT_SYNCHRONIZED == static_cast<int>(TimingViolation::mclkNotSynchronized));
static_assert(AK4522_TIMING_KINDS == static_cast<int>(TimingViolation::count_));

// Out-of-range enum values become values the C++ validation rejects.
template <typename Enum>
Enum toEnum(int32_t value) noexcept
{
    return static_cast<Enum>(value < 0 || value > 255 ? 255 : value);
}

AnalogOptions toCpp(const ak4522_analog_options& a) noexcept
{
    AnalogOptions o;
    o.noise = a.noise;
    o.distortion = a.distortion;
    o.channelMismatch = a.channel_mismatch;
    o.crosstalk = a.crosstalk;
    o.dcOffset = a.dc_offset;
    o.distortionShape = toEnum<DistortionShape>(a.distortion_shape);
    o.crosstalkShape = toEnum<CrosstalkShape>(a.crosstalk_shape);
    o.mismatchMode = toEnum<MismatchMode>(a.mismatch_mode);
    o.noiseSeed = a.noise_seed;
    return o;
}

ak4522_analog_options toC(const AnalogOptions& o) noexcept
{
    return {o.noise, o.distortion, o.channelMismatch, o.crosstalk, o.dcOffset,
            static_cast<int32_t>(o.distortionShape), static_cast<int32_t>(o.crosstalkShape),
            static_cast<int32_t>(o.mismatchMode), o.noiseSeed};
}

DacConfig toCpp(const ak4522_dac_config& c) noexcept
{
    DacConfig d;
    d.dacRate = c.dac_rate;
    d.allowExtendedRate = c.allow_extended_rate;
    d.cmode = toEnum<CmodePin>(c.cmode);
    if (c.master_clock_hz != 0.0) {
        d.masterClockHz = c.master_clock_hz;
    }
    d.profile = toEnum<ModelProfile>(c.profile);
    d.analog = toCpp(c.analog);
    d.initialDeemphasis = toEnum<DeemphasisSelect>(c.initial_deemphasis);
    d.initialSoftMute = c.initial_soft_mute;
    return d;
}

ak4522_dac_config toC(const DacConfig& d) noexcept
{
    ak4522_dac_config c{};
    c.dac_rate = d.dacRate;
    c.allow_extended_rate = d.allowExtendedRate;
    c.cmode = static_cast<int32_t>(d.cmode);
    c.master_clock_hz = d.masterClockHz.value_or(0.0);
    c.profile = static_cast<int32_t>(d.profile);
    c.analog = toC(d.analog);
    c.initial_deemphasis = static_cast<int32_t>(d.initialDeemphasis);
    c.initial_soft_mute = d.initialSoftMute;
    return c;
}

ExampleCircuitOptions toCpp(const ak4522_circuit_options& o) noexcept
{
    return {o.include_gain, o.invert, o.load_ohms};
}

ak4522_circuit_options toC(const ExampleCircuitOptions& o) noexcept
{
    return {o.includeGain, o.invert, o.loadOhms};
}

ak4522_latency toC(const LatencyInfo& l) noexcept
{
    return {l.dacRate, l.hostRate, l.outsideDocumentedRate, l.inputSrcHostFrames, l.coreDacFrames,
            l.outputSrcHostFrames, l.alignmentHostFrames, l.bufferingHostFrames, l.totalSeconds,
            l.reportedHostFrames};
}

ak4522_diagnostics toC(const Diagnostics& d) noexcept
{
    return {d.nonFiniteInputSamples, d.clippedInputSamples, d.chunkedBlocks, d.rejectedBlocks,
            d.ignoredEvents, d.droppedEvents, d.hostUnderruns, d.sourceUnderrunFrames,
            d.sourceOverrunFrames};
}

// Converts C events into preallocated scratch. Type and value must fit the
// C++ event; their meaning is checked by the C++ processors.
ak4522_status convertEvents(const ak4522_event* events, size_t count, std::vector<ControlEvent>& scratch,
                            std::span<const ControlEvent>& out) noexcept
{
    if (count == 0) {
        out = {};
        return AK4522_OK;
    }
    if (events == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    if (count > scratch.size()) {
        return AK4522_TOO_MANY_EVENTS;
    }
    for (size_t i = 0; i < count; ++i) {
        const ak4522_event& e = events[i];
        if (e.type < 0 || e.type > AK4522_EVENT_SOFT_MUTE || e.value < 0 || e.value > 255) {
            return AK4522_INVALID_ARGUMENT;
        }
        scratch[i] = {e.offset, static_cast<ControlType>(e.type), static_cast<std::uint8_t>(e.value)};
    }
    out = std::span<const ControlEvent>(scratch.data(), count);
    return AK4522_OK;
}

ak4522_status status(Status s) noexcept
{
    return static_cast<ak4522_status>(s);
}

} // namespace

struct ak4522_serial_decoder {
    SerialDecoder decoder;
};

struct ak4522_timing_validator {
    TimingValidator validator;
};

struct ak4522_circuit {
    ExampleOutputCircuit circuit;
};

struct ak4522_host {
    HostProcessor processor;
    std::vector<ControlEvent> events;
};

struct ak4522_stream {
    DacStream stream;
    std::vector<ControlEvent> events; // push conversion
    std::vector<ak4522_event> pullEvents;
    ak4522_pull_fn pull = nullptr;
    void* pullContext = nullptr;

    // Bridges DacStream's pull callback to the C callback.
    static void trampoline(void* context, DacPullBlock& block) noexcept
    {
        auto& self = *static_cast<ak4522_stream*>(context);
        ak4522_pull_block c{};
        c.frames = block.frames;
        c.int_left = block.integers.left.empty() ? nullptr : block.integers.left.data();
        c.int_right = block.integers.right.empty() ? nullptr : block.integers.right.data();
        c.f32_left = block.floats.left.empty() ? nullptr : block.floats.left.data();
        c.f32_right = block.floats.right.empty() ? nullptr : block.floats.right.data();
        c.f64_left = block.doubles.left.empty() ? nullptr : block.doubles.left.data();
        c.f64_right = block.doubles.right.empty() ? nullptr : block.doubles.right.data();
        c.events = self.pullEvents.data();
        c.event_capacity = self.pullEvents.size();
        c.event_count = 0;
        self.pull(self.pullContext, &c);

        // Malformed events make the block's events invalid; DacStream drops
        // and counts them.
        std::size_t n = c.event_count;
        if (n > block.events.size()) {
            block.eventCount = n;
            return;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const ak4522_event& e = self.pullEvents[i];
            if (e.type < 0 || e.type > AK4522_EVENT_SOFT_MUTE || e.value < 0 || e.value > 255) {
                block.eventCount = block.events.size() + 1;
                return;
            }
            block.events[i] = {e.offset, static_cast<ControlType>(e.type), static_cast<std::uint8_t>(e.value)};
        }
        block.eventCount = n;
    }
};

// The header declares the functions below with C linkage.

const char* ak4522_version(void)
{
    return versionString().data(); // a string literal, so null-terminated
}

const char* ak4522_status_string(ak4522_status s)
{
    switch (s) {
    case AK4522_INVALID_ARGUMENT: return "a required argument was NULL or out of range";
    case AK4522_TOO_MANY_EVENTS: return "more events than max_events_per_call";
    default: break;
    }
    if (s < 0 || s > AK4522_MODE_MISMATCH) {
        return "unknown status";
    }
    return toString(static_cast<Status>(s)).data(); // literals, null-terminated
}

void ak4522_dac_config_default(ak4522_dac_config* config)
{
    if (config != nullptr) {
        *config = toC(DacConfig{});
    }
}

void ak4522_circuit_options_default(ak4522_circuit_options* options)
{
    if (options != nullptr) {
        *options = toC(ExampleCircuitOptions{});
    }
}

void ak4522_host_config_default(ak4522_host_config* config)
{
    if (config == nullptr) {
        return;
    }
    const HostConfig h;
    *config = {};
    config->host_rate = h.hostRate;
    config->max_block_frames = h.maxBlockFrames;
    config->input_resolution = static_cast<int32_t>(h.inputResolution);
    config->input_dither = h.inputDither;
    config->max_pending_events = h.maxPendingEvents;
    config->dither_seed = h.ditherSeed;
    config->max_events_per_call = 256;
    config->example_output_circuit = h.exampleOutputCircuit;
    config->example_circuit = toC(h.exampleCircuit);
    config->dac = toC(h.dac);
}

void ak4522_stream_config_default(ak4522_stream_config* config)
{
    if (config == nullptr) {
        return;
    }
    const DacStreamConfig s;
    *config = {};
    config->host_rate = s.hostRate;
    config->dac_rate_numerator = s.dacRate.numerator;
    config->dac_rate_denominator = s.dacRate.denominator;
    config->format = static_cast<int32_t>(s.format);
    config->mode = static_cast<int32_t>(s.mode);
    config->max_block_frames = s.maxBlockFrames;
    config->input_dither = s.inputDither;
    config->dither_seed = s.ditherSeed;
    config->max_events_per_block = s.maxEventsPerBlock;
    config->push_capacity_frames = s.pushCapacityFrames;
    config->push_capacity_events = s.pushCapacityEvents;
    config->max_events_per_call = 256;
    config->example_output_circuit = s.exampleOutputCircuit;
    config->example_circuit = toC(s.exampleCircuit);
    config->dac = toC(s.dac);
}

ak4522_host* ak4522_host_create(void)
{
    return new (std::nothrow) ak4522_host;
}

void ak4522_host_destroy(ak4522_host* host)
{
    delete host;
}

ak4522_status ak4522_host_prepare(ak4522_host* host, const ak4522_host_config* config)
{
    if (host == nullptr || config == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    HostConfig h;
    h.hostRate = config->host_rate;
    h.maxBlockFrames = config->max_block_frames;
    h.inputResolution = toEnum<InputResolution>(config->input_resolution);
    h.inputDither = config->input_dither;
    h.maxPendingEvents = config->max_pending_events;
    h.ditherSeed = config->dither_seed;
    h.exampleOutputCircuit = config->example_output_circuit;
    h.exampleCircuit = toCpp(config->example_circuit);
    h.dac = toCpp(config->dac);
    if (config->max_events_per_call == 0) {
        return AK4522_INVALID_CONFIGURATION;
    }
    const Status s = host->processor.prepare(h);
    if (s == Status::ok) {
        host->events.assign(config->max_events_per_call, ControlEvent{});
    }
    return status(s);
}

ak4522_status ak4522_host_reset(ak4522_host* host)
{
    if (host == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    host->processor.reset();
    return host->processor.isPrepared() ? AK4522_OK : AK4522_NOT_PREPARED;
}

template <typename Sample>
ak4522_status hostProcess(ak4522_host* host, const Sample* inL, const Sample* inR, Sample* outL,
                                 Sample* outR, size_t frames, const ak4522_event* events, size_t count) noexcept
{
    if (host == nullptr || (frames > 0 && (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr))) {
        return AK4522_INVALID_ARGUMENT;
    }
    std::span<const ControlEvent> e;
    if (const ak4522_status s = convertEvents(events, count, host->events, e); s != AK4522_OK) {
        return s;
    }
    return status(host->processor.process(StereoView<const Sample>{{inL, frames}, {inR, frames}},
                                          StereoView<Sample>{{outL, frames}, {outR, frames}}, e));
}

ak4522_status ak4522_host_process_f32(ak4522_host* host, const float* in_left, const float* in_right,
                                      float* out_left, float* out_right, size_t frames,
                                      const ak4522_event* events, size_t event_count)
{
    return hostProcess(host, in_left, in_right, out_left, out_right, frames, events, event_count);
}

ak4522_status ak4522_host_process_f64(ak4522_host* host, const double* in_left, const double* in_right,
                                      double* out_left, double* out_right, size_t frames,
                                      const ak4522_event* events, size_t event_count)
{
    return hostProcess(host, in_left, in_right, out_left, out_right, frames, events, event_count);
}

ak4522_status ak4522_host_latency(const ak4522_host* host, ak4522_latency* latency)
{
    if (host == nullptr || latency == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    *latency = toC(host->processor.latency());
    return host->processor.isPrepared() ? AK4522_OK : AK4522_NOT_PREPARED;
}

ak4522_status ak4522_host_diagnostics(const ak4522_host* host, ak4522_diagnostics* diagnostics)
{
    if (host == nullptr || diagnostics == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    *diagnostics = toC(host->processor.diagnostics());
    return AK4522_OK;
}

ak4522_stream* ak4522_stream_create(void)
{
    return new (std::nothrow) ak4522_stream;
}

void ak4522_stream_destroy(ak4522_stream* stream)
{
    delete stream;
}

ak4522_status ak4522_stream_prepare(ak4522_stream* stream, const ak4522_stream_config* config)
{
    if (stream == nullptr || config == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    if (config->max_events_per_call == 0) {
        return AK4522_INVALID_CONFIGURATION;
    }
    DacStreamConfig s;
    s.hostRate = config->host_rate;
    s.dacRate = {config->dac_rate_numerator, config->dac_rate_denominator};
    s.format = toEnum<DacSampleFormat>(config->format);
    s.mode = toEnum<DacStreamMode>(config->mode);
    s.maxBlockFrames = config->max_block_frames;
    s.inputDither = config->input_dither;
    s.ditherSeed = config->dither_seed;
    s.maxEventsPerBlock = config->max_events_per_block;
    s.pushCapacityFrames = config->push_capacity_frames;
    s.pushCapacityEvents = config->push_capacity_events;
    s.exampleOutputCircuit = config->example_output_circuit;
    s.exampleCircuit = toCpp(config->example_circuit);
    s.dac = toCpp(config->dac);
    if (config->pull != nullptr) {
        s.pull = &ak4522_stream::trampoline;
        s.pullContext = stream;
    }
    stream->pull = config->pull;
    stream->pullContext = config->pull_context;
    const Status r = stream->stream.prepare(s);
    if (r == Status::ok) {
        stream->events.assign(config->max_events_per_call, ControlEvent{});
        stream->pullEvents.assign(config->max_events_per_block, ak4522_event{});
    }
    return status(r);
}

ak4522_status ak4522_stream_reset(ak4522_stream* stream)
{
    if (stream == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    stream->stream.reset();
    return stream->stream.isPrepared() ? AK4522_OK : AK4522_NOT_PREPARED;
}

template <typename Sample>
ak4522_status streamRender(ak4522_stream* stream, Sample* outL, Sample* outR, size_t frames) noexcept
{
    if (stream == nullptr || (frames > 0 && (outL == nullptr || outR == nullptr))) {
        return AK4522_INVALID_ARGUMENT;
    }
    return status(stream->stream.render(StereoView<Sample>{{outL, frames}, {outR, frames}}));
}

ak4522_status ak4522_stream_render_f32(ak4522_stream* stream, float* out_left, float* out_right, size_t frames)
{
    return streamRender(stream, out_left, out_right, frames);
}

ak4522_status ak4522_stream_render_f64(ak4522_stream* stream, double* out_left, double* out_right, size_t frames)
{
    return streamRender(stream, out_left, out_right, frames);
}

template <typename Sample>
ak4522_status streamPush(ak4522_stream* stream, const Sample* left, const Sample* right, size_t frames,
                                const ak4522_event* events, size_t count, size_t* accepted) noexcept
{
    if (accepted != nullptr) {
        *accepted = 0;
    }
    if (stream == nullptr || (frames > 0 && (left == nullptr || right == nullptr))) {
        return AK4522_INVALID_ARGUMENT;
    }
    std::span<const ControlEvent> e;
    if (const ak4522_status s = convertEvents(events, count, stream->events, e); s != AK4522_OK) {
        return s;
    }
    const PushResult r = stream->stream.push(StereoView<const Sample>{{left, frames}, {right, frames}}, e);
    if (accepted != nullptr) {
        *accepted = r.framesAccepted;
    }
    return status(r.status);
}

ak4522_status ak4522_stream_push_i32(ak4522_stream* stream, const int32_t* left, const int32_t* right,
                                     size_t frames, const ak4522_event* events, size_t event_count,
                                     size_t* accepted)
{
    return streamPush(stream, left, right, frames, events, event_count, accepted);
}

ak4522_status ak4522_stream_push_f32(ak4522_stream* stream, const float* left, const float* right,
                                     size_t frames, const ak4522_event* events, size_t event_count,
                                     size_t* accepted)
{
    return streamPush(stream, left, right, frames, events, event_count, accepted);
}

ak4522_status ak4522_stream_push_f64(ak4522_stream* stream, const double* left, const double* right,
                                     size_t frames, const ak4522_event* events, size_t event_count,
                                     size_t* accepted)
{
    return streamPush(stream, left, right, frames, events, event_count, accepted);
}

size_t ak4522_stream_queued_frames(const ak4522_stream* stream)
{
    return stream == nullptr ? 0 : stream->stream.queuedFrames();
}

size_t ak4522_stream_free_frames(const ak4522_stream* stream)
{
    return stream == nullptr ? 0 : stream->stream.freeFrames();
}

size_t ak4522_stream_dac_frames_for(const ak4522_stream* stream, size_t host_frames)
{
    return stream == nullptr ? 0 : stream->stream.dacFramesFor(host_frames);
}

ak4522_status ak4522_stream_latency(const ak4522_stream* stream, ak4522_latency* latency)
{
    if (stream == nullptr || latency == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    *latency = toC(stream->stream.latency());
    return stream->stream.isPrepared() ? AK4522_OK : AK4522_NOT_PREPARED;
}

ak4522_status ak4522_stream_diagnostics(const ak4522_stream* stream, ak4522_diagnostics* diagnostics)
{
    if (stream == nullptr || diagnostics == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    *diagnostics = toC(stream->stream.diagnostics());
    return AK4522_OK;
}

ak4522_serial_decoder* ak4522_serial_decoder_create(void)
{
    return new (std::nothrow) ak4522_serial_decoder;
}

void ak4522_serial_decoder_destroy(ak4522_serial_decoder* decoder)
{
    delete decoder;
}

ak4522_status ak4522_serial_decoder_prepare(ak4522_serial_decoder* decoder, int32_t format)
{
    if (decoder == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    return status(decoder->decoder.prepare(toEnum<SerialFormat>(format)));
}

ak4522_status ak4522_serial_decoder_reset(ak4522_serial_decoder* decoder)
{
    if (decoder == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    decoder->decoder.reset();
    return AK4522_OK;
}

ak4522_status ak4522_serial_decoder_sclk_rising_edge(ak4522_serial_decoder* decoder, bool lrck, bool sdti,
                                                     int32_t* event, int32_t* left, int32_t* right)
{
    if (decoder == nullptr || event == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    const SerialEdgeResult r = decoder->decoder.sclkRisingEdge(lrck, sdti);
    *event = static_cast<int32_t>(r.event);
    if (r.frame) {
        if (left != nullptr) {
            *left = r.frame->left.code;
        }
        if (right != nullptr) {
            *right = r.frame->right.code;
        }
    }
    return AK4522_OK;
}

ak4522_timing_validator* ak4522_timing_validator_create(void)
{
    return new (std::nothrow) ak4522_timing_validator;
}

void ak4522_timing_validator_destroy(ak4522_timing_validator* validator)
{
    delete validator;
}

ak4522_status ak4522_timing_validator_prepare(ak4522_timing_validator* validator, int32_t cmode)
{
    if (validator == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    return status(validator->validator.prepare(toEnum<CmodePin>(cmode)));
}

ak4522_status ak4522_timing_validator_reset(ak4522_timing_validator* validator)
{
    if (validator == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    validator->validator.reset();
    return AK4522_OK;
}

ak4522_status ak4522_timing_validator_observe(ak4522_timing_validator* validator, const ak4522_pin_edge* edge)
{
    if (validator == nullptr || edge == nullptr || edge->pin < 0 || edge->pin > AK4522_PIN_PD) {
        return AK4522_INVALID_ARGUMENT;
    }
    validator->validator.observe({edge->time_ns, static_cast<Pin>(edge->pin), edge->level});
    return AK4522_OK;
}

ak4522_status ak4522_timing_validator_advance_to(ak4522_timing_validator* validator, double time_ns)
{
    if (validator == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    validator->validator.advanceTo(time_ns);
    return AK4522_OK;
}

uint64_t ak4522_timing_validator_violations(const ak4522_timing_validator* validator, int32_t kind)
{
    if (validator == nullptr || kind < 0 || kind >= AK4522_TIMING_KINDS) {
        return 0;
    }
    return validator->validator.violations(static_cast<TimingViolation>(kind));
}

uint64_t ak4522_timing_validator_total_violations(const ak4522_timing_validator* validator)
{
    return validator == nullptr ? 0 : validator->validator.totalViolations();
}

ak4522_circuit* ak4522_circuit_create(void)
{
    return new (std::nothrow) ak4522_circuit;
}

void ak4522_circuit_destroy(ak4522_circuit* circuit)
{
    delete circuit;
}

ak4522_status ak4522_circuit_prepare(ak4522_circuit* circuit, double sample_rate, const ak4522_circuit_options* options)
{
    if (circuit == nullptr || options == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    return status(circuit->circuit.prepare(sample_rate, toCpp(*options)));
}

ak4522_status ak4522_circuit_reset(ak4522_circuit* circuit)
{
    if (circuit == nullptr) {
        return AK4522_INVALID_ARGUMENT;
    }
    circuit->circuit.reset();
    return AK4522_OK;
}

template <typename Sample>
ak4522_status circuitProcess(ak4522_circuit* circuit, Sample* left, Sample* right, size_t frames) noexcept
{
    if (circuit == nullptr || (frames > 0 && (left == nullptr || right == nullptr))) {
        return AK4522_INVALID_ARGUMENT;
    }
    if (!circuit->circuit.isPrepared()) {
        return AK4522_NOT_PREPARED;
    }
    circuit->circuit.process(StereoView<Sample>{{left, frames}, {right, frames}});
    return AK4522_OK;
}

ak4522_status ak4522_circuit_process_f32(ak4522_circuit* circuit, float* left, float* right, size_t frames)
{
    return circuitProcess(circuit, left, right, frames);
}

ak4522_status ak4522_circuit_process_f64(ak4522_circuit* circuit, double* left, double* right, size_t frames)
{
    return circuitProcess(circuit, left, right, frames);
}
