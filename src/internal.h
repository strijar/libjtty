#ifndef JTTY_INTERNAL_H
#define JTTY_INTERNAL_H
#include "jtty.h"
#define JT_PI 3.1415926535897932384626433832795

extern const char        jt_alphabet[];
extern const char *const jt_controls[18];
extern const uint8_t     jt_sync[13];

int                      jt_letter(char c);
int                      jt_digit(char c);
int                      jt_index(const char *s, char c);
int                      jt_call_pack(const char *s, uint32_t *v);
int                      jt_call_unpack(uint32_t v, char s[14]);
unsigned                 jt_crc(uint64_t word, int bits);
unsigned                 jt_trellis_tone(unsigned reg);

/* Scores/correlations for rectangular one-symbol matched filters. */

void   jt_correlate(const float *audio, size_t start, double f0, float scores[4]);
double jt_sync_score(const float *audio, size_t start, double f0);

typedef struct {
    size_t sample;
    double frequency, score;
} jt_candidate;

int  jt_find_candidates(const float *audio, size_t count, double lo, double hi, jt_candidate *candidates, int capacity);
void jt_refine(const float *audio, size_t count, jt_candidate *candidate);

#endif
