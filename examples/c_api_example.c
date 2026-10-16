/* The host processor from C: a 1 kHz tone at 48 kHz through the emulated DAC
 * at 44.1 kHz. Prints the latency and the output peak.
 *
 *     ak4522_c_api_example
 */

#include <ak4522/ak4522.h>

#include <math.h>
#include <stdio.h>

int main(void)
{
    enum { frames = 4800, block = 480 };
    static float in[frames], out_l[frames], out_r[frames];
    ak4522_host_config config;
    ak4522_latency latency;
    ak4522_status status;
    ak4522_host* host;
    double peak = 0.0;
    size_t n;

    for (n = 0; n < frames; ++n) {
        in[n] = (float)(0.5 * sin(2.0 * 3.14159265358979323846 * 1000.0 * (double)n / 48000.0));
    }

    host = ak4522_host_create();
    if (host == NULL) {
        return 1;
    }
    ak4522_host_config_default(&config); /* 48 kHz host, 44.1 kHz DAC */
    status = ak4522_host_prepare(host, &config);
    if (status != AK4522_OK) {
        fprintf(stderr, "prepare failed: %s\n", ak4522_status_string(status));
        ak4522_host_destroy(host);
        return 1;
    }
    for (n = 0; n < frames; n += block) {
        status = ak4522_host_process_f32(host, in + n, in + n, out_l + n, out_r + n, block, NULL, 0);
        if (status != AK4522_OK) {
            fprintf(stderr, "process failed: %s\n", ak4522_status_string(status));
            ak4522_host_destroy(host);
            return 1;
        }
    }
    ak4522_host_latency(host, &latency);
    for (n = frames / 2; n < frames; ++n) {
        peak = fmax(peak, fabs((double)out_l[n]));
    }
    printf("ak4522 %s: latency %u host frames, output peak %.3f\n", ak4522_version(),
           (unsigned)latency.reported_host_frames, peak);
    ak4522_host_destroy(host);
    return 0;
}
