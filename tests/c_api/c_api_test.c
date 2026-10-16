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

int main(void)
{
    test_library();
    test_null_arguments();
    test_host();
    test_pull_stream();
    test_push_stream();
    test_push_accounting();
    if (failures != 0) {
        fprintf(stderr, "%d C API check(s) failed\n", failures);
        return 1;
    }
    printf("C API: all checks passed\n");
    return 0;
}
