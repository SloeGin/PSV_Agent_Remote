#include "voicebuf.h"
#include "test.h"

static VoiceBuf g_b; /* 32 KB: keep it off the stack */

static void t_push_pop_round_trip(void) {
    voicebuf_init(&g_b);
    int16_t in[300], out[300];
    for (int i = 0; i < 300; i++) in[i] = (int16_t)(i * 7 - 1000);
    CHECK(voicebuf_push(&g_b, in, 300) == 300);
    CHECK(voicebuf_pop(&g_b, out, 300) == 300);
    for (int i = 0; i < 300; i++) CHECK(out[i] == in[i]);
    CHECK(voicebuf_pop(&g_b, out, 300) == 0);
}

static void t_wraps(void) {
    static int16_t in[12000], out[12000];
    voicebuf_init(&g_b);
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 12000; i++) in[i] = (int16_t)(i + round);
        CHECK(voicebuf_push(&g_b, in, 12000) == 12000);
        CHECK(voicebuf_pop(&g_b, out, 12000) == 12000);
        for (int i = 0; i < 12000; i++) CHECK(out[i] == in[i]);
    }
}

static void t_overflow_drops_newest(void) {
    static int16_t in[17000], out[17000];
    voicebuf_init(&g_b);
    for (int i = 0; i < 17000; i++) in[i] = (int16_t)i;
    CHECK(voicebuf_push(&g_b, in, 17000) == 15999); /* one slot stays free */
    CHECK(voicebuf_pop(&g_b, out, 17000) == 15999);
    for (int i = 0; i < 15999; i++) CHECK(out[i] == (int16_t)i);
}

static void t_level(void) {
    int16_t z[4] = {0, 0, 0, 0};
    CHECK(voicebuf_level(z, 4) == 0);
    int16_t loud[2] = {0, 32767};
    CHECK(voicebuf_level(loud, 2) == 100);
    int16_t half[1] = {-16384};
    CHECK(voicebuf_level(half, 1) == 50);
    int16_t floor_[1] = {-32768};
    CHECK(voicebuf_level(floor_, 1) == 100);
}

int main(void) {
    RUN(t_push_pop_round_trip);
    RUN(t_wraps);
    RUN(t_overflow_drops_newest);
    RUN(t_level);
    return TEST_RESULT();
}
