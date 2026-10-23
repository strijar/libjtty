#include "internal.h"
#include <math.h>

/* Goertzel's recursion gives each tone's energy with one multiply per sample.
 * The four tones are independent, so they run side by side: as one vector
 * where the compiler has vector extensions, which NEON executes together on
 * an in-order core such as the Cortex-A53, and as four interleaved scalars
 * otherwise. This was most of a Pi Zero 2 W's receive time. */
#if defined(__GNUC__) || defined(__clang__)
typedef float jt_four __attribute__((vector_size(16)));

void jt_correlate(const float *audio, size_t start, double f0, float scores[4]) {
    jt_four c, s1 = { 0, 0, 0, 0 }, s2 = { 0, 0, 0, 0 };

    for (int k = 0; k < 4; k++)
        c[k] = (float) (2 * cos(2 * JT_PI * (f0 + k * 31.25) / 12000));

    for (size_t i = 0; i < JTTY_SPS; i++) {
        float   x = audio[start + i];
        jt_four s0 = (jt_four) { x, x, x, x } + c * s1 - s2;

        s2 = s1;
        s1 = s0;
    }

    jt_four power = s1 * s1 + s2 * s2 - c * s1 * s2;

    for (int k = 0; k < 4; k++)
        scores[k] = power[k] / (JTTY_SPS * JTTY_SPS);
}
#else
void jt_correlate(const float *audio, size_t start, double f0, float scores[4]) {
    float c[4], s1[4] = { 0, 0, 0, 0 }, s2[4] = { 0, 0, 0, 0 };

    for (int k = 0; k < 4; k++)
        c[k] = (float) (2 * cos(2 * JT_PI * (f0 + k * 31.25) / 12000));

    for (size_t i = 0; i < JTTY_SPS; i++) {
        float x = audio[start + i];

        for (int k = 0; k < 4; k++) {
            float s0 = x + c[k] * s1[k] - s2[k];

            s2[k] = s1[k];
            s1[k] = s0;
        }
    }

    for (int k = 0; k < 4; k++)
        scores[k] = (s1[k] * s1[k] + s2[k] * s2[k] - c[k] * s1[k] * s2[k]) / (JTTY_SPS * JTTY_SPS);
}
#endif

double jt_sync_score(const float *audio, size_t start, double f0) {
    double score = 0;

    for (size_t i = 0; i < 13; i++) {
        float e[4];
        jt_correlate(audio, start + i * JTTY_SPS, f0, e);
        double sum = (double) e[0] + e[1] + e[2] + e[3];
        if (sum > 1e-25)
            score += e[jt_sync[i]] / sum;
    }
    return score / 13;
}

void jt_refine(const float *audio, size_t count, jt_candidate *c) {
    /* Two small grids resolve the 4 ms / 7.8125 Hz coarse acquisition grid. */
    for (int pass = 0; pass < 2; pass++) {
        jt_candidate best = *c;
        int          dt = pass ? 6 : 24;
        double       df = pass ? 0.9765625 : 3.90625;

        for (int t = -2; t <= 2; t++)
            for (int f = -2; f <= 2; f++) {
                if (t < 0 && c->sample < (size_t) (-t * dt))
                    continue;

                size_t sample = t < 0 ? c->sample - (size_t) (-t * dt) : c->sample + (size_t) (t * dt);
                double frequency = c->frequency + f * df;

                if (sample > count - JTTY_FRAME_SAMPLES || frequency <= 0 || frequency + 93.75 >= 6000)
                    continue;

                double score = jt_sync_score(audio, sample, frequency);

                if (score > best.score) {
                    best.sample = sample;
                    best.frequency = frequency;
                    best.score = score;
                }
            }
        *c = best;
    }
}
