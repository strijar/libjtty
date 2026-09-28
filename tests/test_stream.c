#include "jtty.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef JT_TEST_ALLOC
static size_t allocations, releases;
static int    deny_allocation;
void         *__real_malloc(size_t size);
void         *__real_calloc(size_t count, size_t size);
void         *__real_realloc(void *ptr, size_t size);
void          __real_free(void *ptr);
void         *__wrap_malloc(size_t size) {
    allocations++;
    return deny_allocation ? NULL : __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
    allocations++;
    return deny_allocation ? NULL : __real_calloc(count, size);
}
void *__wrap_realloc(void *ptr, size_t size) {
    allocations++;
    return deny_allocation ? NULL : __real_realloc(ptr, size);
}
void __wrap_free(void *ptr) {
    releases++;
    __real_free(ptr);
}
#endif

typedef struct {
    jtty_frame frames[16];
    size_t     count;
} result;
static void collect(const jtty_frame *frame, void *user) {
    result *r = user;
    assert(r->count < 16);
    if (r->count)
        assert(frame->time >= r->frames[r->count - 1].time);
    r->frames[r->count++] = *frame;
}

static void same(const result *a, const result *b) {
    assert(a->count == b->count);
    for (size_t i = 0; i < a->count; i++) {
        assert(a->frames[i].payload == b->frames[i].payload);
        assert(a->frames[i].time == b->frames[i].time);
        assert(a->frames[i].frequency == b->frames[i].frequency);
        assert(a->frames[i].sync == b->frames[i].sync);
        assert(a->frames[i].eom == b->frames[i].eom);
        assert(!strcmp(a->frames[i].text, b->frames[i].text));
    }
}

static void run(jtty_rx *rx, const float *audio, size_t count, size_t packet, result *r) {
    jtty_rx_reset(rx);
    uint32_t rng = 57;
    int      total = 0;
    for (size_t offset = 0; offset < count;) {
        rng = rng * 1664525u + 1013904223u;
        size_t chunk = packet ? packet : 1 + rng % 513;
        if (chunk > count - offset)
            chunk = count - offset;
        assert(jtty_rx_process(rx, NULL, 0, collect, r) == 0);
        int got = jtty_rx_process(rx, audio + offset, chunk, collect, r);
        assert(got >= 0);
        total += got;
        offset += chunk;
    }
    int got = jtty_rx_flush(rx, collect, r);
    assert(got >= 0);
    total += got;
    assert((size_t) total == r->count);
    assert(!jtty_rx_flush(rx, collect, r));
    assert(jtty_rx_process(rx, audio, 1, collect, r) == -1);
}

static void boundaries(void) {
    size_t starts[] = { 0, 30720 - 30, 66013 };
    size_t count = starts[2] + JTTY_FRAME_SAMPLES;
    float *audio = calloc(count, sizeof(*audio));
    assert(audio);
    uint64_t payloads[3];
    for (size_t i = 0; i < 3; i++) {
        jtty_atom atom = { 0 };
        atom.kind = JTTY_NUM;
        atom.subtype = JTTY_SERIAL;
        atom.value = 123 + (int) (i % 2);
        uint8_t tones[JTTY_SYMBOLS];
        assert(!jtty_atom_pack(&atom, 1, &payloads[i]));
        assert(!jtty_encode(payloads[i], tones));
        assert(!jtty_modulate(tones, JTTY_SYMBOLS, 1000.9, audio + starts[i], JTTY_FRAME_SAMPLES));
    }
    uint32_t rng = 29;
    for (size_t i = 0; i < count; i++) {
        rng = rng * 1664525u + 1013904223u;
        audio[i] += (float) (0.3 * ((double) rng / 2147483648.0 - 1));
    }
    jtty_rx_config config = { 950, 1050, 128, 128 };
#ifdef JT_TEST_ALLOC
    size_t before = allocations;
#endif
    jtty_rx *rx = jtty_rx_create(&config);
    assert(rx);
#ifdef JT_TEST_ALLOC
    assert(allocations == before + 1);
    before = allocations;
    size_t freed = releases;
    deny_allocation = 1;
#endif
    result expected = { 0 };
    run(rx, audio, count, count, &expected);
    assert(expected.count == 3);
    for (size_t i = 0; i < 3; i++) {
        assert(expected.frames[i].payload == payloads[i]);
        assert(fabs(expected.frames[i].time - (double) starts[i] / JTTY_SAMPLE_RATE) < 0.012);
    }
    jtty_rx_stats reference;
    assert(!jtty_rx_get_stats(rx, &reference));
    assert(reference.samples == count && reference.frames == 3 && !reference.pending_candidates);
    size_t packets[] = { 1, 47, 48, 137, 256, 4096, 0 };
    for (size_t i = 0; i < sizeof(packets) / sizeof(*packets); i++) {
        result actual = { 0 };
        run(rx, audio, count, packets[i], &actual);
        same(&expected, &actual);
        jtty_rx_stats stats;
        assert(!jtty_rx_get_stats(rx, &stats));
        assert(stats.decode_attempts == reference.decode_attempts);
        assert(stats.candidates_dropped == reference.candidates_dropped);
        assert(stats.budget_dropped == reference.budget_dropped);
        assert(stats.memory_bytes == reference.memory_bytes);
    }
    /* An incomplete frame must never be completed by flush padding. */
    result partial = { 0 };
    run(rx, audio, JTTY_FRAME_SAMPLES - 1, 17, &partial);
    assert(!partial.count);
    jtty_rx_reset(rx);
    assert(!jtty_rx_process(rx, audio, 113, NULL, NULL));
    float invalid[] = { 0, 0, NAN };
    assert(jtty_rx_process(rx, invalid, 3, NULL, NULL) == -1);
    assert(jtty_rx_process(rx, NULL, 1, NULL, NULL) == -1);
    invalid[2] = INFINITY;
    assert(jtty_rx_process(rx, invalid, 3, NULL, NULL) == -1);
    invalid[2] = 1e7f;
    assert(jtty_rx_process(rx, invalid, 3, NULL, NULL) == -1);
    jtty_rx_stats stats;
    assert(!jtty_rx_get_stats(rx, &stats) && stats.samples == 113);
    int delivered = jtty_rx_process(rx, audio + 113, count - 113, NULL, NULL);
    assert(delivered >= 0 && delivered + jtty_rx_flush(rx, NULL, NULL) == 3);
#ifdef JT_TEST_ALLOC
    deny_allocation = 0;
    assert(allocations == before && releases == freed);
#endif
    printf("Streaming memory (950-1050 Hz, 128 candidates): %zu bytes\n", stats.memory_bytes);
    jtty_rx_destroy(rx);
    free(audio);
}

static void limits(void) {
    assert(!jtty_rx_create(NULL));
    jtty_rx_config config = { 950, 1050, 0, 1 };
    assert(!jtty_rx_create(&config));
    config.candidate_capacity = 1;
    config.decode_budget = 2;
    assert(!jtty_rx_create(&config));
    config.decode_budget = 1;
    config.fmin = NAN;
    assert(!jtty_rx_create(&config));
    config.fmin = 1051;
    assert(!jtty_rx_create(&config));
    config.fmin = 950;
    config.fmax = 5906.25;
    assert(!jtty_rx_create(&config));
    config.fmax = 1050;
    config.candidate_capacity = SIZE_MAX;
    assert(!jtty_rx_create(&config));
    config.candidate_capacity = SIZE_MAX / 2;
    assert(!jtty_rx_create(&config));
    config.candidate_capacity = 1;
#ifdef JT_TEST_ALLOC
    deny_allocation = 1;
    assert(!jtty_rx_create(&config));
    deny_allocation = 0;
#endif
    size_t count = 72000;
    float *noise = malloc(count * sizeof(*noise));
    assert(noise);
    uint32_t rng = 1;
    for (size_t i = 0; i < count; i++) {
        rng = rng * 1664525u + 1013904223u;
        noise[i] = (float) ((double) rng / 2147483648.0 - 1);
    }
    config.fmin = 200;
    config.fmax = 3000;
    for (int pass = 0; pass < 2; pass++) {
        config.candidate_capacity = pass ? 128 : 1;
        jtty_rx *rx = jtty_rx_create(&config);
        assert(rx);
        result r = { 0 };
        run(rx, noise, count, 37, &r);
        jtty_rx_stats stats;
        assert(!jtty_rx_get_stats(rx, &stats));
        assert(stats.decode_attempts <= (count + 3071) / 3072);
        assert(pass ? stats.budget_dropped > 0 : stats.candidates_dropped > 0);
        assert(!r.count);
        jtty_rx_destroy(rx);
    }
    free(noise);
    assert(jtty_rx_process(NULL, NULL, 0, NULL, NULL) == -1);
    assert(jtty_rx_flush(NULL, NULL, NULL) == -1);
    jtty_rx_reset(NULL);
    jtty_rx_destroy(NULL);
}

static void reenter(const jtty_frame *frame, void *user) {
    (void) frame;
    jtty_rx *rx = user;
    assert(jtty_rx_process(rx, NULL, 0, NULL, NULL) == -1);
    assert(jtty_rx_flush(rx, NULL, NULL) == -1);
    jtty_rx_reset(rx);
    jtty_rx_destroy(rx);
}

static void long_stream(void) {
    jtty_rx_config config = { 1000, 1000, 8, 8 };
    jtty_rx       *rx = jtty_rx_create(&config);
    assert(rx);
    float silence[1200] = { 0 };
    for (size_t i = 0; i < 1210; i++)
        assert(!jtty_rx_process(rx, silence, 1200, NULL, NULL));
    jtty_rx_stats stats;
    assert(!jtty_rx_get_stats(rx, &stats));
    assert(stats.samples == 121u * JTTY_SAMPLE_RATE);
    uint64_t payloads[16];
    char     text[81];
    uint8_t  tones[JTTY_SYMBOLS];
    float    audio[JTTY_FRAME_SAMPLES];
    assert(jtty_pack("CQ R1CBU CQ", 0, payloads, text) == 1);
    assert(!jtty_encode(payloads[0], tones));
    assert(!jtty_modulate(tones, JTTY_SYMBOLS, 1000, audio, JTTY_FRAME_SAMPLES));
    result r = { 0 };
    int    got = jtty_rx_process(rx, audio, JTTY_FRAME_SAMPLES, collect, &r);
    assert(got >= 0);
    assert(got + jtty_rx_flush(rx, collect, &r) == 1);
    assert(r.count == 1 && r.frames[0].payload == payloads[0]);
    assert(fabs(r.frames[0].time - 121) < 0.012);
    jtty_rx_reset(rx);
    got = jtty_rx_process(rx, audio, JTTY_FRAME_SAMPLES, reenter, rx);
    assert(got >= 0);
    assert(got + jtty_rx_flush(rx, reenter, rx) == 1);
    assert(!jtty_rx_get_stats(rx, &stats));
    assert(stats.samples == JTTY_FRAME_SAMPLES && stats.frames == 1);
    jtty_rx_destroy(rx);
}

int main(void) {
    boundaries();
    limits();
    long_stream();
    puts("Streaming tests passed.");
    return 0;
}
