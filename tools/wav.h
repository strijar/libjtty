#ifndef JTTY_WAV_H
#define JTTY_WAV_H
#include <stddef.h>

/* PCM16 mono 12 kHz RIFF/WAVE. Read accepts unknown RIFF chunks. */

int wav_read(const char *path, float **audio, size_t *count);
int wav_write(const char *path, const float *audio, size_t count);

#endif
