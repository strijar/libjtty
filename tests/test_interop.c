#include "jtty.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compare the original Fortran tones/waveform and receive the original audio. */
static uint64_t received;
static void     accept(const jtty_frame *frame, void *unused) {
    (void) unused;
    received = frame->payload;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    uint64_t p = strtoull(argv[1], NULL, 16);
    FILE    *f = fopen(argv[2], "r");
    assert(f);
    char text[64];
    assert(fgets(text, sizeof(text), f));
    fclose(f);
    uint8_t tones[59];
    assert(!jtty_encode(p, tones));
    for (int i = 0; i < 59; i++)
        assert(text[i] == '0' + tones[i]);
    float original[JTTY_FRAME_SAMPLES], local[JTTY_FRAME_SAMPLES];
    f = fopen(argv[3], "rb");
    assert(f);
    assert(fread(original, sizeof(float), JTTY_FRAME_SAMPLES, f) == JTTY_FRAME_SAMPLES);
    fclose(f);
    assert(!jtty_modulate(tones, 59, 1000, local, JTTY_FRAME_SAMPLES));
    double error = 0, power = 0;
    for (size_t i = 0; i < JTTY_FRAME_SAMPLES; i++) {
        double d = original[i] - local[i];
        error += d * d;
        power += (double) original[i] * original[i];
    }
    double relative = sqrt(error / power);
    printf("%s: identical tones; relative waveform RMS error %.6g\n", argv[1], relative);
    assert(relative < 0.005);
    jtty_rx_config config = { 980, 1020, 128, 128 };
    jtty_rx       *rx = jtty_rx_create(&config);
    assert(rx);
    int got = jtty_rx_process(rx, original, JTTY_FRAME_SAMPLES, accept, NULL);
    assert(got >= 0);
    assert(got + jtty_rx_flush(rx, accept, NULL) == 1);
    jtty_rx_destroy(rx);
    assert(received == p);
    return 0;
}
