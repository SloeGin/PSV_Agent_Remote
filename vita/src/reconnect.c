#include "reconnect.h"

#define MAX_BACKOFF_S 10
#define STABLE_SESSION_MS 10000

void reconnect_init(Reconnect *r) { r->backoff_s = 1; }

int reconnect_after_failure(Reconnect *r) {
    int delay = r->backoff_s;
    r->backoff_s = r->backoff_s * 2 > MAX_BACKOFF_S ? MAX_BACKOFF_S : r->backoff_s * 2;
    return delay;
}

int reconnect_after_session(Reconnect *r, uint64_t session_ms) {
    if (session_ms < STABLE_SESSION_MS) return reconnect_after_failure(r);
    r->backoff_s = 1;
    return 0;
}
