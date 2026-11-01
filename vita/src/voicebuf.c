#include "voicebuf.h"

void voicebuf_init(VoiceBuf *b) {
    b->head = b->tail = 0;
}

int voicebuf_push(VoiceBuf *b, const int16_t *in, int n) {
    uint32_t head = b->head, tail = b->tail;
    int stored = 0;
    while (stored < n) {
        uint32_t next = (head + 1) % VOICEBUF_SAMPLES;
        if (next == tail) break;
        b->s[head] = in[stored++];
        head = next;
    }
    __sync_synchronize(); /* samples before the index the consumer reads */
    b->head = head;
    return stored;
}

int voicebuf_pop(VoiceBuf *b, int16_t *out, int max) {
    uint32_t head = b->head, tail = b->tail;
    __sync_synchronize();
    int read = 0;
    while (read < max && tail != head) {
        out[read++] = b->s[tail];
        tail = (tail + 1) % VOICEBUF_SAMPLES;
    }
    b->tail = tail;
    return read;
}

int voicebuf_level(const int16_t *in, int n) {
    int peak = 0;
    for (int i = 0; i < n; i++) {
        int v = in[i] < 0 ? -(int)in[i] : in[i];
        if (v > peak) peak = v;
    }
    int level = (peak * 100 + 16383) / 32767;
    return level > 100 ? 100 : level;
}
