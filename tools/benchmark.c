#include "jtty.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Reproducible synthetic AWGN experiment; not a WSJT-X sensitivity claim. */
static uint32_t state = 0x79023abc;

static double uniform(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return ((double) state + 0.5) / 4294967296.0;
}

static double normal(void) {
    return sqrt(-2 * log(uniform())) * cos(6.283185307179586 * uniform());
}

typedef struct {
    uint64_t expected;
    int      correct, wrong;
} result;

static void record(const jtty_frame *frame, void *user) {
    result *r = user;

    if (frame->payload == r->expected)
        r->correct = 1;
    else
        r->wrong++;
}

int main(int argc, char **argv) {
    int trials = 20;

    if (argc > 2)
        return 2;

    if (argc == 2) {
        char *end;
        long  n = strtol(argv[1], &end, 10);

        if (*end || n < 1 || n > 10000)
            return 2;

        trials = (int) n;
    }

    jtty_rx_config config = { 950, 1050, 128, 128 };
    jtty_rx       *rx = jtty_rx_create(&config);
    if (!rx)
        return 1;
    puts("SNR_2500_dB,trials,correct_frames,false_frames");

    for (int snr = -4; snr >= -16; snr -= 2) {
        int    correct = 0, wrong = 0;
        double sigma = sqrt(1.2 / pow(10, (double) snr / 10));

        for (int i = 0; i < trials; i++) {
            size_t count = JTTY_FRAME_SAMPLES + 2000, delay = 400 + (size_t) (uniform() * 1200);
            float *audio = calloc(count, sizeof(*audio));

            if (!audio)
                return 1;

            jtty_atom a = { 0 };
            a.kind = JTTY_NUM;
            a.subtype = JTTY_SERIAL;
            a.role = JTTY_FULL_EXCHANGE;
            a.value = (int) (uniform() * 131072);
            uint64_t p;
            uint8_t  tones[59];

            if (jtty_atom_pack(&a, 1, &p) || jtty_encode(p, tones) || jtty_modulate(tones, 59, 970 + uniform() * 60, audio + delay, count - delay))
                return 1;

            for (size_t j = 0; j < count; j++)
                audio[j] += (float) (sigma * normal());

            result r = { p, 0, 0 };

            jtty_rx_reset(rx);
            if (jtty_rx_process(rx, audio, count, record, &r) < 0 ||
                jtty_rx_flush(rx, record, &r) < 0)
                return 1;

            correct += r.correct;
            wrong += r.wrong;
            free(audio);
        }
        printf("%d,%d,%d,%d\n", snr, trials, correct, wrong);
        fflush(stdout);
    }
    jtty_rx_destroy(rx);
    return 0;
}
