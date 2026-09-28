#include "internal.h"
#include <math.h>
#include <stdlib.h>

int jtty_modulate(const uint8_t *tones, size_t count, double f0, float *audio, size_t capacity) {
    if (!tones || !audio || !count || count > JTTY_MAX_FRAMES * JTTY_SYMBOLS ||
        count > capacity / JTTY_SPS || !isfinite(f0) || f0 <= 0 || f0 + 93.75 >= 6000)
        return -1;

    for (size_t i = 0; i < count; i++)
        if (tones[i] > 3)
            return -1;

    const size_t sps = JTTY_SPS, n = count * sps;
    double       pulse[3 * JTTY_SPS];
    const double c = JT_PI * sqrt(2 / log(2.0)) * 2; /* BT = 2 */

    for (size_t i = 0; i < 3 * sps; i++) {
        double t = ((double) i + 1 - 1.5 * (double) sps) / (double) sps;
        pulse[i] = 0.5 * (erf(c * (t + 0.5)) - erf(c * (t - 0.5)));
    }

    double *frequency = calloc(n + 2 * sps, sizeof(*frequency));

    if (!frequency)
        return -1;

    for (size_t j = 0; j < count; j++)
        for (size_t i = 0; i < 3 * sps; i++)
            frequency[j * sps + i] += tones[j] * pulse[i];

    for (size_t i = 0; i < 2 * sps; i++) {
        frequency[i] += tones[0] * pulse[i + sps];
        frequency[n + i] += tones[count - 1] * pulse[i];
    }

    double phase = 0;

    for (size_t i = 0; i < n; i++) {
        audio[i] = (float) sin(phase);
        phase = fmod(phase + 2 * JT_PI * (f0 / 12000 + frequency[i + sps] / (double) sps), 2 * JT_PI);
    }

    for (size_t i = 0; i < sps / 8; i++) {
        double x = JT_PI * (double) i / (double) (sps / 8);
        audio[i] *= (float) (0.5 * (1 - cos(x)));
        audio[n - sps / 8 + i] *= (float) (0.5 * (1 + cos(x)));
    }

    free(frequency);
    return 0;
}
