#include "wav.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t le32(const unsigned char *b) {
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
}

static unsigned le16(const unsigned char *b) {
    return (unsigned) b[0] | ((unsigned) b[1] << 8);
}

static int put32(FILE *f, uint32_t x) {
    unsigned char b[4] = { (unsigned char) x, (unsigned char) (x >> 8), (unsigned char) (x >> 16), (unsigned char) (x >> 24) };
    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

int wav_read(const char *path, float **audio, size_t *count) {
    *audio = NULL;
    *count = 0;
    FILE *f = fopen(path, "rb");

    if (!f)
        return -1;

    unsigned char h[12];
    int           ok = -1, format = 0;
    float        *samples = NULL;

    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        goto done;

    uint32_t remaining = le32(h + 4);

    if (remaining < 4)
        goto done;

    remaining -= 4;

    while (remaining >= 8) {
        if (fread(h, 1, 8, f) != 8)
            goto done;

        remaining -= 8;
        uint32_t n = le32(h + 4);

        if (n > remaining || (n & 1) > remaining - n)
            goto done;

        if (!memcmp(h, "fmt ", 4)) {
            unsigned char b[16];

            if (n < 16 || fread(b, 1, 16, f) != 16 || le16(b) != 1 || le16(b + 2) != 1 || le32(b + 4) != 12000 || le16(b + 12) != 2 || le16(b + 14) != 16)
                goto done;

            if (fseek(f, (long) (n - 16 + (n & 1)), SEEK_CUR))
                goto done;

            format = 1;
        } else if (!memcmp(h, "data", 4)) {
            if (!format || n % 2 || !n || n > 120u * 12000 * 2)
                goto done;

            samples = malloc((size_t) n / 2 * sizeof(*samples));

            if (!samples)
                goto done;

            for (uint32_t i = 0; i < n / 2; i++) {
                unsigned char b[2];

                if (fread(b, 1, 2, f) != 2)
                    goto done;

                unsigned u = le16(b);
                int      value = u < 32768 ? (int) u : (int) u - 65536;
                samples[i] = (float) value / 32768;
            }

            *count = n / 2;
            *audio = samples;
            samples = NULL;
            ok = 0;
            goto done;
        } else if (fseek(f, (long) (n + (n & 1)), SEEK_CUR))
            goto done;

        remaining -= n + (n & 1);
    }
done:
    free(samples);
    fclose(f);
    return ok;
}

int wav_write(const char *path, const float *audio, size_t count) {
    if (count > (UINT32_MAX - 36) / 2)
        return -1;

    FILE *f = fopen(path, "wb");

    if (!f)
        return -1;

    int error = 0;

    error |= fwrite("RIFF", 1, 4, f) != 4;
    error |= put32(f, 36 + (uint32_t) count * 2);
    error |= fwrite("WAVEfmt ", 1, 8, f) != 8;
    error |= put32(f, 16);
    error |= put32(f, 0x00010001);
    error |= put32(f, 12000);
    error |= put32(f, 24000);
    error |= put32(f, 0x00100002);
    error |= fwrite("data", 1, 4, f) != 4;
    error |= put32(f, (uint32_t) count * 2);

    for (size_t i = 0; i < count && !error; i++) {
        if (!isfinite(audio[i])) {
            error = 1;
            break;
        }

        double        x = fmax(-1, fmin(1, audio[i]));
        int           v = (int) lrint(x * 32767);
        unsigned      u = (unsigned) v & 65535;
        unsigned char b[2] = { (unsigned char) u, (unsigned char) (u >> 8) };

        error |= fwrite(b, 1, 2, f) != 2;
    }

    error |= fclose(f) != 0;

    return error ? -1 : 0;
}
