#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

typedef enum { NET_DOWN, NET_CONNECTING, NET_UP } NetStatus;

/* Why a broker could not be used last time it was tried. */
typedef enum { NETERR_NONE, NETERR_UNREACHABLE, NETERR_KEY, NETERR_TLS, NETERR_NO_KEY } NetError;

typedef struct {
    char topic[160];
    uint8_t *payload; /* malloc'd (NUL-terminated); caller frees */
    size_t len;
} InMsg;

/* Starts the network thread: it walks the broker list from cfg->active,
 * publishes online/mode on connect and subscribes to claude/session/#. */
void net_start(const AppConfig *cfg, Mode initial_mode);
void net_stop(void);

/* Restart the connection with a new broker list / active broker. */
void net_set_brokers(const AppConfig *cfg);

/* Publish the mode now (if connected) and on every reconnect. */
void net_set_mode(Mode mode);

bool net_pop(InMsg *out);
void net_publish(const char *topic, const void *payload, size_t len, int qos, int retain);

NetStatus net_status(void);
int net_connected_index(void);
/* Last failure of broker idx (NETERR_NONE once it connects or the list changes). */
NetError net_broker_error(int idx);
