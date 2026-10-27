#include "outbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void outbox_init(Outbox *o) {
    memset(o, 0, sizeof *o);
    o->next_seq = 1;
}

static void remove_at(Outbox *o, int i) {
    free(o->q[i].payload);
    memmove(&o->q[i], &o->q[i + 1], sizeof(OutMsg) * (size_t)(o->count - i - 1));
    o->count--;
}

void outbox_free(Outbox *o) {
    while (o->count) remove_at(o, 0);
}

unsigned outbox_push(Outbox *o, const char *topic, const void *payload, size_t len, int qos, int retain) {
    uint8_t *copy = malloc(len ? len : 1);
    if (!copy) return 0;
    memcpy(copy, payload, len);
    if (retain) /* the broker keeps only the newest retained message, so older ones are moot */
        for (int i = o->count - 1; i >= 0; i--)
            if (!strcmp(o->q[i].topic, topic)) remove_at(o, i);
    if (o->count == OUTBOX_CAP) remove_at(o, 0);
    OutMsg *m = &o->q[o->count++];
    memset(m, 0, sizeof *m);
    m->seq = o->next_seq++;
    snprintf(m->topic, sizeof m->topic, "%s", topic);
    m->payload = copy;
    m->len = len;
    m->qos = qos;
    m->retain = retain;
    return m->seq;
}

const OutMsg *outbox_next_unsent(const Outbox *o) {
    for (int i = 0; i < o->count; i++)
        if (!o->q[i].sent) return &o->q[i];
    return NULL;
}

void outbox_mark_sent(Outbox *o, unsigned seq, unsigned short packet_id) {
    for (int i = 0; i < o->count; i++) {
        if (o->q[i].seq != seq) continue;
        if (o->q[i].qos == 0) {
            remove_at(o, i);
        } else {
            o->q[i].sent = true;
            o->q[i].packet_id = packet_id;
        }
        return;
    }
}

void outbox_ack(Outbox *o, unsigned short packet_id) {
    for (int i = 0; i < o->count; i++)
        if (o->q[i].sent && o->q[i].packet_id == packet_id) {
            remove_at(o, i);
            return;
        }
}

void outbox_requeue_all(Outbox *o) {
    for (int i = 0; i < o->count; i++) {
        o->q[i].sent = false;
        o->q[i].packet_id = 0;
    }
}
