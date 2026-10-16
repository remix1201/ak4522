/*
 * C API for the AK4522 DAC emulation library (REQ-API-02).
 *
 * Usable from C99 and later, C++, and other languages with a C foreign
 * function interface. It wraps HostProcessor (an audio effect at the host
 * rate), DacStream (a source at its own DAC rate), and the SerialDecoder and
 * TimingValidator conformance tools; see docs/api.md for their behavior. Alpha: the API and ABI may change in any release.
 *
 * Conventions:
 * - Handles are opaque. Create and destroy them off the audio thread.
 * - Every function that can fail returns an ak4522_status. Passing NULL for a
 *   required pointer returns AK4522_INVALID_ARGUMENT and does nothing else.
 * - Buffers belong to the caller and are used only during the call. Channels
 *   are planar: one array per channel.
 * - Fill configuration structs with the matching *_default function first,
 *   then change the fields you need.
 * - Thread rules follow the C++ classes: prepare, reset, and destroy never
 *   run concurrently with processing. ak4522_stream_push may run on a
 *   different thread from ak4522_stream_render. Diagnostics and the stream
 *   queue queries may be called from any thread.
 */
#ifndef AK4522_H
#define AK4522_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes. Values up to AK4522_MODE_MISMATCH match ak4522::Status. */
typedef int32_t ak4522_status;
enum {
    AK4522_OK = 0,
    AK4522_NOT_PREPARED = 1,
    AK4522_INVALID_DAC_RATE = 2,
    AK4522_INVALID_MASTER_CLOCK = 3,
    AK4522_UNSUPPORTED_HOST_RATE = 4,
    AK4522_UNSUPPORTED_RATE_PAIR = 5,
    AK4522_INVALID_BLOCK_SIZE = 6,
    AK4522_INVALID_CONFIGURATION = 7,
    AK4522_CHANNEL_SIZE_MISMATCH = 8,
    AK4522_INSUFFICIENT_OUTPUT_CAPACITY = 9,
    AK4522_EVENT_OUT_OF_RANGE = 10,
    AK4522_EVENTS_NOT_SORTED = 11,
    AK4522_FORMAT_MISMATCH = 12,
    AK4522_MODE_MISMATCH = 13,
    /* C API only. */
    AK4522_INVALID_ARGUMENT = 100, /* a required pointer was NULL */
    AK4522_TOO_MANY_EVENTS = 101   /* more events than max_events_per_call */
};

/* Pins and options; values match the C++ enums. */
enum { AK4522_CMODE_LOW = 0, AK4522_CMODE_HIGH = 1, AK4522_CMODE_OPEN = 2 };
enum {
    AK4522_DEEMPHASIS_44100 = 0,
    AK4522_DEEMPHASIS_OFF = 1,
    AK4522_DEEMPHASIS_48000 = 2,
    AK4522_DEEMPHASIS_32000 = 3
};
enum { AK4522_PROFILE_SPECIFICATION = 0, AK4522_PROFILE_NOMINAL_ANALOG = 1, AK4522_PROFILE_MEASURED = 2 };
enum { AK4522_DISTORTION_EQUAL_2ND_3RD = 0, AK4522_DISTORTION_MOSTLY_3RD = 1, AK4522_DISTORTION_MOSTLY_2ND = 2 };
enum { AK4522_CROSSTALK_CAPACITIVE = 0, AK4522_CROSSTALK_FLAT = 1 };
enum { AK4522_MISMATCH_FIXED = 0, AK4522_MISMATCH_PER_INSTANCE = 1 };
enum { AK4522_RESOLUTION_20 = 0, AK4522_RESOLUTION_16 = 1 };
enum {
    AK4522_FORMAT_INT16 = 0,
    AK4522_FORMAT_INT20 = 1,
    AK4522_FORMAT_INT24 = 2,
    AK4522_FORMAT_FLOAT32 = 3,
    AK4522_FORMAT_FLOAT64 = 4
};
enum { AK4522_MODE_PULL = 0, AK4522_MODE_PUSH = 1 };
enum { AK4522_EVENT_POWER_DOWN = 0, AK4522_EVENT_DEEMPHASIS = 1, AK4522_EVENT_SOFT_MUTE = 2 };
/* Serial formats; the values are the DIF1:DIF0 pin bits. */
enum {
    AK4522_SERIAL_MODE0_RIGHT_JUSTIFIED_16 = 0,
    AK4522_SERIAL_MODE1_RIGHT_JUSTIFIED_20 = 1,
    AK4522_SERIAL_MODE2_LEFT_JUSTIFIED_20 = 2,
    AK4522_SERIAL_MODE3_I2S = 3
};
/* Outcome of one SCLK rising edge in the serial decoder. */
enum {
    AK4522_SERIAL_NONE = 0,
    AK4522_SERIAL_FRAME_COMPLETE = 1,
    AK4522_SERIAL_INCOMPLETE_FRAME = 2,
    AK4522_SERIAL_SCLK_RATIO_TOO_LOW = 3
};
enum { AK4522_PIN_MCLK = 0, AK4522_PIN_LRCK = 1, AK4522_PIN_SCLK = 2, AK4522_PIN_SDTI = 3, AK4522_PIN_PD = 4 };
/* Timing checks counted by the validator; limits in docs/features.md. */
enum {
    AK4522_TIMING_SCLK_PERIOD = 0,
    AK4522_TIMING_SCLK_PULSE_WIDTH = 1,
    AK4522_TIMING_LRCK_TO_SCLK_RISE = 2,
    AK4522_TIMING_SCLK_RISE_TO_LRCK = 3,
    AK4522_TIMING_SCLK_RISE_COINCIDES_WITH_LRCK = 4,
    AK4522_TIMING_SDTI_SETUP = 5,
    AK4522_TIMING_SDTI_HOLD = 6,
    AK4522_TIMING_LRCK_DUTY_CYCLE = 7,
    AK4522_TIMING_LRCK_FREQUENCY = 8,
    AK4522_TIMING_MCLK_FREQUENCY = 9,
    AK4522_TIMING_MCLK_PULSE_WIDTH = 10,
    AK4522_TIMING_MCLK_NOT_SYNCHRONIZED = 11,
    AK4522_TIMING_PD_PULSE_WIDTH = 12,
    AK4522_TIMING_CLOCK_STOPPED = 13,
    AK4522_TIMING_KINDS = 14
};

/* A pin level with an absolute timestamp, for the timing validator. */
typedef struct ak4522_pin_edge {
    double time_ns;
    int32_t pin;
    bool level;
} ak4522_pin_edge;

/* A timestamped pin change. offset is in host frames for the host processor
 * and DAC frames for the stream, relative to the block. value: PD and SMUTE
 * 0 = pin low, 1 = pin high; DEM one of AK4522_DEEMPHASIS_*. */
typedef struct ak4522_event {
    uint32_t offset;
    int32_t type;
    int32_t value;
} ak4522_event;

typedef struct ak4522_analog_options {
    bool noise;
    bool distortion;
    bool channel_mismatch;
    bool crosstalk;
    bool dc_offset;
    int32_t distortion_shape;
    int32_t crosstalk_shape;
    int32_t mismatch_mode;
    uint64_t noise_seed;
} ak4522_analog_options;

typedef struct ak4522_dac_config {
    double dac_rate;          /* Hz; ignored by the stream, which uses its exact rate */
    bool allow_extended_rate; /* accept up to 50400 Hz (REQ-CLOCK-02) */
    int32_t cmode;
    double master_clock_hz;   /* 0 = not given */
    int32_t profile;
    ak4522_analog_options analog;
    int32_t initial_deemphasis;
    bool initial_soft_mute;
} ak4522_dac_config;

/* The datasheet's example output circuit (Fig.11), board behavior after the
 * DAC; see ExampleCircuitOptions. */
typedef struct ak4522_circuit_options {
    bool include_gain; /* -1.8 stage gain; false keeps the DAC level */
    bool invert;       /* the stage inverts; false restores polarity */
    double load_ohms;  /* external load across the 27 kohm output; 0 = none */
} ak4522_circuit_options;

typedef struct ak4522_host_config {
    double host_rate;
    size_t max_block_frames;
    int32_t input_resolution;
    bool input_dither;
    size_t max_pending_events;
    uint64_t dither_seed;
    size_t max_events_per_call; /* events accepted by one process call */
    bool example_output_circuit; /* apply the Fig.11 circuit at the host rate */
    ak4522_circuit_options example_circuit;
    ak4522_dac_config dac;
} ak4522_host_config;

/* One request from render to a pull source: write `frames` DAC frames into
 * the pointers matching the configured format (the others are NULL), and
 * optionally up to event_capacity events, setting event_count. */
typedef struct ak4522_pull_block {
    size_t frames;
    int32_t* int_left;
    int32_t* int_right;
    float* f32_left;
    float* f32_right;
    double* f64_left;
    double* f64_right;
    ak4522_event* events;
    size_t event_capacity;
    size_t event_count;
} ak4522_pull_block;

/* Called on the audio thread; must not block. */
typedef void (*ak4522_pull_fn)(void* context, ak4522_pull_block* block);

typedef struct ak4522_stream_config {
    double host_rate;
    uint64_t dac_rate_numerator;   /* exact DAC rate in Hz: numerator / denominator */
    uint64_t dac_rate_denominator;
    int32_t format;
    int32_t mode;
    size_t max_block_frames;
    bool input_dither;          /* float formats: TPDF dither before quantization */
    uint64_t dither_seed;
    ak4522_pull_fn pull;
    void* pull_context;
    size_t max_events_per_block;
    size_t push_capacity_frames;
    size_t push_capacity_events;
    size_t max_events_per_call; /* events accepted by one push call */
    bool example_output_circuit; /* apply the Fig.11 circuit at the host rate */
    ak4522_circuit_options example_circuit;
    ak4522_dac_config dac;
} ak4522_stream_config;

typedef struct ak4522_latency {
    double dac_rate;
    double host_rate;
    bool outside_documented_rate;
    double input_src_host_frames;
    double core_dac_frames;
    double output_src_host_frames;
    double alignment_host_frames;
    uint32_t buffering_host_frames;
    double total_seconds;
    uint32_t reported_host_frames;
} ak4522_latency;

typedef struct ak4522_diagnostics {
    uint64_t non_finite_input_samples;
    uint64_t clipped_input_samples;
    uint64_t chunked_blocks;
    uint64_t rejected_blocks;
    uint64_t ignored_events;
    uint64_t dropped_events;
    uint64_t host_underruns;
    uint64_t source_underrun_frames;
    uint64_t source_overrun_frames;
} ak4522_diagnostics;

typedef struct ak4522_host ak4522_host;
typedef struct ak4522_stream ak4522_stream;
typedef struct ak4522_serial_decoder ak4522_serial_decoder;
typedef struct ak4522_timing_validator ak4522_timing_validator;
typedef struct ak4522_circuit ak4522_circuit;

/* Library */
const char* ak4522_version(void);
const char* ak4522_status_string(ak4522_status status);

/* Defaults: the same as the C++ structs. */
void ak4522_dac_config_default(ak4522_dac_config* config);
void ak4522_circuit_options_default(ak4522_circuit_options* options);
void ak4522_host_config_default(ak4522_host_config* config);
void ak4522_stream_config_default(ak4522_stream_config* config);

/* Host processor: host-rate audio in and out (HostProcessor). */
ak4522_host* ak4522_host_create(void); /* NULL if out of memory */
void ak4522_host_destroy(ak4522_host* host); /* NULL is ignored */
ak4522_status ak4522_host_prepare(ak4522_host* host, const ak4522_host_config* config);
ak4522_status ak4522_host_reset(ak4522_host* host);
ak4522_status ak4522_host_process_f32(ak4522_host* host, const float* in_left, const float* in_right,
                                      float* out_left, float* out_right, size_t frames,
                                      const ak4522_event* events, size_t event_count);
ak4522_status ak4522_host_process_f64(ak4522_host* host, const double* in_left, const double* in_right,
                                      double* out_left, double* out_right, size_t frames,
                                      const ak4522_event* events, size_t event_count);
ak4522_status ak4522_host_latency(const ak4522_host* host, ak4522_latency* latency);
ak4522_status ak4522_host_diagnostics(const ak4522_host* host, ak4522_diagnostics* diagnostics);

/* Stream: a source at its own DAC rate (DacStream). */
ak4522_stream* ak4522_stream_create(void);
void ak4522_stream_destroy(ak4522_stream* stream);
ak4522_status ak4522_stream_prepare(ak4522_stream* stream, const ak4522_stream_config* config);
ak4522_status ak4522_stream_reset(ak4522_stream* stream);
ak4522_status ak4522_stream_render_f32(ak4522_stream* stream, float* out_left, float* out_right, size_t frames);
ak4522_status ak4522_stream_render_f64(ak4522_stream* stream, double* out_left, double* out_right, size_t frames);
/* Push mode. accepted (may be NULL) receives the frames queued; the rest
 * were dropped because the queue was full. */
ak4522_status ak4522_stream_push_i32(ak4522_stream* stream, const int32_t* left, const int32_t* right,
                                     size_t frames, const ak4522_event* events, size_t event_count,
                                     size_t* accepted);
ak4522_status ak4522_stream_push_f32(ak4522_stream* stream, const float* left, const float* right,
                                     size_t frames, const ak4522_event* events, size_t event_count,
                                     size_t* accepted);
ak4522_status ak4522_stream_push_f64(ak4522_stream* stream, const double* left, const double* right,
                                     size_t frames, const ak4522_event* events, size_t event_count,
                                     size_t* accepted);
size_t ak4522_stream_queued_frames(const ak4522_stream* stream);
size_t ak4522_stream_free_frames(const ak4522_stream* stream);
size_t ak4522_stream_dac_frames_for(const ak4522_stream* stream, size_t host_frames);
ak4522_status ak4522_stream_latency(const ak4522_stream* stream, ak4522_latency* latency);
ak4522_status ak4522_stream_diagnostics(const ak4522_stream* stream, ak4522_diagnostics* diagnostics);

/* Serial decoder: SDTI bits to 20-bit frames (SerialDecoder). Call once per
 * SCLK rising edge with the latched LRCK and SDTI levels. event receives an
 * AK4522_SERIAL_* value; on AK4522_SERIAL_FRAME_COMPLETE, left and right (each
 * may be NULL) receive the 20-bit codes. */
ak4522_serial_decoder* ak4522_serial_decoder_create(void);
void ak4522_serial_decoder_destroy(ak4522_serial_decoder* decoder);
ak4522_status ak4522_serial_decoder_prepare(ak4522_serial_decoder* decoder, int32_t format);
ak4522_status ak4522_serial_decoder_reset(ak4522_serial_decoder* decoder);
ak4522_status ak4522_serial_decoder_sclk_rising_edge(ak4522_serial_decoder* decoder, bool lrck, bool sdti,
                                                     int32_t* event, int32_t* left, int32_t* right);

/* Timing validator: counts datasheet timing violations in timestamped pin
 * transitions (TimingValidator). Timestamps must not decrease. */
ak4522_timing_validator* ak4522_timing_validator_create(void);
void ak4522_timing_validator_destroy(ak4522_timing_validator* validator);
ak4522_status ak4522_timing_validator_prepare(ak4522_timing_validator* validator, int32_t cmode);
ak4522_status ak4522_timing_validator_reset(ak4522_timing_validator* validator);
ak4522_status ak4522_timing_validator_observe(ak4522_timing_validator* validator, const ak4522_pin_edge* edge);
/* Advances time without an edge, so clocks that stop for good are reported. */
ak4522_status ak4522_timing_validator_advance_to(ak4522_timing_validator* validator, double time_ns);
uint64_t ak4522_timing_validator_violations(const ak4522_timing_validator* validator, int32_t kind);
uint64_t ak4522_timing_validator_total_violations(const ak4522_timing_validator* validator);

/* The example output circuit on its own, for example after the bare core
 * (ExampleOutputCircuit). Processes in place. */
ak4522_circuit* ak4522_circuit_create(void);
void ak4522_circuit_destroy(ak4522_circuit* circuit);
ak4522_status ak4522_circuit_prepare(ak4522_circuit* circuit, double sample_rate, const ak4522_circuit_options* options);
ak4522_status ak4522_circuit_reset(ak4522_circuit* circuit);
ak4522_status ak4522_circuit_process_f32(ak4522_circuit* circuit, float* left, float* right, size_t frames);
ak4522_status ak4522_circuit_process_f64(ak4522_circuit* circuit, double* left, double* right, size_t frames);

#ifdef __cplusplus
}
#endif

#endif /* AK4522_H */
