#include "jtty.h"
#include "internal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng = 0x7351ac92;
static double   uniform(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return ((double) rng + 0.5) / 4294967296.0;
}
static double gaussian(void) {
    return sqrt(-2 * log(uniform())) * cos(2 * JT_PI * uniform());
}
static void source_vectors(void) {
    /* Frozen normative vectors: WSJT-X lib/jtty/jtty_source_encoding.txt. */
    const uint64_t words[] = { 0x026F78D41, 0x20007B008, 0x20007B009, 0x2001BA019, 0x00B790D29, 0x082C58029, 0x204E42D39, 0x000000049, 0x04A198049, 0x11395558D };
    const char    *texts[] = { "CQ K1ABC CQ", "599 123", "599 123", "599 CA", "599 05 NWT", "1D EMA", "599 156 1749", "AGN?", "FN42", "HELLO" };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        jtty_atom a;
        int       eom;
        uint64_t  back;
        char      text[81];
        assert(!jtty_atom_unpack(words[i], &a, &eom));
        assert(!jtty_atom_pack(&a, eom, &back) && back == words[i]);
        assert(!jtty_atom_render(&a, text) && !strcmp(text, texts[i]));
    }
    jtty_atom      a;
    int            eom;
    const uint64_t invalid[] = { 0, 1, UINT64_C(1) << 34, 0x026F78D43, ((uint64_t) 0x16 << 2) | 1, /* reserved STRUCT30 family 5 */
                                 ((uint64_t) 0x12 << 2) | 1 | ((uint64_t) 18 << 23),               /* control ID 18 */
                                 0x20007B009 | 128 /* nonzero reserved number field */ };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert(jtty_atom_unpack(invalid[i], &a, &eom) < 0);
    /* Valid random wire words must survive canonical unpack/repack. */
    for (int i = 0; i < 20000; i++) {
        uint64_t p = ((uint64_t) (uniform() * 4294967296.0) << 2) | (unsigned) (i & 1), back;
        if (!jtty_atom_unpack(p, &a, &eom))
            assert(!jtty_atom_pack(&a, eom, &back) && p == back);
    }
}
static void atom_boundaries(void) {
    jtty_atom a = { 0 }, back;
    uint64_t  p;
    int       eom;
    for (int k = 0; k < 8; k++) {
        a = (jtty_atom) { 0 };
        a.kind = JTTY_NUM;
        a.subtype = k;
        int max = k == 1 ? 40 : k == 2 ? 90
                            : k == 6   ? 9999
                                       : 131071;
        a.value = max;
        assert(!jtty_atom_pack(&a, 1, &p));
        assert(!jtty_atom_unpack(p, &back, &eom) && back.value == max && eom == 1);
        a.value = max + 1;
        assert(jtty_atom_pack(&a, 1, &p) < 0);
        a.value = -1;
        assert(jtty_atom_pack(&a, 1, &p) < 0);
    }
    a = (jtty_atom) { 0 };
    a.kind = JTTY_LOC;
    strcpy(a.text, "ZZZ");
    assert(!jtty_atom_pack(&a, 1, &p));
    assert(!jtty_atom_unpack(p, &back, &eom));
    strcpy(a.text, "0AB");
    assert(jtty_atom_pack(&a, 1, &p) < 0);
    a = (jtty_atom) { 0 };
    a.kind = JTTY_GRID;
    strcpy(a.text, "RR99");
    assert(!jtty_atom_pack(&a, 1, &p));
    assert(!jtty_atom_unpack(p, &back, &eom));
    strcpy(a.text, "SA00");
    assert(jtty_atom_pack(&a, 1, &p) < 0);
    a = (jtty_atom) { 0 };
    a.kind = JTTY_TIME;
    a.value = 16383;
    a.value2 = 1439;
    assert(!jtty_atom_pack(&a, 1, &p));
    assert(!jtty_atom_unpack(p, &back, &eom));
    a.value2 = 1440;
    assert(jtty_atom_pack(&a, 1, &p) < 0);
    a = (jtty_atom) { 0 };
    a.kind = JTTY_PAIR;
    a.subtype = JTTY_CLASS_SECTION;
    a.value = 32;
    a.value2 = 86;
    strcpy(a.text, "F");
    assert(!jtty_atom_pack(&a, 1, &p));
    assert(!jtty_atom_unpack(p, &back, &eom));
    a.value2 = 87;
    assert(jtty_atom_pack(&a, 1, &p) < 0);
    a = (jtty_atom) { 0 };
    a.kind = JTTY_TEXT5;
    memset(a.text, 'A', sizeof(a.text));
    assert(jtty_atom_pack(&a, 1, &p) < 0);
    assert(jtty_atom_pack(NULL, 1, &p) < 0);
    assert(jtty_atom_unpack(UINT64_MAX, &a, &eom) < 0);
}
static void text_tests(void) {
    const char *texts[] = { "CQ K1ABC CQ", "CQ KA1ABC CQ", "WB9XYZ", "WB9XYZ TU CQ KA1ABC CQ", "WB9XYZ 599 123", "599 123", "599 MA", "599 FN42", "1D EMA", "599 001", "599 05", "599 BRUCE", "HELLO WORLD!", "R1CBU AGN?", "TU NOW R1CBU", "CQ Q1ABC CQ", "00", "3DA0ABC", "CQ 3XABC CQ" };
    const int   counts[] = { 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 2, 3, 1, 1, 3, 1, 2, 3 };
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
        uint64_t frames[16];
        char     normal[81], decoded[81];
        int      n = jtty_pack(texts[i], 0, frames, normal);
        if (n != counts[i])
            fprintf(stderr, "%s: expected %d frames, got %d\n", texts[i], counts[i], n);
        assert(n == counts[i]);
        assert(!strcmp(normal, texts[i]));
        assert(!jtty_unpack(frames, (size_t) n, decoded));
        assert(!strcmp(decoded, normal));
    }
    uint64_t frames[16];
    char     normal[81], decoded[81];
    assert(jtty_pack("  cq r1cbu  cq  ", 0, frames, normal) == 1 && !strcmp(normal, "CQ R1CBU CQ"));
    assert(jtty_pack("599 05", 2, frames, normal) == 1 && !strcmp(normal, "599 005"));
    assert(jtty_pack("599 0123", 2, frames, normal) == 1 && frames[0] == 0x20007B009);
    assert(jtty_pack("599 599 05", 2, frames, normal) > 0 && !strcmp(normal, "599 599 05"));
    assert(jtty_pack("HELLO~WORLD", 0, frames, normal) > 0 && !strcmp(normal, "HELLO WORLD"));
    char expanded[81];
    memset(expanded, 'A', 73);
    strcpy(expanded + 73, " 599 05");
    assert(jtty_pack(expanded, 2, frames, normal) < 0);
    assert(jtty_pack("", 0, frames, normal) == 0);
    assert(jtty_pack("hello", 8, frames, normal) < 0);
    char longtext[100];
    memset(longtext, 'X', 99);
    longtext[99] = 0;
    assert(jtty_pack(longtext, 0, frames, normal) == 16 && strlen(normal) == 80);
    for (int t = 0; t < 500; t++) {
        char text[81];
        int  len = 1 + (int) (uniform() * 80);
        for (int i = 0; i < len; i++)
            text[i] = jt_alphabet[(int) (uniform() * 64)];
        text[len] = 0;
        int n = jtty_pack(text, 0, frames, normal);
        if (n > 0)
            assert(!jtty_unpack(frames, (size_t) n, decoded) && !strcmp(decoded, normal));
    }
}
static void fec_tests(void) {
    /* Frozen channel vector from test_jtty_tbcc_code_profile.f90. */
    const char   *bits = "1001001001001001001001001001001001";
    const uint8_t golden[46] = { 3, 0, 1, 0, 3, 2, 1, 2, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 3, 3 };
    uint64_t      p = 0;
    for (int i = 0; i < 34; i++)
        p = (p << 1) | (unsigned) (bits[i] - '0');
    uint8_t tones[59];
    assert(!jtty_encode(p, tones));
    assert(!memcmp(tones + 13, golden, 46));
    assert(jt_crc(p, 34) == 0x24a); /* original information bits end 001001001010 */
    for (int i = 0; i < 100; i++) {
        jtty_atom a = { 0 };
        a.kind = JTTY_NUM;
        a.subtype = i % 8;
        a.role = i % 2;
        a.value = a.subtype == 1 ? 1 + i % 40 : a.subtype == 2 ? 1 + i % 90
                                                               : i * 17;
        assert(!jtty_atom_pack(&a, 1, &p));
        assert(!jtty_encode(p, tones));
        float scores[46][4] = { { 0 } };
        for (int s = 0; s < 46; s++)
            scores[s][tones[s + 13]] = 1;
        /* Three isolated wrong hard decisions plus soft observations. */
        for (int j = 0; j < 3; j++) {
            int s = (i + 13 * j) % 46;
            scores[s][tones[s + 13]] = 0;
            scores[s][(tones[s + 13] + 1) % 4] = 1;
        }
        uint64_t decoded = 0;
        assert(!jtty_decode((const float(*)[4]) scores, &decoded) && decoded == p);
    }
    float flat[46][4] = { { 0 } };
    assert(jtty_decode((const float(*)[4]) flat, &p) < 0);
    flat[0][0] = NAN;
    assert(jtty_decode((const float(*)[4]) flat, &p) < 0);
    /* Channel coding permits raw payloads, receiver rejects source-invalid words. */
    assert(!jtty_encode(3, tones));
    memset(flat, 0, sizeof(flat));
    for (int i = 0; i < 46; i++)
        flat[i][tones[13 + i]] = 1;
    assert(jtty_decode((const float(*)[4]) flat, &p) < 0);
}
typedef struct {
    uint64_t p[16];
    double   time[16], frequency[16];
    int      count;
} result;
static void collect(const jtty_frame *frame, void *user) {
    result *r = user;
    assert(r->count < 16);
    r->p[r->count] = frame->payload;
    r->time[r->count] = frame->time;
    r->frequency[r->count++] = frame->frequency;
}
/* Exercise the public streaming interface with small, uneven chunks. */
static int receive_audio(const float *audio, size_t count, double lo, double hi, jtty_frame_callback callback, void *user) {
    jtty_rx_config config = { lo, hi, 128, 128 };
    jtty_rx       *rx = jtty_rx_create(&config);
    if (!rx)
        return -1;
    int total = 0;
    for (size_t i = 0; i < count;) {
        size_t chunk = count - i < 137 ? count - i : 137;
        int    got = jtty_rx_process(rx, audio + i, chunk, callback, user);
        if (got < 0) {
            jtty_rx_destroy(rx);
            return -1;
        }
        total += got;
        i += chunk;
    }
    total += jtty_rx_flush(rx, callback, user);
    jtty_rx_destroy(rx);
    return total;
}
static void audio_tests(void) {
    uint64_t frames[16];
    char     normal[81];
    uint8_t  tones[16 * 59];
    int      n = jtty_pack("R1CBU 599 123", 0, frames, normal);
    assert(n == 2);
    for (int i = 0; i < n; i++)
        assert(!jtty_encode(frames[i], tones + 59 * i));
    size_t delay = 1543, length = (size_t) n * JTTY_FRAME_SAMPLES, count = delay + length + 937;
    float *audio = calloc(count, sizeof(*audio));
    assert(audio);
    assert(!jtty_modulate(tones, (size_t) n * 59, 1234.56, audio + delay, length));
    /* Unit-amplitude sine: noise sigma=1.8 => approx -4.3 dB in 2500 Hz. */
    for (size_t i = 0; i < count; i++)
        audio[i] += (float) (1.8 * gaussian());
    result r = { 0 };
    int    got = receive_audio(audio, count, 1150, 1320, collect, &r);
    fprintf(stderr, "Audio with AWGN: %d/%d frames\n", got, n);
    assert(got == n && r.count == n);
    for (int i = 0; i < n; i++) {
        assert(r.p[i] == frames[i]);
        assert(fabs(r.time[i] - (double) (delay + (size_t) i * JTTY_FRAME_SAMPLES) / 12000) < 0.012);
        assert(fabs(r.frequency[i] - 1234.56) < 4);
    }
    memset(audio, 0, count * sizeof(*audio));
    assert(receive_audio(audio, count, 1150, 1320, NULL, NULL) == 0);
    for (size_t i = 0; i < count; i++)
        audio[i] = (float) gaussian();
    assert(receive_audio(audio, count, 1150, 1320, NULL, NULL) == 0);
    assert(receive_audio(audio, count, 1400, 1300, NULL, NULL) < 0);
    audio[0] = NAN;
    assert(receive_audio(audio, count, 1150, 1320, NULL, NULL) < 0);
    free(audio);
}
static void multiple_signals(void) {
    const size_t count = JTTY_FRAME_SAMPLES + 3000;
    float       *mix = calloc(count, sizeof(*mix)), *second = calloc(count, sizeof(*second));
    assert(mix && second);
    uint64_t p[16], q[16];
    uint8_t  tones[59];
    char     normalized[81];
    assert(jtty_pack("CQ R1CBU CQ", 0, p, normalized) == 1);
    assert(jtty_pack("599 123", 0, q, normalized) == 1);
    assert(!jtty_encode(p[0], tones));
    assert(!jtty_modulate(tones, 59, 800.9, mix + 435, count - 435));
    assert(!jtty_encode(q[0], tones));
    assert(!jtty_modulate(tones, 59, 1350.1, second + 1859, count - 1859));
    for (size_t i = 0; i < count; i++)
        mix[i] += 0.7f * second[i];
    result r = { 0 };
    assert(receive_audio(mix, count, 750, 1450, collect, &r) == 2);
    assert(r.p[0] == p[0] && r.p[1] == q[0]);
    free(second);
    free(mix);
}
int main(void) {
    source_vectors();
    atom_boundaries();
    text_tests();
    fec_tests();
    audio_tests();
    multiple_signals();
    puts("All JTTY tests passed.");
    return 0;
}
