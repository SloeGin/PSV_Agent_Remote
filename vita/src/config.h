#pragma once

#include <stdbool.h>
#include <stddef.h>

#define MAX_BROKERS 8
#define DATA_DIR "ux0:data/psv_agent_remote"
#define OLD_DATA_DIR "ux0:data/psv_claude_remote" /* before the rename to PSV Agent Remote */
#define CONFIG_PATH DATA_DIR "/config.txt"

typedef enum { MODE_SOFA = 0, MODE_DESK = 1 } Mode;

#define DEFAULT_TLS_PORT 8883

typedef struct {
    char host[40];
    int port;
    bool tls; /* "tls://" in config.txt: TLS-PSK only, never plaintext */
} Broker;

typedef struct {
    Broker brokers[MAX_BROKERS];
    int n_brokers;
    int active;
    int sound;
    int front_touch; /* 0 = front panel ignored; the rear always scrolls */
    int dim_after_s;
    Mode mode;
} AppConfig;

void config_defaults(AppConfig *c);
/* "tls://host:port" or "host:port": which broker this is, for noticing a switch. */
void broker_key(const Broker *b, char out[64]);

/* Accepts "a.b.c.d" or "a.b.c.d:port" (port defaults to 1883), optionally
 * prefixed "tls://" (port then defaults to 8883). */
bool broker_parse(const char *s, Broker *out);

typedef enum { BROKER_ADDED, BROKER_REPLACED, BROKER_EXISTS, BROKER_FULL } BrokerAdd;
/* The same IP:port is listed once; adding it as the other kind replaces it. */
BrokerAdd config_add_broker(AppConfig *c, const Broker *b);

/* Indices to try, starting at active. An active tls:// broker only fails over
 * to other tls:// brokers; a plaintext one tries them all. Returns the count. */
int broker_try_order(const Broker *b, int n, int active, int *order);
bool config_remove_broker(AppConfig *c, int idx);

void config_parse(AppConfig *c, const char *text);
int config_serialize(const AppConfig *c, char *out, size_t cap);

bool config_load(AppConfig *c, const char *path);

/* Move old_dir to new_dir when only the old one exists (config.txt, psk.txt
 * and the rest come along). True if it moved; never overwrites new_dir. */
bool config_migrate_dir(const char *old_dir, const char *new_dir);
bool config_save(const AppConfig *c, const char *path);
