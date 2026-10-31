#include "screen.h"
#include "test.h"

static void t_awake_draws_every_frame(void) {
    Screen s;
    screen_init(&s);
    CHECK(screen_step(&s, false, false));
    CHECK(screen_step(&s, false, false));
    CHECK(!s.off);
}

static void t_dimming_draws_one_black_frame_then_stops(void) {
    Screen s;
    screen_init(&s);
    CHECK(screen_step(&s, true, false)); /* the black frame */
    CHECK(s.off);
    CHECK(!screen_step(&s, true, false));
    CHECK(!screen_step(&s, true, false));
}

static void t_waking_draws_again(void) {
    Screen s;
    screen_init(&s);
    screen_step(&s, true, false);
    screen_step(&s, true, false);
    CHECK(screen_step(&s, false, false));
    CHECK(!s.off);
}

static void t_open_dialog_keeps_rendering(void) {
    /* the system IME is drawn by our frame loop: it must keep running */
    Screen s;
    screen_init(&s);
    CHECK(screen_step(&s, true, true));
    CHECK(!s.off);
}

static void t_dialog_opening_while_dark_draws(void) {
    Screen s;
    screen_init(&s);
    screen_step(&s, true, false);
    CHECK(screen_step(&s, true, true));
    CHECK(!s.off);
}

int main(void) {
    RUN(t_awake_draws_every_frame);
    RUN(t_dimming_draws_one_black_frame_then_stops);
    RUN(t_waking_draws_again);
    RUN(t_open_dialog_keeps_rendering);
    RUN(t_dialog_opening_while_dark_draws);
    return TEST_RESULT();
}
