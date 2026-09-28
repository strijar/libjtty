#include "internal.h"
#include <math.h>
#include <stdlib.h>

#define HOP 48
#define DF  7.8125

/* Sliding rectangular DFT: O(samples * frequency_bins), independent of
 * symbol-window length. Only 4 ms energy snapshots are retained. */

int jt_find_candidates(const float *audio, size_t count, double lo, double hi, jt_candidate *out, int capacity) {
    size_t rows = (count - JTTY_SPS) / HOP + 1;
    size_t starts = (count - JTTY_FRAME_SAMPLES) / HOP + 1;
    int    bases = (int) floor((hi - lo) / DF) + 1, bins = bases + 12;

    if (rows > SIZE_MAX / (size_t) bins / sizeof(float) || starts > SIZE_MAX / (size_t) bases / sizeof(float))
        return -1;

    float *energy = calloc(rows * (size_t) bins, sizeof(*energy));
    float *surface = calloc(starts * (size_t) bases, sizeof(*surface));

    if (!energy || !surface) {
        free(energy);
        free(surface);
        return -1;
    }

    for (int b = 0; b < bins; b++) {
        double w = 2 * JT_PI * (lo + b * DF) / 12000, wr = cos(w), wi = -sin(w);
        double dr = cos(w * JTTY_SPS), di = sin(w * JTTY_SPS), zr = 1, zi = 0, re = 0, im = 0;

        for (size_t i = 0; i < count; i++) {
            double xr = audio[i] * zr, xi = audio[i] * zi;

            if (i >= JTTY_SPS) {
                double old = audio[i - JTTY_SPS];
                xr -= old * (zr * dr - zi * di);
                xi -= old * (zr * di + zi * dr);
            }

            re += xr;
            im += xi;

            if (i + 1 >= JTTY_SPS && (i + 1 - JTTY_SPS) % HOP == 0) {
                size_t row = (i + 1 - JTTY_SPS) / HOP;
                energy[row * (size_t) bins + (size_t) b] = (float) ((re * re + im * im) / (JTTY_SPS * JTTY_SPS));
            }

            double r = zr * wr - zi * wi;

            zi = zr * wi + zi * wr;
            zr = r;

            if ((i & 4095) == 4095) {
                double norm = hypot(zr, zi);

                zr /= norm;
                zi /= norm;
            }
        }
    }

    for (size_t t = 0; t < starts; t++)
        for (int b = 0; b < bases; b++) {
            double score = 0;

            for (int s = 0; s < 13; s++) {
                const float *e = energy + (t + (size_t) s * 8) * (size_t) bins + (size_t) b;
                double       sum = (double) e[0] + e[4] + e[8] + e[12];

                if (sum > 1e-25)
                    score += e[4 * jt_sync[s]] / sum;
            }
            surface[t * (size_t) bases + (size_t) b] = (float) (score / 13);
        }

    free(energy);
    int found = 0;

    /* Greedy peak extraction with a compact exclusion region. */

    while (found < capacity) {
        float  best = 0.46f;
        size_t index = 0;
        int    have = 0;

        for (size_t i = 0; i < starts * (size_t) bases; i++)
            if (surface[i] > best) {
                best = surface[i];
                index = i;
                have = 1;
            }

        if (!have)
            break;

        size_t t = index / (size_t) bases;
        int    b = (int) (index % (size_t) bases);

        out[found++] = (jt_candidate) { t * HOP, lo + b * DF, best };
        size_t low = t > 12 ? t - 12 : 0, high = t + 12 < starts ? t + 12 : starts - 1;

        for (size_t r = low; r <= high; r++)
            for (int f = b - 2; f <= b + 2; f++)
                if (f >= 0 && f < bases)
                    surface[r * (size_t) bases + (size_t) f] = 0;
    }

    free(surface);
    return found;
}
