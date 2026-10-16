/* C API test, compiled as C99 (REQ-API-02). Exercises every function through
 * C only; the C++ equivalence test checks the output against the C++ API. */

#include "ak4522/ak4522.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

static const double pi = 3.14159265358979323846;

typedef struct sine_source {
    double freq;
    double rate;
    size_t position;
    int emit_mute; /* raise SMUTE at DAC frame 3000 */
} sine_source;

static void pull_sine(void* context, ak4522_pull_block* block)
{
    sine_source* s = (sine_source*)context;
    size_t j;
    for (j = 0; j < block->frames; ++j) {
        double x = 0.5 * sin(2.0 * pi * s->freq * (double)(s->position + j) / s->rate);
        block->f32_left[j] = (float)x;
        block->f32_right[j] = (float)x;
    }
    if (s->emit_mute && s->position <= 3000 && 3000 < s->position + block->frames && block->event_capacity > 0) {
        block->events[0].offset = (uint32_t)(3000 - s->position);
        block->events[0].type = AK4522_EVENT_SOFT_MUTE;
        block->events[0].value = 1;
        block->event_count = 1;
    }
    s->position += block->frames;
}

static void test_library(void)
{
    const ak4522_status known[] = {AK4522_OK, AK4522_NOT_PREPARED, AK4522_INVALID_DAC_RATE,
                                   AK4522_FORMAT_MISMATCH, AK4522_MODE_MISMATCH,
                                   AK4522_INVALID_ARGUMENT, AK4522_TOO_MANY_EVENTS};
    size_t i, j;
    CHECK(strlen(ak4522_version()) > 0);
    for (i = 0; i < sizeof known / sizeof known[0]; ++i) {
        CHECK(strcmp(ak4522_status_string(known[i]), "unknown status") != 0);
        for (j = 0; j < i; ++j) {
            CHECK(strcmp(ak4522_status_string(known[i]), ak4522_status_string(known[j])) != 0);
        }
    }
    CHECK(strcmp(ak4522_status_string(55), "unknown status") == 0);
}

static void test_null_arguments(void)
{
    ak4522_host_config hc;
    ak4522_latency latency;
    float buffer[4] = {0};
    ak4522_host* host = ak4522_host_create();
    ak4522_host_config_default(&hc);
    CHECK(ak4522_host_prepare(NULL, &hc) == AK4522_INVALID_ARGUMENT);
    CHECK(ak4522_host_prepare(host, NULL) == AK4522_INVALID_ARGUMENT);
    CHECK(ak4522_host_process_f32(host, NULL, buffer, buffer, buffer, 4, NULL, 0) == AK4522_INVALID_ARGUMENT);
    CHECK(ak4522_host_process_f32(host, buffer, buffer, buffer, buffer, 4, NULL, 0) == AK4522_NOT_PREPARED);
    CHECK(ak4522_host_latency(host, &latency) == AK4522_NOT_PREPARED);
    CHECK(ak4522_host_latency(host, NULL) == AK4522_INVALID_ARGUMENT);
    CHECK(ak4522_stream_queued_frames(NULL) == 0);
    ak4522_host_destroy(host);
    ak4522_host_destroy(NULL);
    ak4522_stream_destroy(NULL);
}

static void test_host(void)
{
    enum { frames = 4800, block = 480 };
    static float in_l[frames], in_r[frames], out_l[frames], out_r[frames];
    ak4522_host_config config;
    ak4522_latency latency;
    ak4522_diagnostics diag;
    ak4522_event mute = {100, AK4522_EVENT_SOFT_MUTE, 1};
    ak4522_event two[2] = {{1, AK4522_EVENT_SOFT_MUTE, 1}, {2, AK4522_EVENT_SOFT_MUTE, 0}};
    ak4522_event bad = {1, 7, 1};
    ak4522_host* host = ak4522_host_create();
    size_t n;
    double tail = 0.0;

    ak4522_host_config_default(&config);
    CHECK(config.host_rate == 48000.0);
    CHECK(config.dac.dac_rate == 44100.0);
    CHECK(config.dac.initial_deemphasis == AK4522_DEEMPHASIS_OFF);

    config.host_rate = 32000.0;
    CHECK(ak4522_host_prepare(host, &config) == AK4522_UNSUPPORTED_HOST_RATE);
    config.host_rate = 48000.0;
    config.dac.profile = 9;
    CHECK(ak4522_host_prepare(host, &config) == AK4522_INVALID_CONFIGURATION);
    config.dac.profile = AK4522_PROFILE_SPECIFICATION;
    config.max_events_per_call = 1;
    CHECK(ak4522_host_prepare(host, &config) == AK4522_OK);

    for (n = 0; n < frames; ++n) {
        in_l[n] = in_r[n] = (float)(0.5 * sin(2.0 * pi * 1000.0 * (double)n / 48000.0));
    }
    CHECK(ak4522_host_process_f32(host, in_l, in_r, out_l, out_r, block, two, 2) == AK4522_TOO_MANY_EVENTS);
    CHECK(ak4522_host_process_f32(host, in_l, in_r, out_l, out_r, block, &bad, 1) == AK4522_INVALID_ARGUMENT);
    for (n = 0; n < frames; n += block) {
        CHECK(ak4522_host_process_f32(host, in_l + n, in_r + n, out_l + n, out_r + n, block,
                                      n == 0 ? &mute : NULL, n == 0 ? 1 : 0)
              == AK4522_OK);
    }
    CHECK(ak4522_host_latency(host, &latency) == AK4522_OK);
    CHECK(latency.reported_host_frames > 0);
    CHECK(!latency.outside_documented_rate);
    /* Muted after 1024 DAC frames plus the latency: silent at the end. */
    for (n = frames - 500; n < frames; ++n) {
        tail = fmax(tail, fabs((double)out_l[n]));
    }
    CHECK(tail == 0.0);
    CHECK(ak4522_host_diagnostics(host, &diag) == AK4522_OK);
    CHECK(diag.host_underruns == 0);
    CHECK(ak4522_host_reset(host) == AK4522_OK);
    ak4522_host_destroy(host);
}

static void test_pull_stream(void)
{
    enum { frames = 9600 };
    static float out_l[frames], out_r[frames];
    ak4522_stream_config config;
    ak4522_latency latency;
    ak4522_diagnostics diag;
    sine_source source = {1000.0, 112000000.0 / 2304.0, 0, 1};
    ak4522_stream* stream = ak4522_stream_create();
    size_t n;
    double peak = 0.0;

    ak4522_stream_config_default(&config);
    config.dac_rate_numerator = 112000000;
    config.dac_rate_denominator = 2304;
    config.format = AK4522_FORMAT_FLOAT32;
    config.pull = pull_sine;
    config.pull_context = &source;
    CHECK(ak4522_stream_prepare(stream, &config) == AK4522_INVALID_DAC_RATE);
    config.dac.allow_extended_rate = true;
    CHECK(ak4522_stream_prepare(stream, &config) == AK4522_OK);
    CHECK(ak4522_stream_latency(stream, &latency) == AK4522_OK);
    CHECK(latency.outside_documented_rate);

    CHECK(ak4522_stream_render_f32(stream, out_l, out_r, 1000) == AK4522_OK);
    for (n = 300; n < 1000; ++n) {
        peak = fmax(peak, fabs((double)out_l[n]));
    }
    CHECK(fabs(peak - 0.5) < 0.02);
    CHECK(ak4522_stream_render_f32(stream, out_l, out_r, frames) == AK4522_OK);
    CHECK(fabs((double)out_l[frames - 1]) == 0.0); /* the source's mute took effect */
    CHECK(ak4522_stream_diagnostics(stream, &diag) == AK4522_OK);
    CHECK(diag.dropped_events == 0);
    ak4522_stream_destroy(stream);
}

static void test_push_stream(void)
{
    enum { frames = 2000 };
    static int32_t data[frames];
    static double out_l[frames], out_r[frames];
    float wrong[4] = {0};
    ak4522_stream_config config;
    ak4522_diagnostics diag;
    ak4522_stream* stream = ak4522_stream_create();
    size_t accepted = 0;
    size_t need;
    size_t n;

    ak4522_stream_config_default(&config);
    config.mode = AK4522_MODE_PUSH;
    config.push_capacity_frames = 4096;
    CHECK(ak4522_stream_prepare(stream, &config) == AK4522_OK);
    for (n = 0; n < frames; ++n) {
        data[n] = (int32_t)(200000.0 * sin(2.0 * pi * 500.0 * (double)n / 44100.0));
    }
    CHECK(ak4522_stream_push_f32(stream, wrong, wrong, 4, NULL, 0, &accepted) == AK4522_FORMAT_MISMATCH);
    need = ak4522_stream_dac_frames_for(stream, 1000);
    CHECK(need > 900 && need < 1000);
    CHECK(ak4522_stream_push_i32(stream, data, data, need, NULL, 0, &accepted) == AK4522_OK);
    CHECK(accepted == need);
    CHECK(ak4522_stream_queued_frames(stream) == need);
    CHECK(ak4522_stream_free_frames(stream) == 4096 - need);
    CHECK(ak4522_stream_render_f64(stream, out_l, out_r, 1000) == AK4522_OK);
    CHECK(ak4522_stream_queued_frames(stream) == 0);
    CHECK(ak4522_stream_diagnostics(stream, &diag) == AK4522_OK);
    CHECK(diag.source_underrun_frames == 0);
    CHECK(ak4522_stream_reset(stream) == AK4522_OK);
    ak4522_stream_destroy(stream);
}

static void test_push_accounting(void)
{
    static int32_t data[100];
    static float out_l[500], out_r[500];
    ak4522_stream_config config;
    ak4522_diagnostics diag;
    ak4522_stream* stream = ak4522_stream_create();
    size_t accepted = 0;
    size_t need;

    ak4522_stream_config_default(&config);
    config.mode = AK4522_MODE_PUSH;
    config.push_capacity_frames = 64;
    CHECK(ak4522_stream_prepare(stream, &config) == AK4522_OK);
    need = ak4522_stream_dac_frames_for(stream, 500);
    CHECK(ak4522_stream_render_f32(stream, out_l, out_r, 500) == AK4522_OK);
    CHECK(ak4522_stream_push_i32(stream, data, data, 100, NULL, 0, &accepted) == AK4522_OK);
    CHECK(accepted == 64);
    CHECK(ak4522_stream_diagnostics(stream, &diag) == AK4522_OK);
    CHECK(diag.source_underrun_frames == need);
    CHECK(diag.source_overrun_frames == 36);
    ak4522_stream_destroy(stream);
}

/* Mode 2 (left justified, 20 bits) at 64fs: each half-frame of 32 bit clocks
 * starts with the 20-bit word, MSB first; LRCK high is left. */
static void send_half(ak4522_serial_decoder* d, bool lrck, int32_t code, int32_t* events, int32_t* left,
                      int32_t* right)
{
    int slot;
    for (slot = 0; slot < 32; ++slot) {
        bool bit = slot < 20 && (((uint32_t)code >> (19 - slot)) & 1u) != 0;
        int32_t event = -1;
        CHECK(ak4522_serial_decoder_sclk_rising_edge(d, lrck, bit, &event, left, right) == AK4522_OK);
        if (event == AK4522_SERIAL_FRAME_COMPLETE) {
            ++*events;
        }
    }
}

static void test_serial(void)
{
    ak4522_serial_decoder* d = ak4522_serial_decoder_create();
    int32_t frames = 0, left = 0, right = 0, event = 0;
    CHECK(ak4522_serial_decoder_prepare(d, 9) == AK4522_INVALID_CONFIGURATION);
    CHECK(ak4522_serial_decoder_prepare(d, AK4522_SERIAL_MODE2_LEFT_JUSTIFIED_20) == AK4522_OK);
    send_half(d, false, 0, &frames, &left, &right);         /* partial start, discarded */
    send_half(d, true, 0x12345, &frames, &left, &right);    /* left */
    send_half(d, false, -300000 & 0xFFFFF, &frames, &left, &right); /* right */
    send_half(d, true, 0, &frames, &left, &right);          /* the edge completes the frame */
    CHECK(frames == 1);
    CHECK(left == 0x12345);
    CHECK(right == -300000);
    CHECK(ak4522_serial_decoder_sclk_rising_edge(d, true, false, NULL, NULL, NULL) == AK4522_INVALID_ARGUMENT);
    CHECK(ak4522_serial_decoder_reset(d) == AK4522_OK);
    CHECK(ak4522_serial_decoder_sclk_rising_edge(d, true, false, &event, NULL, NULL) == AK4522_OK);
    CHECK(event == AK4522_SERIAL_NONE);
    ak4522_serial_decoder_destroy(d);
}

static void test_timing(void)
{
    ak4522_timing_validator* v = ak4522_timing_validator_create();
    ak4522_pin_edge edge = {0.0, AK4522_PIN_SCLK, true};
    ak4522_pin_edge bad = {0.0, 9, true};
    CHECK(ak4522_timing_validator_prepare(v, AK4522_CMODE_LOW) == AK4522_OK);
    CHECK(ak4522_timing_validator_observe(v, &edge) == AK4522_OK);
    edge.time_ns = 10.0;
    edge.level = false;
    CHECK(ak4522_timing_validator_observe(v, &edge) == AK4522_OK); /* 10 ns high: too short */
    CHECK(ak4522_timing_validator_violations(v, AK4522_TIMING_SCLK_PULSE_WIDTH) == 1);
    CHECK(ak4522_timing_validator_advance_to(v, 100000.0) == AK4522_OK); /* SCLK stopped */
    CHECK(ak4522_timing_validator_violations(v, AK4522_TIMING_CLOCK_STOPPED) == 1);
    CHECK(ak4522_timing_validator_total_violations(v) == 2);
    CHECK(ak4522_timing_validator_violations(v, AK4522_TIMING_KINDS) == 0);
    CHECK(ak4522_timing_validator_observe(v, &bad) == AK4522_INVALID_ARGUMENT);
    CHECK(ak4522_timing_validator_reset(v) == AK4522_OK);
    CHECK(ak4522_timing_validator_total_violations(v) == 0);
    ak4522_timing_validator_destroy(v);
}

static void test_circuit(void)
{
    enum { frames = 48000 };
    static double l[frames], r[frames], x[frames];
    ak4522_circuit_options options;
    ak4522_circuit* c = ak4522_circuit_create();
    double worst = 0.0;
    size_t n;
    ak4522_circuit_options_default(&options);
    CHECK(options.include_gain && options.invert && options.load_ohms == 0.0);
    CHECK(ak4522_circuit_process_f64(c, l, r, 4) == AK4522_NOT_PREPARED);
    CHECK(ak4522_circuit_prepare(c, 48000.0, NULL) == AK4522_INVALID_ARGUMENT);
    options.load_ohms = -1.0;
    CHECK(ak4522_circuit_prepare(c, 48000.0, &options) == AK4522_INVALID_CONFIGURATION);
    options.load_ohms = 0.0;
    CHECK(ak4522_circuit_prepare(c, 48000.0, &options) == AK4522_OK);
    for (n = 0; n < frames; ++n) {
        x[n] = l[n] = r[n] = 0.5 * sin(2.0 * pi * 1000.0 * (double)n / 48000.0);
    }
    CHECK(ak4522_circuit_process_f64(c, l, r, frames) == AK4522_OK);
    for (n = frames / 2; n < frames; ++n) {
        worst = fmax(worst, fabs(l[n] + 1.8 * x[n])); /* gain -1.8 at 1 kHz */
    }
    CHECK(worst < 0.002);
    CHECK(ak4522_circuit_reset(c) == AK4522_OK);
    ak4522_circuit_destroy(c);
}

int main(void)
{
    test_library();
    test_null_arguments();
    test_host();
    test_pull_stream();
    test_push_stream();
    test_push_accounting();
    test_serial();
    test_timing();
    test_circuit();
    if (failures != 0) {
        fprintf(stderr, "%d C API check(s) failed\n", failures);
        return 1;
    }
    printf("C API: all checks passed\n");
    return 0;
}
