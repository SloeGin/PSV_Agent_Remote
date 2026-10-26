#include "outbox.h"
#include "test.h"

static void t_qos1_kept_until_ack(void) {
    Outbox o;
    outbox_init(&o);
    unsigned a = outbox_push(&o, "t/a", "allow", 5, 1, 0);
    outbox_push(&o, "t/b", "x", 1, 1, 0);
    const OutMsg *m = outbox_next_unsent(&o);
    CHECK(m && m->seq == a);
    CHECK_STR(m->topic, "t/a");
    outbox_mark_sent(&o, a, 5);
    CHECK(o.count == 2);
    CHECK_STR(outbox_next_unsent(&o)->topic, "t/b");
    outbox_ack(&o, 5);
    CHECK(o.count == 1);
    outbox_free(&o);
}

static void t_qos0_removed_when_sent(void) {
    Outbox o;
    outbox_init(&o);
    unsigned a = outbox_push(&o, "t", "", 0, 0, 1);
    outbox_mark_sent(&o, a, 0);
    CHECK(o.count == 0);
    CHECK(outbox_next_unsent(&o) == NULL);
    outbox_free(&o);
}

static void t_unacked_resent_after_reconnect(void) {
    Outbox o;
    outbox_init(&o);
    unsigned a = outbox_push(&o, "t/a", "allow", 5, 1, 0);
    outbox_mark_sent(&o, a, 9);
    CHECK(outbox_next_unsent(&o) == NULL);
    outbox_requeue_all(&o);
    const OutMsg *m = outbox_next_unsent(&o);
    CHECK(m && m->seq == a);
    outbox_ack(&o, 9); /* stale id from the old connection: ignored once requeued */
    CHECK(o.count == 1);
    outbox_free(&o);
}

static void t_failed_send_stays_queued(void) {
    Outbox o;
    outbox_init(&o);
    unsigned a = outbox_push(&o, "t/a", "allow", 5, 1, 0);
    /* publish failed: nothing marked, still the next one to send */
    CHECK(outbox_next_unsent(&o)->seq == a);
    outbox_free(&o);
}

static void t_full_drops_oldest(void) {
    Outbox o;
    outbox_init(&o);
    unsigned first = outbox_push(&o, "t/0", "x", 1, 1, 0);
    for (int i = 1; i <= OUTBOX_CAP; i++) outbox_push(&o, "t/n", "x", 1, 1, 0);
    CHECK(o.count == OUTBOX_CAP);
    CHECK(outbox_next_unsent(&o)->seq != first);
    outbox_mark_sent(&o, first, 3); /* already dropped: harmless */
    outbox_free(&o);
}

int main(void) {
    RUN(t_qos1_kept_until_ack);
    RUN(t_qos0_removed_when_sent);
    RUN(t_unacked_resent_after_reconnect);
    RUN(t_failed_send_stays_queued);
    RUN(t_full_drops_oldest);
    return TEST_RESULT();
}
