#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#else
#include <sys/stat.h>
#endif

#define DEFAULT_PORT 1883

void config_defaults(AppConfig *c) {
    memset(c, 0, sizeof *c);
    strcpy(c->brokers[0].host, "192.168.1.20");
    c->brokers[0].port = DEFAULT_PORT;
    c->n_brokers = 1;
    c->active = 0;
    c->sound = 1;
    c->front_touch = 1;
    c->dim_after_s = 300;
    c->mode = MODE_SOFA;
}

/* Parse a base-10 integer filling all of [s, end); false on junk or overflow. */
static bool parse_int(const char *s, const char *end, long lo, long hi, long *out) {
    if (s == end) return false;
    long v = 0;
    for (const char *p = s; p < end; p++) {
        if (!isdigit((unsigned char)*p)) return false;
        v = v * 10 + (*p - '0');
        if (v > hi) return false;
    }
    if (v < lo) return false;
    *out = v;
    return true;
}

static void trim(const char **s, const char **end) {
    while (*s < *end && isspace((unsigned char)**s)) (*s)++;
    while (*end > *s && isspace((unsigned char)(*end)[-1])) (*end)--;
}

static bool broker_parse_range(const char *s, const char *end, Broker *out) {
    trim(&s, &end);
    bool tls = end - s >= 6;
    for (int i = 0; tls && i < 6; i++) /* any case: TLS:// must not turn into plaintext */
        tls = (s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]) == "tls://"[i];
    if (tls) s += 6;
    const char *colon = memchr(s, ':', (size_t)(end - s));
    const char *host_end = colon ? colon : end;
    long port = tls ? DEFAULT_TLS_PORT : DEFAULT_PORT;
    if (colon && !parse_int(colon + 1, end, 1, 65535, &port)) return false;

    int parts = 0;
    const char *p = s;
    while (p <= host_end) {
        const char *dot = p;
        while (dot < host_end && *dot != '.') dot++;
        long octet;
        if (!parse_int(p, dot, 0, 255, &octet) || dot - p > 3) return false;
        parts++;
        p = dot + 1;
        if (dot == host_end) break;
    }
    size_t len = (size_t)(host_end - s);
    if (parts != 4 || len >= sizeof out->host) return false;
    memcpy(out->host, s, len);
    out->host[len] = '\0';
    out->port = (int)port;
    out->tls = tls;
    return true;
}

bool broker_parse(const char *s, Broker *out) {
    return broker_parse_range(s, s + strlen(s), out);
}

BrokerAdd config_add_broker(AppConfig *c, const Broker *b) {
    for (int i = 0; i < c->n_brokers; i++)
        if (strcmp(c->brokers[i].host, b->host) == 0 && c->brokers[i].port == b->port) {
            if (c->brokers[i].tls == b->tls) return BROKER_EXISTS;
            c->brokers[i] = *b;
            return BROKER_REPLACED;
        }
    if (c->n_brokers >= MAX_BROKERS) return BROKER_FULL;
    c->brokers[c->n_brokers++] = *b;
    return BROKER_ADDED;
}

int broker_try_order(const Broker *b, int n, int active, int *order) {
    if (n <= 0) return 0;
    if (active < 0 || active >= n) active = 0;
    int count = 0;
    for (int k = 0; k < n; k++) {
        int i = (active + k) % n;
        if (!b[active].tls || b[i].tls) order[count++] = i;
    }
    return count;
}

bool config_remove_broker(AppConfig *c, int idx) {
    if (c->n_brokers <= 1 || idx < 0 || idx >= c->n_brokers) return false;
    memmove(&c->brokers[idx], &c->brokers[idx + 1], sizeof(Broker) * (size_t)(c->n_brokers - idx - 1));
    c->n_brokers--;
    if (c->active > idx || c->active >= c->n_brokers) c->active = c->active > 0 ? c->active - 1 : 0;
    return true;
}

void config_parse(AppConfig *c, const char *text) {
    AppConfig parsed = *c;
    bool saw_broker = false;
    long active = c->active;
    const char *line = text;
    while (*line) {
        const char *eol = strchr(line, '\n');
        if (!eol) eol = line + strlen(line);
        const char *s = line, *end = eol;
        trim(&s, &end);
        const char *eq = memchr(s, '=', (size_t)(end - s));
        if (s < end && *s != '#' && eq) {
            const char *k = s, *kend = eq, *v = eq + 1, *vend = end;
            trim(&k, &kend);
            trim(&v, &vend);
            size_t klen = (size_t)(kend - k);
            long n;
            Broker b;
            if (klen == 6 && !memcmp(k, "broker", 6)) {
                bool ok = broker_parse_range(v, vend, &b);
                /* A mistyped tls:// entry must not leave the plaintext default in its place. */
                bool meant_tls = vend - v >= 3 && (v[0] | 32) == 't' && (v[1] | 32) == 'l' && (v[2] | 32) == 's';
                if ((ok || meant_tls) && !saw_broker) { parsed.n_brokers = 0; saw_broker = true; }
                if (ok) config_add_broker(&parsed, &b);
            } else if (klen == 6 && !memcmp(k, "active", 6)) {
                if (parse_int(v, vend, 0, 1000, &n)) active = n;
            } else if (klen == 5 && !memcmp(k, "sound", 5)) {
                if (parse_int(v, vend, 0, 1, &n)) parsed.sound = (int)n;
            } else if (klen == 11 && !memcmp(k, "front_touch", 11)) {
                if (parse_int(v, vend, 0, 1, &n)) parsed.front_touch = (int)n;
            } else if (klen == 11 && !memcmp(k, "dim_after_s", 11)) {
                if (parse_int(v, vend, 0, 86400, &n)) parsed.dim_after_s = (int)n;
            } else if (klen == 4 && !memcmp(k, "mode", 4)) {
                if (vend - v == 4 && !memcmp(v, "sofa", 4)) parsed.mode = MODE_SOFA;
                else if (vend - v == 4 && !memcmp(v, "desk", 4)) parsed.mode = MODE_DESK;
            }
        }
        line = *eol ? eol + 1 : eol;
    }
    parsed.active = (active >= 0 && active < parsed.n_brokers) ? (int)active : 0;
    *c = parsed;
}

int config_serialize(const AppConfig *c, char *out, size_t cap) {
    size_t used = 0;
    int n;
#define EMIT(...)                                                    \
    do {                                                             \
        n = snprintf(out + used, cap > used ? cap - used : 0, __VA_ARGS__); \
        if (n < 0 || (size_t)n >= cap - used) return -1;             \
        used += (size_t)n;                                           \
    } while (0)
    EMIT("# PSV Agent Remote\n");
    for (int i = 0; i < c->n_brokers; i++) EMIT("broker=%s%s:%d\n", c->brokers[i].tls ? "tls://" : "", c->brokers[i].host, c->brokers[i].port);
    EMIT("active=%d\nsound=%d\nfront_touch=%d\ndim_after_s=%d\nmode=%s\n", c->active, c->sound,
         c->front_touch, c->dim_after_s, c->mode == MODE_DESK ? "desk" : "sofa");
#undef EMIT
    return (int)used;
}

bool config_load(AppConfig *c, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[2048];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    config_parse(c, buf);
    return true;
}

static void make_parent_dirs(const char *path) {
    char dir[256];
    snprintf(dir, sizeof dir, "%s", path);
    for (char *p = dir; *p; p++) {
        if (*p != '/' || p == dir || p[-1] == ':') continue;
        *p = '\0';
#ifdef __vita__
        sceIoMkdir(dir, 0777);
#else
        mkdir(dir, 0777);
#endif
        *p = '/';
    }
}

bool config_save(const AppConfig *c, const char *path) {
    char buf[2048];
    int n = config_serialize(c, buf, sizeof buf);
    if (n < 0) return false;
    make_parent_dirs(path);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n;
    return fclose(f) == 0 && ok;
}

static bool exists(const char *path) {
#ifdef __vita__
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

bool config_migrate_dir(const char *old_dir, const char *new_dir) {
    if (exists(new_dir) || !exists(old_dir)) return false;
#ifdef __vita__
    return sceIoRename(old_dir, new_dir) >= 0;
#else
    return rename(old_dir, new_dir) == 0;
#endif
}

void broker_key(const Broker *b, char out[64]) {
    snprintf(out, 64, "%s%s:%d", b->tls ? "tls://" : "", b->host, b->port);
}
