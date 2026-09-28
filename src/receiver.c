#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int chronological(const void *left, const void *right) {
    const jtty_frame *a = left, *b = right;

    return (a->time > b->time) - (a->time < b->time);
}

int jtty_receive(const float *audio, size_t count, double fmin, double fmax, jtty_frame_callback callback, void *user) {
    if (!audio || !isfinite(fmin) || !isfinite(fmax) || fmin <= 0 || fmax < fmin ||
        fmax + 93.75 >= 6000 || count > 120u * JTTY_SAMPLE_RATE)
        return -1;

    for (size_t i = 0; i < count; i++)
        if (!isfinite(audio[i]) || fabsf(audio[i]) > 1e6f)
            return -1;

    if (count < JTTY_FRAME_SAMPLES)
        return 0;

    jt_candidate candidates[128];
    jtty_frame   frames[128];
    int          total = 0;
    int          n = jt_find_candidates(audio, count, fmin, fmax, candidates, 128);

    if (n < 0)
        return -1;

    for (int i = 0; i < n; i++) {
        jt_candidate c = candidates[i];

        jt_refine(audio, count, &c);

        /* Refinement must not move outside the requested frequency window. */

        if (c.frequency < fmin)
            c.frequency = fmin;

        if (c.frequency > fmax)
            c.frequency = fmax;

        c.score = jt_sync_score(audio, c.sample, c.frequency);

        if (c.score < 0.46)
            continue;

        int duplicate = 0;

        for (int j = 0; j < total; j++)
            if (fabs(frames[j].time - (double) c.sample / 12000) < 0.2 && fabs(frames[j].frequency - c.frequency) < 12)
                duplicate = 1;

        if (duplicate)
            continue;

        float scores[46][4];

        for (int s = 0; s < 46; s++)
            jt_correlate(audio, c.sample + (size_t) (s + 13) * JTTY_SPS, c.frequency, scores[s]);

        uint64_t p;

        if (jtty_decode((const float(*)[4]) scores, &p))
            continue;

        jtty_atom a;
        int       eom;

        if (jtty_atom_unpack(p, &a, &eom))
            continue;

        jtty_frame *frame = &frames[total++];
        memset(frame, 0, sizeof(*frame));

        frame->payload = p;
        frame->time = (double) c.sample / 12000;
        frame->frequency = c.frequency;
        frame->sync = c.score;
        frame->eom = eom;

        jtty_atom_render(&a, frame->text);
    }

    qsort(frames, (size_t) total, sizeof(*frames), chronological);

    for (int i = 0; i < total; i++)
        if (callback)
            callback(&frames[i], user);

    return total;
}
