#include "jtty.h"
#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <math.h>

static int number(const char *s, double *v) {
    char *end;
    errno = 0;
    *v = strtod(s, &end);

    return errno || end == s || *end || !isfinite(*v) ? -1 : 0;
}

static int profile_id(const char *s) {
    if (!strcmp(s, "unknown"))
        return JTTY_UNKNOWN;

    if (!strcmp(s, "field-day"))
        return JTTY_FIELD_DAY;

    if (!strcmp(s, "rtty-roundup"))
        return JTTY_RTTY_ROUNDUP;

    return -1;
}

static void print_frame(const jtty_frame *frame, void *unused) {
    (void) unused;
    printf("%.3f s  %.2f Hz  sync=%.3f  %09" PRIX64 "  %s%s\n", frame->time, frame->frequency, frame->sync, frame->payload, frame->text, frame->eom ? " [EOM]" : "");
}

static int usage(void) {
    fputs("Usage:\n  jtty pack TEXT [unknown|field-day|rtty-roundup]\n"
          "  jtty tones TEXT [PROFILE]\n  jtty encode OUTPUT.wav TEXT [F0_HZ [PROFILE]]\n"
          "  jtty decode INPUT.wav [FMIN_HZ FMAX_HZ]\n"
          "Audio: PCM16 mono, 12000 Hz; frequency is the lowest of four tones.\n",
          stderr);
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 3)
        return usage();

    if (!strcmp(argv[1], "decode")) {
        if (argc != 3 && argc != 5)
            return usage();

        double lo = 200, hi = 3000;

        if (argc == 5 && (number(argv[3], &lo) || number(argv[4], &hi)))
            return usage();

        float *audio;
        size_t count;

        if (wav_read(argv[2], &audio, &count)) {
            fputs("Cannot read PCM16 mono 12 kHz WAV (maximum 120 s).\n", stderr);
            return 1;
        }

        jtty_rx_config config = { lo, hi, 128, 128 };
        jtty_rx       *rx = jtty_rx_create(&config);
        int            n = rx ? 0 : -1;
        for (size_t offset = 0; rx && offset < count;) {
            size_t chunk = count - offset < 256 ? count - offset : 256;
            int    got = jtty_rx_process(rx, audio + offset, chunk, print_frame, NULL);
            if (got < 0) {
                n = -1;
                break;
            }
            n += got;
            offset += chunk;
        }
        if (n >= 0)
            n += jtty_rx_flush(rx, print_frame, NULL);
        jtty_rx_destroy(rx);
        free(audio);

        if (n < 0) {
            fputs("Invalid receive parameters or insufficient memory.\n", stderr);
            return 1;
        }

        fprintf(stderr, "Decoded frames: %d\n", n);
        return n ? 0 : 1;
    }

    int encode = !strcmp(argv[1], "encode"), tones = !strcmp(argv[1], "tones"), pack = !strcmp(argv[1], "pack");

    if (!encode && !tones && !pack)
        return usage();

    if ((encode && (argc < 4 || argc > 6)) || (!encode && argc > 4))
        return usage();

    int    profile = JTTY_UNKNOWN;
    double f0 = 1000;

    if (encode && argc >= 5 && number(argv[4], &f0))
        return usage();

    if (encode && argc == 6)
        profile = profile_id(argv[5]);

    if (!encode && argc == 4)
        profile = profile_id(argv[3]);

    if (profile < 0)
        return usage();

    uint64_t frames[16];
    char     normalized[81];
    uint8_t  symbols[16 * 59];
    int      n = jtty_pack(argv[encode ? 3 : 2], profile, frames, normalized);

    if (n <= 0) {
        fputs("Message cannot be packed.\n", stderr);
        return 1;
    }

    for (int i = 0; i < n; i++)
        jtty_encode(frames[i], symbols + i * 59);

    if (pack) {
        printf("%s\nframes=%d duration=%.3f s\n", normalized, n, n * 1.888);

        for (int i = 0; i < n; i++)
            printf("%09" PRIX64 "\n", frames[i]);
    } else if (tones) {
        for (int i = 0; i < n * 59; i++) {
            putchar('0' + symbols[i]);
            if (i % 59 == 58)
                putchar('\n');
        }
    } else {
        /* A short guard leaves room for acquisition and boundary refinement. */
        size_t guard = 2400, count = (size_t) n * JTTY_FRAME_SAMPLES + 2 * guard;
        float *audio = calloc(count, sizeof(*audio));

        if (!audio)
            return 1;

        int error = jtty_modulate(symbols, (size_t) n * 59, f0, audio + guard, count - 2 * guard);

        if (!error) {
            for (size_t i = 0; i < count; i++)
                audio[i] *= 0.8f;
            error = wav_write(argv[2], audio, count);
        }

        free(audio);

        if (error) {
            fputs("Cannot generate WAV; check path and frequency.\n", stderr);
            return 1;
        }
        printf("%s: %d frames, %s\n", argv[2], n, normalized);
    }
    return 0;
}
