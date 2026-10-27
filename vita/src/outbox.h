#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OUTBOX_CAP 64

/* Messages waiting to be published. QoS 1 messages stay queued after sending
 * until the broker acknowledges them, and go out again after a reconnect.
 * Not locked: the caller serialises access. */
typedef struct {
    unsigned seq; /* stable handle while the message is queued */
    char topic[160];
    uint8_t *payload;
    size_t len;
    int qos, retain;
    bool sent;
    unsigned short packet_id;
} OutMsg;

typedef struct {
    OutMsg q[OUTBOX_CAP]; /* oldest first */
    int count;
    unsigned next_seq;
} Outbox;

void outbox_init(Outbox *o);
void outbox_free(Outbox *o);

/* Copies payload; drops the oldest message when full. A retained message
 * replaces any older queued message on the same topic, sent-but-unacked ones
 * included, so a withdrawn retained message is never resent. Returns the new seq. */
unsigned outbox_push(Outbox *o, const char *topic, const void *payload, size_t len, int qos, int retain);

const OutMsg *outbox_next_unsent(const Outbox *o);

/* After a successful send: QoS 0 messages leave, QoS 1 wait for packet_id's ack. */
void outbox_mark_sent(Outbox *o, unsigned seq, unsigned short packet_id);
void outbox_ack(Outbox *o, unsigned short packet_id);

/* New connection: everything not acknowledged is sent again. */
void outbox_requeue_all(Outbox *o);
