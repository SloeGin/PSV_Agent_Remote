#pragma once

#include <stdint.h>

/* One second of 16 kHz microphone samples between the mic thread (the only
 * producer) and the main loop (the only consumer). One slot stays free to tell
 * full from empty; when full, the newest samples are dropped. */
#define VOICEBUF_SAMPLES 16000

typedef struct {
    int16_t s[VOICEBUF_SAMPLES];
    volatile uint32_t head, tail; /* head: next write; tail: next read */
} VoiceBuf;

void voicebuf_init(VoiceBuf *b);
int voicebuf_push(VoiceBuf *b, const int16_t *in, int n); /* samples stored */
int voicebuf_pop(VoiceBuf *b, int16_t *out, int max);     /* samples read */

/* Peak absolute value of in[0..n), scaled to 0-100. */
int voicebuf_level(const int16_t *in, int n);
