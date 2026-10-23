#include "internal.h"
#include <math.h>
#include <string.h>

/* The energies of all bins at a hop are |DFT|^2 of the last JTTY_SPS samples
 * zero-padded to JT_DFT_SIZE, whose bins are JT_DF apart. They were kept by a
 * sliding DFT updated for every bin at every sample; computing them once per
 * hop is the same energy for a fraction of the work. The samples are first
 * shifted down by the fraction of a bin that fmin lies above the DFT's
 * grid, so that bin b is at fmin + b * JT_DF for any fmin. */
void jt_sync_init(jtty_rx *rx) {
    memset(rx->energy, 0, JT_ENERGY_ROWS * rx->bins * sizeof(float));
    rx->audio_at = 0;
    rx->until_hop = JTTY_SPS;
    rx->energy_row = 0;
    rx->rows = 0;

    rx->first_bin = (size_t) floor(rx->config.fmin / JT_DF);

    double fraction = rx->config.fmin - (double) rx->first_bin * JT_DF;

    for (size_t k = 0; k < JT_DFT_SIZE; k++) {
        rx->cos_dft[k] = (float) cos(2 * JT_PI * (double) k / JT_DFT_SIZE);
        rx->sin_dft[k] = (float) -sin(2 * JT_PI * (double) k / JT_DFT_SIZE);
    }

    for (size_t n = 0; n < JTTY_SPS; n++) {
        rx->shift_re[n] = (float) cos(2 * JT_PI * fraction * (double) n / JTTY_SAMPLE_RATE);
        rx->shift_im[n] = (float) -sin(2 * JT_PI * fraction * (double) n / JTTY_SAMPLE_RATE);
    }
}

/* In-place radix-2 FFT of JT_FFT_SIZE points, e^{-i2pi nk/N}; its twiddles
 * are every JT_DFT_PARTS-th of the DFT's. */
static void fft(const jtty_rx *rx, float *re, float *im) {
    for (size_t i = 1, j = 0; i < JT_FFT_SIZE; i++) {
        size_t bit = JT_FFT_SIZE >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;

        j |= bit;

        if (i < j) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }

    for (size_t length = 2; length <= JT_FFT_SIZE; length <<= 1) {
        size_t half = length / 2, step = JT_DFT_SIZE / length;

        for (size_t i = 0; i < JT_FFT_SIZE; i += length)
            for (size_t k = 0; k < half; k++) {
                float  wr = rx->cos_dft[k * step], wi = rx->sin_dft[k * step];
                size_t a = i + k, b = a + half;
                float  tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;

                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
    }
}

/* The DFT of the symbol is split by n = JT_DFT_PARTS m + p into three FFTs,
 * and bin k is their sum, each turned by e^{-i2pi pk/JT_DFT_SIZE}. */
static void hop_energies(jtty_rx *rx, float *energy) {
    size_t at = rx->audio_at >= JTTY_SPS ? rx->audio_at - JTTY_SPS : rx->audio_at + JT_AUDIO_SIZE - JTTY_SPS;

    memset(rx->fft_re, 0, sizeof(rx->fft_re));
    memset(rx->fft_im, 0, sizeof(rx->fft_im));

    for (size_t n = 0, p = 0, m = 0; n < JTTY_SPS; n++) {
        float x = rx->audio[at];

        rx->fft_re[p][m] = x * rx->shift_re[n];
        rx->fft_im[p][m] = x * rx->shift_im[n];

        if (++at == JT_AUDIO_SIZE)
            at = 0;

        if (++p == JT_DFT_PARTS) {
            p = 0;
            m++;
        }
    }

    for (size_t p = 0; p < JT_DFT_PARTS; p++)
        fft(rx, rx->fft_re[p], rx->fft_im[p]);

    /* fmax + 93.75 < 6000 keeps k below half the DFT, so p * k stays
     * within its table */
    for (size_t b = 0; b < rx->bins; b++) {
        size_t k = rx->first_bin + b, m = k & (JT_FFT_SIZE - 1);
        float  re = 0, im = 0;

        for (size_t p = 0; p < JT_DFT_PARTS; p++) {
            size_t t = p * k;
            float  wr = rx->cos_dft[t], wi = rx->sin_dft[t];

            re += rx->fft_re[p][m] * wr - rx->fft_im[p][m] * wi;
            im += rx->fft_re[p][m] * wi + rx->fft_im[p][m] * wr;
        }

        energy[b] = (re * re + im * im) / (JTTY_SPS * JTTY_SPS);
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
    rx->audio[rx->audio_at] = sample;

    if (++rx->audio_at == JT_AUDIO_SIZE)
        rx->audio_at = 0;

    int snapshot = --rx->until_hop == 0;

    if (snapshot) {
        rx->until_hop = JT_HOP;
        hop_energies(rx, rx->energy + rx->energy_row * rx->bins);
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
