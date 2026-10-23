#include "internal.h"
#include <math.h>
#include <string.h>

void jt_sync_init(jtty_rx *rx) {
    memset(rx->energy, 0, JT_ENERGY_ROWS * rx->bins * sizeof(float));
    rx->audio_at = 0;
    rx->until_hop = JTTY_SPS;
    rx->energy_row = 0;
    rx->rows = 0;

    for (size_t b = 0; b < rx->bins; b++) {
        double w = 2 * JT_PI * (rx->config.fmin + (double) b * JT_DF) / JTTY_SAMPLE_RATE;
        rx->bank[b] = (jt_bin) { cos(w), -sin(w), cos(w * JTTY_SPS), sin(w * JTTY_SPS), 1, 0, 0, 0 };
    }
}

static int nearby(const jt_pending *a, const jt_pending *b) {
    uint64_t dt = a->sample > b->sample ? a->sample - b->sample : b->sample - a->sample;

    return dt <= 12 * JT_HOP && fabs(a->frequency - b->frequency) <= 2 * JT_DF;
}

/* Online nonmaximum suppression. Retain stronger hypotheses when full.
 * All decisions depend on sample positions, never on process boundaries. */
static void candidate(jtty_rx *rx, jt_pending c) {
    for (size_t i = 0; i < rx->pending_count; i++)
        if (nearby(&c, &rx->pending[i]) && rx->pending[i].score >= c.score)
            return;

    for (size_t i = 0; i < rx->pending_count;)
        if (nearby(&c, &rx->pending[i]))
            rx->pending[i] = rx->pending[--rx->pending_count];
        else
            i++;

    if (rx->pending_count < rx->config.candidate_capacity) {
        rx->pending[rx->pending_count++] = c;
        return;
    }

    size_t weakest = 0;

    for (size_t i = 1; i < rx->pending_count; i++)
        if (rx->pending[i].score < rx->pending[weakest].score)
            weakest = i;

    if (c.score > rx->pending[weakest].score)
        rx->pending[weakest] = c;

    rx->stats.candidates_dropped++;
}

/* The ring position, the next hop and the newest energy row are counted as
 * samples arrive and wrap by subtraction. A 64-bit remainder per sample is a
 * library call on 32-bit ARM and was a fifth of a Pi Zero 2 W's time. */
void jt_sync_push(jtty_rx *rx, float sample) {
    uint64_t n = rx->stats.samples;
    size_t   old_at = rx->audio_at >= JTTY_SPS ? rx->audio_at - JTTY_SPS : rx->audio_at + JT_AUDIO_SIZE - JTTY_SPS;
    double   old = n >= JTTY_SPS ? rx->audio[old_at] : 0;

    rx->audio[rx->audio_at] = sample;

    if (++rx->audio_at == JT_AUDIO_SIZE)
        rx->audio_at = 0;

    int snapshot = --rx->until_hop == 0;

    if (snapshot)
        rx->until_hop = JT_HOP;

    float *energy = rx->energy + rx->energy_row * rx->bins;

    for (size_t b = 0; b < rx->bins; b++) {
        jt_bin *v = &rx->bank[b];
        v->re += sample * v->zr - old * (v->zr * v->dr - v->zi * v->di);
        v->im += sample * v->zi - old * (v->zr * v->di + v->zi * v->dr);

        /* Periodic direct reconstruction bounds accumulated cancellation error
         * during indefinitely long streams, including silence after a signal. */
        if ((n & 65535) == 65535) {
            double zr = v->zr, zi = v->zi;

            v->re = v->im = 0;

            size_t at = rx->audio_at;

            for (size_t k = 0; k < JTTY_SPS; k++) {
                at = at ? at - 1 : JT_AUDIO_SIZE - 1;

                double x = rx->audio[at];

                v->re += x * zr;
                v->im += x * zi;

                double r = zr * v->wr + zi * v->wi;

                zi = zi * v->wr - zr * v->wi;
                zr = r;
            }
        }

        if (snapshot)
            energy[b] = (float) ((v->re * v->re + v->im * v->im) / (JTTY_SPS * JTTY_SPS));

        double r = v->zr * v->wr - v->zi * v->wi;
        v->zi = v->zr * v->wi + v->zi * v->wr;
        v->zr = r;

        if ((n & 4095) == 4095) {
            double norm = hypot(v->zr, v->zi);
            v->zr /= norm;
            v->zi /= norm;
        }
    }

    rx->stats.samples++;

    if (!snapshot)
        return;

    uint64_t row = rx->rows++;

    if (++rx->energy_row == JT_ENERGY_ROWS)
        rx->energy_row = 0;

    if (row < JT_ENERGY_ROWS - 1)
        return;

    /* The oldest row is the one the next snapshot overwrites. The thirteen
     * rows a score reads are found once per hop rather than once per bin,
     * and the score is single precision: double division is slow on small
     * ARM cores. */
    uint64_t     start = row - (JT_ENERGY_ROWS - 1);
    const float *rows[13];

    for (size_t s = 0; s < 13; s++) {
        size_t r = rx->energy_row + s * 8;

        rows[s] = rx->energy + (r >= JT_ENERGY_ROWS ? r - JT_ENERGY_ROWS : r) * rx->bins;
    }

    for (size_t b = 0; b < rx->bases; b++) {
        float score = 0;

        for (size_t s = 0; s < 13; s++) {
            const float *e = rows[s] + b;
            float        sum = e[0] + e[4] + e[8] + e[12];

            if (sum > 1e-25f)
                score += e[4 * jt_sync[s]] / sum;
        }

        score /= 13;

        if (score > 0.46)
            candidate(rx, (jt_pending) { start * JT_HOP, rx->config.fmin + (double) b * JT_DF, score });
    }
}
