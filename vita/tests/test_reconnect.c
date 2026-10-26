#include "reconnect.h"
#include "test.h"

static void t_failures_back_off(void) {
    Reconnect r;
    reconnect_init(&r);
    CHECK(reconnect_after_failure(&r) == 1);
    CHECK(reconnect_after_failure(&r) == 2);
    CHECK(reconnect_after_failure(&r) == 4);
    CHECK(reconnect_after_failure(&r) == 8);
    CHECK(reconnect_after_failure(&r) == 10);
    CHECK(reconnect_after_failure(&r) == 10);
}

static void t_short_session_counts_as_failure(void) {
    Reconnect r;
    reconnect_init(&r);
    /* broker accepts then drops at once: must not reconnect in a tight loop */
    CHECK(reconnect_after_session(&r, 200) == 1);
    CHECK(reconnect_after_session(&r, 200) == 2);
    CHECK(reconnect_after_session(&r, 200) == 4);
}

static void t_long_session_resets(void) {
    Reconnect r;
    reconnect_init(&r);
    reconnect_after_failure(&r);
    reconnect_after_failure(&r);
    CHECK(reconnect_after_session(&r, 60000) == 0);
    CHECK(reconnect_after_failure(&r) == 1);
}

int main(void) {
    RUN(t_failures_back_off);
    RUN(t_short_session_counts_as_failure);
    RUN(t_long_session_resets);
    return TEST_RESULT();
}
