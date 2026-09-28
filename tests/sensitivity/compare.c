#include "jtty.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

extern void     wsjtx_wave(int64_t payload, float frequency, float *wave);
extern void     wsjtx_receive(const int16_t *pcm, int count, const char *expected, int *correct, int *wrong);
static uint32_t state;

static double uniform(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return ((double) state + 0.5) / 4294967296.0;
}

static double gaussian(void) {
    return sqrt(-2 * log(uniform())) * cos(6.283185307179586 * uniform());
}

typedef struct {
    uint64_t expected;
    int      correct, wrong;
} result;

static void collect(const jtty_frame *frame, void *context) {
    result *r = context;

    if (frame->payload == r->expected)
        r->correct = 1;
    else
        r->wrong++;
}

int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: compare TRIALS SNR_START SNR_STOP SEED [--noise]\n");
        return 2;
    }

    int noise_only = argc == 6;

    if (noise_only && strcmp(argv[5], "--noise"))
        return 2;

    int      trials = atoi(argv[1]), start = atoi(argv[2]), stop = atoi(argv[3]);
    uint32_t seed = (uint32_t) strtoul(argv[4], NULL, 0);

    if (trials < 1 || trials > 100000 || start < stop || stop < -40 || start > 20 || !seed)
        return 2;

    const int count = 2 * JTTY_FRAME_SAMPLES;
    float    *wave = malloc(JTTY_FRAME_SAMPLES * sizeof(*wave)), *audio = malloc((size_t) count * sizeof(*audio));
    int16_t  *pcm = malloc((size_t) count * sizeof(*pcm));

    if (!wave || !audio || !pcm)
        return 1;

    puts("snr_db,trials,c_correct,wsjtx_correct,c_wrong,wsjtx_wrong,both,c_only,wsjtx_only,c_seconds,wsjtx_seconds");

    for (int snr = start; snr >= stop; snr--) {
        int    correct = 0, reference = 0, wrong = 0, rwrong = 0, both = 0, onlyc = 0, onlyr = 0;
        double ctime = 0, rtime = 0;

        state = seed; /* Paired data also across SNR: waveform and noise realization unchanged. */
        double sigma = sqrt(1.2 / pow(10, snr / 10.0));

        for (int trial = 0; trial < trials; trial++) {
            jtty_atom a = { 0 };

            if (trial % 2) {
                static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
                a.kind = JTTY_TEXT5;
                for (int j = 0; j < 5; j++)
                    a.text[j] = alphabet[(int) (uniform() * 36)];
            } else {
                a.kind = JTTY_NUM;
                a.subtype = JTTY_SERIAL;
                a.role = JTTY_FULL_EXCHANGE;
                a.value = (int) (uniform() * 131072);
            }

            uint64_t payload;
            char     text[81] = { 0 };

            if (jtty_atom_pack(&a, 1, &payload) || jtty_atom_render(&a, text))
                return 1;

            int   delay = 120 + (int) (uniform() * 5300);
            float frequency = (float) (965 + uniform() * 70);

            wsjtx_wave((int64_t) payload, frequency, wave);

            double energy = 0;

            for (int j = 0; j < JTTY_FRAME_SAMPLES; j++)
                energy += (double) wave[j] * wave[j];

            double rms_factor = sqrt(0.5 / (energy / JTTY_FRAME_SAMPLES));

            for (int j = 0; j < count; j++) {
                double signal = j >= delay && j < delay + JTTY_FRAME_SAMPLES ? wave[j - delay] * rms_factor : 0;

                if (noise_only)
                    signal = 0;

                /* Noise RMS 1000 PCM units at every SNR, no clipping or AGC differences. */
                long value = lrint(1000 * (signal / sigma + gaussian()));

                if (value < -32768 || value > 32767) {
                    fputs("PCM clipping\n", stderr);
                    return 1;
                }

                pcm[j] = (int16_t) value;
                audio[j] = (float) value / 32768;
            }

            result  got = { payload, 0, 0 };
            clock_t t = clock();

            jtty_rx_config config = { 950, 1050, 128, 128 };
            jtty_rx       *rx = jtty_rx_create(&config);
            if (!rx || jtty_rx_process(rx, audio, (size_t) count, collect, &got) < 0 ||
                jtty_rx_flush(rx, collect, &got) < 0)
                return 1;

            jtty_rx_destroy(rx);
            ctime += (double) (clock() - t) / CLOCKS_PER_SEC;

            int hit = 0, other = 0;
            t = clock();

            wsjtx_receive(pcm, count, text, &hit, &other);

            rtime += (double) (clock() - t) / CLOCKS_PER_SEC;
            correct += got.correct;
            reference += hit;
            wrong += got.wrong;
            rwrong += other;
            both += got.correct && hit;
            onlyc += got.correct && !hit;
            onlyr += !got.correct && hit;
        }

        if (noise_only)
            fputs("noise", stdout);
        else
            printf("%d", snr);

        printf(",%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f\n", trials, correct, reference, wrong, rwrong, both, onlyc, onlyr, ctime, rtime);
        fflush(stdout);
    }

    free(wave);
    free(audio);
    free(pcm);

    return 0;
}
