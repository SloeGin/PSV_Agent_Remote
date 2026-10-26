#include "psk.h"

#include <stdio.h>
#include <string.h>

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void wipe(void *p, size_t n) {
    volatile unsigned char *b = p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool psk_parse(const char *text, Psk *out) {
    const char *s = text;
    while (space(*s)) s++;
    const char *e = s;
    while (*e && *e != '\n') e++;
    while (e > s && space(e[-1])) e--;
    const char *colon = memchr(s, ':', (size_t)(e - s));
    if (!colon || colon == s || (size_t)(colon - s) >= sizeof out->identity) return false;
    const char *h = colon + 1;
    if (e - h != 2 * PSK_KEY_LEN) return false;
    Psk p;
    memcpy(p.identity, s, (size_t)(colon - s));
    p.identity[colon - s] = 0;
    for (int i = 0; i < PSK_KEY_LEN; i++) {
        int a = hex(h[2 * i]), b = hex(h[2 * i + 1]);
        if (a < 0 || b < 0) {
            psk_wipe(&p);
            return false;
        }
        p.key[i] = (unsigned char)(a << 4 | b);
    }
    *out = p;
    psk_wipe(&p);
    return true;
}

bool psk_load(const char *path, Psk *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[256];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    bool ok = psk_parse(buf, out);
    wipe(buf, sizeof buf);
    return ok;
}

void psk_wipe(Psk *p) { wipe(p, sizeof *p); }
