#include "input.h"
#include "test.h"

static InputFrame idle_frame(uint64_t now) {
    InputFrame f;
    memset(&f, 0, sizeof f);
    f.lx = f.ly = f.rx = f.ry = 128;
    f.now_ms = now;
    return f;
}

static void t_buttons_edge(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.buttons = BTN_CROSS;
    CHECK(input_map(&s, &f).confirm);
    f.now_ms = 16;
    CHECK(!input_map(&s, &f).confirm);
    f.buttons = 0;
    f.now_ms = 32;
    input_map(&s, &f);
    f.buttons = BTN_CROSS;
    f.now_ms = 48;
    CHECK(input_map(&s, &f).confirm);
}

static void t_button_actions(void) {
    struct { uint32_t btn; int which; } cases[] = {
        {BTN_SQUARE, 0}, {BTN_TRIANGLE, 1}, {BTN_START, 2}, {BTN_CIRCLE, 3}, {BTN_SELECT, 4}};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        InputState s;
        input_init(&s);
        InputFrame f = idle_frame(0);
        f.buttons = cases[i].btn;
        Actions a = input_map(&s, &f);
        bool got[] = {a.square, a.toggle_mode, a.settings, a.back, a.sessions};
        for (int k = 0; k < 5; k++) CHECK(got[k] == (k == cases[i].which));
    }
}

static void t_lr_switch(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.buttons = BTN_R;
    CHECK(input_map(&s, &f).session_dir == 1);
    f.buttons = BTN_L;
    f.now_ms = 16;
    CHECK(input_map(&s, &f).session_dir == -1);
}

static void t_left_stick_repeat(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.ly = 255;
    CHECK(input_map(&s, &f).scroll_lines == 1);
    f.now_ms = 60;
    CHECK(input_map(&s, &f).scroll_lines == 0);
    f.now_ms = 130;
    CHECK(input_map(&s, &f).scroll_lines == 1);
    f.ly = 0;
    f.now_ms = 140;
    CHECK(input_map(&s, &f).scroll_lines == -1); /* direction change fires at once */
}

static void t_dpad_left_right_selects(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.buttons = BTN_RIGHT;
    CHECK(input_map(&s, &f).sel_dir == 1);
    f.buttons = BTN_LEFT;
    f.now_ms = 16;
    CHECK(input_map(&s, &f).sel_dir == -1);
}

static void t_dpad_scrolls(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.buttons = BTN_UP;
    CHECK(input_map(&s, &f).scroll_lines == -1);
}

static void t_stick_deadzone(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.ly = 128 + 40;
    f.rx = 128 - 60;
    Actions a = input_map(&s, &f);
    CHECK(a.scroll_lines == 0 && a.sel_dir == 0);
}

static void t_right_stick_pans_not_selects(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.rx = 255;
    Actions a = input_map(&s, &f);
    CHECK(a.pan_px == 18 && a.sel_dir == 0);
    f.rx = 0;
    CHECK(input_map(&s, &f).pan_px == -18);
    f.rx = 140;
    CHECK(input_map(&s, &f).pan_px == 0);
}

static void t_right_stick_vertical_switches_table(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.ry = 255;
    CHECK(input_map(&s, &f).table_dir == 1);
    f.now_ms = 100;
    CHECK(input_map(&s, &f).table_dir == 0);
    f.now_ms = 300;
    CHECK(input_map(&s, &f).table_dir == 1);
}

static void t_left_stick_selects(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.lx = 255;
    Actions a = input_map(&s, &f);
    CHECK(a.sel_dir == 1 && a.scroll_lines == 0);
}

static void t_left_stick_dominant_axis(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.lx = 220;
    f.ly = 250;
    Actions a = input_map(&s, &f);
    CHECK(a.scroll_lines == 1 && a.sel_dir == 0);
}

static void t_front_horizontal_drag_pans(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.front_down = true;
    f.front_x = 400;
    f.front_y = 300;
    input_map(&s, &f);
    f.front_x = 360;
    f.front_y = 302;
    f.now_ms = 16;
    Actions a = input_map(&s, &f);
    CHECK(a.touch_pan_px == 28 && a.touch_x == 400 && a.touch_y == 300 && a.scroll_lines == 0);
    CHECK(actions_any(&a));
    f.front_y = 200; /* locked sideways: vertical travel no longer scrolls */
    f.now_ms = 32;
    a = input_map(&s, &f);
    CHECK(a.scroll_lines == 0 && a.touch_pan_px == 0);
    f.front_down = false;
    f.now_ms = 48;
    CHECK(!input_map(&s, &f).tap);
}

static InputFrame rear(int x, int y, uint64_t now) {
    InputFrame f = idle_frame(now);
    f.rear_down = true;
    f.rear_x = x;
    f.rear_y = y;
    return f;
}

static void t_rear_drag_scrolls_only(void) {
    InputState s;
    input_init(&s);
    InputFrame f = rear(900, 600, 0);
    Actions a = input_map(&s, &f);
    CHECK(!a.tap && a.scroll_lines == 0);
    /* rear y spans 108-889 for 544 screen px; move well past threshold + one line */
    f = rear(900, 600 - 120, 16);
    a = input_map(&s, &f);
    CHECK(a.scroll_lines >= 1);                     /* finger up: read further down */
    f = idle_frame(32);
    a = input_map(&s, &f);
    CHECK(!a.tap);                                  /* the rear never taps */
}

static void t_rear_touch_without_drag_does_nothing(void) {
    InputState s;
    input_init(&s);
    InputFrame f = rear(500, 500, 0);
    input_map(&s, &f);
    f = idle_frame(16);
    Actions a = input_map(&s, &f);
    CHECK(!a.tap && a.scroll_lines == 0);
    CHECK(!actions_any(&a));
}

static void t_front_touch_can_be_disabled(void) {
    InputState s;
    input_init(&s);
    input_set_front_enabled(&s, false);
    InputFrame f = idle_frame(0);
    f.front_down = true;
    f.front_x = 300;
    f.front_y = 500;
    input_map(&s, &f);
    f.front_y = 300;
    f.now_ms = 16;
    CHECK(input_map(&s, &f).scroll_lines == 0);
    f.front_down = false;
    f.now_ms = 32;
    CHECK(!input_map(&s, &f).tap);
}

static void t_start_opens_settings_once(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(1000);
    f.buttons = BTN_START;
    CHECK(input_map(&s, &f).settings);
    f.now_ms = 3000; /* holding START does nothing more: exit lives in settings now */
    Actions a = input_map(&s, &f);
    CHECK(!a.settings && !a.quit);
}

static void t_front_drag_scrolls_no_tap(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.front_down = true;
    f.front_x = 400;
    f.front_y = 400;
    input_map(&s, &f);
    f.front_y = 400 - 12 - 28;
    f.now_ms = 16;
    CHECK(input_map(&s, &f).scroll_lines >= 1); /* finger up → content scrolls down */
    f.front_y = 400 + 100;
    f.now_ms = 24;
    CHECK(input_map(&s, &f).scroll_lines < 0);
    f.front_down = false;
    f.now_ms = 32;
    CHECK(!input_map(&s, &f).tap);
}

static void t_front_tap_on_release(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.front_down = true;
    f.front_x = 300;
    f.front_y = 500;
    CHECK(!input_map(&s, &f).tap);
    f.front_y = 505;
    f.now_ms = 16;
    input_map(&s, &f); /* 5 px jitter is not a drag */
    f.front_down = false;
    f.now_ms = 32;
    Actions a = input_map(&s, &f);
    CHECK(a.tap && a.tap_x == 300 && a.tap_y == 500 && a.scroll_lines == 0);
}

static void t_tap_release_counts_as_action(void) {
    InputState s;
    input_init(&s);
    InputFrame f = idle_frame(0);
    f.front_down = true;
    f.front_x = 300;
    f.front_y = 500;
    input_map(&s, &f);
    f.front_down = false;
    f.now_ms = 16;
    Actions a = input_map(&s, &f);
    CHECK(a.tap);
    CHECK(actions_any(&a)); /* the release frame has no input but must still be handled */
    Actions none;
    memset(&none, 0, sizeof none);
    CHECK(!actions_any(&none));
}

static void t_rear_drag_reports_its_lines(void) {
    InputState s;
    input_init(&s);
    InputFrame f = rear(900, 600, 0);
    input_map(&s, &f);
    f = rear(900, 480, 16); /* finger up on the back */
    Actions a = input_map(&s, &f);
    CHECK(a.scroll_lines >= 1 && a.rear_lines == a.scroll_lines);
    CHECK(actions_cursor_lines(&a) == -a.scroll_lines); /* on the back, finger up moves the highlight up */
    Actions b;
    memset(&b, 0, sizeof b);
    b.scroll_lines = 2;                                 /* stick / front: unchanged */
    CHECK(actions_cursor_lines(&b) == 2);
}

static void t_rear_sideways_drag_pans_where_it_touches(void) {
    InputState s;
    input_init(&s);
    InputFrame f = rear(960, 500, 0);  /* raw x 960 ≈ screen x 480 */
    input_map(&s, &f);
    f = rear(880, 504, 16);            /* 80 raw units left = 40 screen px */
    Actions a = input_map(&s, &f);
    CHECK(a.touch_pan_px == 40 - 12 && a.scroll_lines == 0);
    CHECK(a.touch_x >= 470 && a.touch_x <= 490 && a.touch_y > 0);
    CHECK(!a.tap);
}

static void t_drag_steps(void) {
    int acc = 0;
    CHECK(drag_steps(&acc, -30) == 0); /* finger moved right, not far enough yet */
    CHECK(drag_steps(&acc, -40) == 1); /* 70 px right: one step right */
    CHECK(drag_steps(&acc, -130) == 2);
    CHECK(drag_steps(&acc, 200) == -3); /* finger moved left */
    acc = 0;
    CHECK(drag_steps(&acc, 59) == 0 && drag_steps(&acc, 1) == -1);
}

int main(void) {
    RUN(t_drag_steps);
    RUN(t_tap_release_counts_as_action);
    RUN(t_buttons_edge);
    RUN(t_button_actions);
    RUN(t_lr_switch);
    RUN(t_left_stick_repeat);
    RUN(t_dpad_scrolls);
    RUN(t_dpad_left_right_selects);
    RUN(t_stick_deadzone);
    RUN(t_right_stick_pans_not_selects);
    RUN(t_right_stick_vertical_switches_table);
    RUN(t_left_stick_selects);
    RUN(t_left_stick_dominant_axis);
    RUN(t_front_horizontal_drag_pans);
    RUN(t_rear_drag_scrolls_only);
    RUN(t_rear_touch_without_drag_does_nothing);
    RUN(t_front_touch_can_be_disabled);
    RUN(t_start_opens_settings_once);
    RUN(t_front_drag_scrolls_no_tap);
    RUN(t_front_tap_on_release);
    RUN(t_rear_drag_reports_its_lines);
    RUN(t_rear_sideways_drag_pans_where_it_touches);
    return TEST_RESULT();
}
