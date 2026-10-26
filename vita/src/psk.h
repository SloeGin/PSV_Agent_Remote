#pragma once

/* The pre-shared key for tls:// brokers: psk.txt holds one line
 * "<identity>:<64 hex chars>", copied to the PSV once while pairing. */

#include <stdbool.h>

#define PSK_KEY_LEN 32
#define PSK_PATH "ux0:data/psv_agent_remote/psk.txt"

typedef struct Psk {
    char identity[64];
    unsigned char key[PSK_KEY_LEN];
} Psk;

/* Parses the first non-empty line; surrounding spaces and CR are ignored. */
bool psk_parse(const char *text, Psk *out);
/* False if the file is missing, unreadable or invalid. */
bool psk_load(const char *path, Psk *out);
/* Zero the key and identity. */
void psk_wipe(Psk *p);
