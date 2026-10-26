#pragma once

#include <stdint.h>

/* Delay policy between broker connection attempts. */
typedef struct {
    int backoff_s;
} Reconnect;

void reconnect_init(Reconnect *r);

/* Every broker in the list failed: wait this many seconds (1, 2, 4, 8, 10, 10, ...). */
int reconnect_after_failure(Reconnect *r);

/* A connected session ended after session_ms. Sessions shorter than 10 s
 * count as failures, so a broker that accepts and drops cannot spin us. */
int reconnect_after_session(Reconnect *r, uint64_t session_ms);
