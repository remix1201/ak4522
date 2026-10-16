/* Uses the installed C API. */

#include <ak4522/ak4522.h>

#include <stdio.h>

int main(void)
{
    static float in[256], out_l[256], out_r[256];
    ak4522_host_config config;
    ak4522_host* host = ak4522_host_create();
    ak4522_latency latency;
    if (host == NULL) {
        return 1;
    }
    ak4522_host_config_default(&config);
    if (ak4522_host_prepare(host, &config) != AK4522_OK
        || ak4522_host_process_f32(host, in, in, out_l, out_r, 256, NULL, 0) != AK4522_OK
        || ak4522_host_latency(host, &latency) != AK4522_OK) {
        return 1;
    }
    printf("library %s, latency %u frames\n", ak4522_version(), (unsigned)latency.reported_host_frames);
    ak4522_host_destroy(host);
    printf("consumer ok\n");
    return 0;
}
