#include "test.h"
#include "view.h"

static void t_new_request_starts_at_top(void) {
    ScrollState s = {0};
    scroll_update(&s, "A", "r1", 0, 100, 10);
    CHECK(scroll_update(&s, "A", "r1", 40, 100, 10) == 40);
    /* r1 cleared by a message, r2 shown: no input this frame */
    CHECK(scroll_update(&s, "A", "r2", 0, 100, 10) == 0);
    CHECK(scroll_update(&s, "B", "r2", 5, 100, 10) == 0); /* different session, same rid */
}

static void t_clamps(void) {
    ScrollState s = {0};
    scroll_update(&s, "A", "r1", 0, 25, 10);
    CHECK(scroll_update(&s, "A", "r1", 100, 25, 10) == 15);
    CHECK(scroll_update(&s, "A", "r1", -100, 25, 10) == 0);
    CHECK(scroll_update(&s, "A", "r1", 3, 5, 10) == 0); /* fits on one page */
}

static void t_clip_rect_normalized(void) {
    ClipRect r = clip_rect(10, 644, 950, 504, 960, 544); /* sheet still sliding in: top below bottom */
    CHECK(r.x0 == 10 && r.x1 == 950);
    CHECK(r.y0 <= r.y1 && r.y0 >= 0 && r.y1 <= 544);
    CHECK(r.y0 == r.y1);                                  /* empty, so nothing draws */
    r = clip_rect(-20, -5, 2000, 900, 960, 544);
    CHECK(r.x0 == 0 && r.y0 == 0 && r.x1 == 960 && r.y1 == 544);
    r = clip_rect(0, 44, 960, 514, 960, 544);             /* ordinary rect is unchanged */
    CHECK(r.x0 == 0 && r.y0 == 44 && r.x1 == 960 && r.y1 == 514);
    r = clip_rect(700, 10, 600, 20, 960, 544);            /* x inverted */
    CHECK(r.x0 <= r.x1);
}

static void t_focus_default_first_visible(void) {
    bool v[] = {false, true, true};
    CHECK(table_focus_pick(v, 3, -1, 0) == 1);
}

static void t_focus_keeps_current(void) {
    bool v[] = {true, true, false};
    CHECK(table_focus_pick(v, 3, 1, 0) == 1);
}

static void t_focus_moves_when_scrolled_off(void) {
    bool v[] = {true, false, true};
    CHECK(table_focus_pick(v, 3, 1, 0) == 0);
}

static void t_focus_next_prev(void) {
    bool v[] = {true, false, true};
    CHECK(table_focus_pick(v, 3, 0, 1) == 2);
    CHECK(table_focus_pick(v, 3, 2, 1) == 2);
    CHECK(table_focus_pick(v, 3, 2, -1) == 0);
}

static void t_focus_none(void) {
    bool v[] = {false, false};
    CHECK(table_focus_pick(v, 2, 0, 0) == -1);
}

static void t_pan_clamp(void) {
    CHECK(table_pan_clamp(-5, 500, 300) == 0);
    CHECK(table_pan_clamp(250, 500, 300) == 200);
    CHECK(table_pan_clamp(10, 200, 300) == 0);
}

static void t_battery_levels(void) {
    CHECK(battery_level(100) == BAT_OK);
    CHECK(battery_level(31) == BAT_OK);
    CHECK(battery_level(30) == BAT_LOW);
    CHECK(battery_level(11) == BAT_LOW);
    CHECK(battery_level(10) == BAT_CRITICAL);
    CHECK(battery_level(0) == BAT_CRITICAL);
    CHECK(battery_level(-1) == BAT_UNKNOWN); /* scePower returns < 0 on error */
    CHECK(battery_level(101) == BAT_UNKNOWN);
}

static void t_power_edges_switch_mode(void) {
    PowerWatch w = {0};
    CHECK(power_watch(&w, true) == POWER_NO_CHANGE); /* first sample: keep the saved mode */
    CHECK(power_watch(&w, true) == POWER_NO_CHANGE);
    CHECK(power_watch(&w, false) == POWER_TO_SOFA);  /* unplugged */
    CHECK(power_watch(&w, false) == POWER_NO_CHANGE);
    CHECK(power_watch(&w, true) == POWER_TO_DESK);   /* plugged in */
    CHECK(power_watch(&w, true) == POWER_NO_CHANGE); /* a manual toggle in between stays */
}

static void t_broker_switched(void) {
    char last[64] = "";
    Broker home = {"192.168.1.20", 1883, false}, office = {"10.0.0.5", 8883, true};
    CHECK(!broker_switched(last, &home));   /* first broker: nothing to drop */
    CHECK(!broker_switched(last, &home));   /* reconnecting to the same one keeps sessions */
    CHECK(broker_switched(last, &office));  /* a different broker drops them */
    CHECK(!broker_switched(last, &office));
    CHECK(!broker_switched(last, NULL));    /* no broker configured: nothing to compare */
    CHECK(broker_switched(last, &home));
}

int main(void) {
    RUN(t_clip_rect_normalized);
    RUN(t_broker_switched);
    RUN(t_new_request_starts_at_top);
    RUN(t_clamps);
    RUN(t_focus_default_first_visible);
    RUN(t_focus_keeps_current);
    RUN(t_focus_moves_when_scrolled_off);
    RUN(t_focus_next_prev);
    RUN(t_focus_none);
    RUN(t_pan_clamp);
    RUN(t_battery_levels);
    RUN(t_power_edges_switch_mode);
    return TEST_RESULT();
}
