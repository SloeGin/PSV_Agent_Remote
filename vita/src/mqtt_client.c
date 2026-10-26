#include "mqtt_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "MQTTPacket.h"
#include "psk.h"

#ifdef MQTT_TLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <psa/crypto.h>
#include <psp2/kernel/rng.h>
#endif

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#else
#include <time.h>
#endif

#define RBUF_CAP (192 * 1024)
#define MAX_ACKS 16
#define IO_TIMEOUT_MS 3000

struct MqttClient {
    int sock;
    struct sockaddr_in addr;
    char client_id[32];
    uint8_t *rbuf;
    size_t rlen;
    size_t discard; /* bytes still to drop from an oversized packet */
    int keepalive_s;
    uint64_t last_send_ms;
    int pings_outstanding;
    unsigned short next_id;
    int connack_rc; /* -1 until a CONNACK arrives */
    unsigned short acks[MAX_ACKS];
    int n_acks;
    MqttError err;
    bool tls;
    Psk psk;
#ifdef MQTT_TLS
    bool tls_ready; /* ssl/conf/drbg initialised and need freeing */
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context drbg;
#endif
};

static uint64_t mono_ms(void) {
#ifdef __vita__
    return sceKernelGetProcessTimeWide() / 1000;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

static int set_nonblocking(int sock, bool on) {
#ifdef __vita__
    int v = on;
    return setsockopt(sock, SOL_SOCKET, SO_NONBLOCK, &v, sizeof v);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    return fcntl(sock, F_SETFL, on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
#endif
}

static int wait_fd(int sock, bool for_write, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    return select(sock + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &tv);
}

/* ---------- transport: plain socket or TLS ---------- */

#define IO_AGAIN (-2)

static ssize_t io_send(MqttClient *c, const uint8_t *buf, size_t len) {
#ifdef MQTT_TLS
    if (c->tls) {
        int n = mbedtls_ssl_write(&c->ssl, buf, len);
        if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) return IO_AGAIN;
        return n > 0 ? n : -1;
    }
#endif
    ssize_t n = send(c->sock, buf, len, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return IO_AGAIN;
    return n > 0 ? n : -1;
}

static ssize_t io_recv(MqttClient *c, uint8_t *buf, size_t len) {
#ifdef MQTT_TLS
    if (c->tls) {
        int n = mbedtls_ssl_read(&c->ssl, buf, len);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) return IO_AGAIN;
        return n > 0 ? n : -1; /* 0 or close_notify: the broker hung up */
    }
#endif
    ssize_t n = recv(c->sock, buf, len, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return IO_AGAIN;
    return n > 0 ? n : -1;
}

/* Decrypted bytes already buffered by TLS, which select() cannot see. */
static bool io_buffered(MqttClient *c) {
#ifdef MQTT_TLS
    if (c->tls) return mbedtls_ssl_get_bytes_avail(&c->ssl) > 0;
#endif
    (void)c;
    return false;
}

#ifdef MQTT_TLS
static int tls_net_send(void *ctx, const unsigned char *buf, size_t len) {
    ssize_t n = send(*(int *)ctx, buf, len, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return n < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : (int)n;
}

static int tls_net_recv(void *ctx, unsigned char *buf, size_t len) {
    ssize_t n = recv(*(int *)ctx, buf, len, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return MBEDTLS_ERR_SSL_WANT_READ;
    return n < 0 ? MBEDTLS_ERR_NET_RECV_FAILED : (int)n;
}

static int vita_entropy(void *ud, unsigned char *out, size_t len) {
    (void)ud;
    return sceKernelGetRandomNumber(out, (SceSize)len) < 0 ? -1 : 0;
}

static const int PSK_SUITES[] = {
    MBEDTLS_TLS_PSK_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_PSK_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_PSK_WITH_CHACHA20_POLY1305_SHA256,
    0,
};

static void tls_free(MqttClient *c) {
    if (!c->tls_ready) return;
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_ctr_drbg_free(&c->drbg);
    c->tls_ready = false;
}

/* TLS 1.2 PSK handshake on the connected socket. Sets c->err on failure. */
static int tls_handshake(MqttClient *c) {
    tls_free(c);
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_ctr_drbg_init(&c->drbg);
    c->tls_ready = true;
    c->err = MQTT_ERR_TLS;
    static const unsigned char pers[] = "psv-agent-remote";
    if (psa_crypto_init() != PSA_SUCCESS) return -1;
    if (mbedtls_ctr_drbg_seed(&c->drbg, vita_entropy, NULL, pers, sizeof pers - 1)) return -1;
    if (mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT))
        return -1;
    mbedtls_ssl_conf_min_tls_version(&c->conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&c->conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_ciphersuites(&c->conf, PSK_SUITES);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);
    if (mbedtls_ssl_conf_psk(&c->conf, c->psk.key, PSK_KEY_LEN, (const unsigned char *)c->psk.identity,
                             strlen(c->psk.identity)))
        return -1;
    if (mbedtls_ssl_setup(&c->ssl, &c->conf)) return -1;
    mbedtls_ssl_set_bio(&c->ssl, &c->sock, tls_net_send, tls_net_recv, NULL);
    uint64_t deadline = mono_ms() + IO_TIMEOUT_MS;
    for (;;) {
        int r = mbedtls_ssl_handshake(&c->ssl);
        if (r == 0) break;
        if (r == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
            c->err = MQTT_ERR_KEY; /* the broker refused us: with PSK that is the key */
            return -1;
        }
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) return -1;
        int left = (int)(deadline - mono_ms());
        if (left <= 0 || wait_fd(c->sock, r == MBEDTLS_ERR_SSL_WANT_WRITE, left) <= 0) return -1;
    }
    c->err = MQTT_ERR_NONE;
    return 0;
}
#endif

static int send_all(MqttClient *c, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        if (wait_fd(c->sock, true, IO_TIMEOUT_MS) <= 0) return -1;
        ssize_t n = io_send(c, buf + sent, len - sent);
        if (n == IO_AGAIN) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    c->last_send_ms = mono_ms();
    return 0;
}

MqttClient *mqtt_create(const char *host, int port, const char *client_id) {
    MqttClient *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->sock = -1;
    c->addr.sin_family = AF_INET;
    c->addr.sin_port = htons((unsigned short)port);
    c->rbuf = malloc(RBUF_CAP);
    if (!c->rbuf || inet_pton(AF_INET, host, &c->addr.sin_addr) != 1) {
        mqtt_destroy(c);
        return NULL;
    }
    snprintf(c->client_id, sizeof c->client_id, "%s", client_id);
    c->next_id = 1;
    return c;
}

MqttClient *mqtt_create_tls(const char *host, int port, const char *client_id, const Psk *psk) {
    if (!psk) return mqtt_create(host, port, client_id);
#ifdef MQTT_TLS
    MqttClient *c = mqtt_create(host, port, client_id);
    if (!c) return NULL;
    c->tls = true;
    c->psk = *psk;
    return c;
#else
    (void)host;
    (void)port;
    (void)client_id;
    return NULL;
#endif
}

MqttError mqtt_last_error(const MqttClient *c) { return c->err; }

/* Parse the fixed header at buf; returns total packet length, 0 if more bytes are needed. */
static size_t packet_length(const uint8_t *buf, size_t len) {
    size_t rem = 0, mult = 1;
    for (size_t i = 1; i < 5; i++) {
        if (i >= len) return 0;
        rem += (buf[i] & 0x7F) * mult;
        if (!(buf[i] & 0x80)) return 1 + i + rem;
        mult *= 128;
    }
    return SIZE_MAX; /* malformed: more than 4 length bytes */
}

static int on_packet(MqttClient *c, uint8_t *pkt, size_t len, MqttOnMessage cb, void *ud) {
    int type = pkt[0] >> 4;
    if (type == PUBLISH) {
        unsigned char dup, retained;
        int qos, payloadlen;
        unsigned short id;
        MQTTString topic;
        unsigned char *payload;
        if (MQTTDeserialize_publish(&dup, &qos, &retained, &id, &topic, &payload, &payloadlen, pkt, (int)len) != 1)
            return -1;
        char name[256];
        int tlen = topic.lenstring.len < (int)sizeof name - 1 ? topic.lenstring.len : (int)sizeof name - 1;
        memcpy(name, topic.lenstring.data, (size_t)tlen);
        name[tlen] = '\0';
        if (cb) cb(name, payload, (size_t)payloadlen, ud);
        if (qos == 1) {
            uint8_t ack[4];
            int n = MQTTSerialize_puback(ack, sizeof ack, id);
            if (n <= 0 || send_all(c, ack, (size_t)n) < 0) return -1;
        }
    } else if (type == PINGRESP) {
        c->pings_outstanding = 0;
    } else if (type == CONNACK) {
        c->connack_rc = mqtt_connack_rc(pkt, len);
    } else if (type == PUBACK) {
        unsigned char ptype, dup;
        unsigned short id;
        if (MQTTDeserialize_ack(&ptype, &dup, &id, pkt, (int)len) == 1) {
            if (c->n_acks == MAX_ACKS) memmove(c->acks, c->acks + 1, sizeof c->acks[0] * (MAX_ACKS - 1)), c->n_acks--;
            c->acks[c->n_acks++] = id;
        }
    }
    return 0;
}

int mqtt_connack_rc(const uint8_t *pkt, size_t len) {
    unsigned char session_present, rc;
    if (len != 4 || pkt[1] != 2) return -1; /* Paho does not check the length */
    if (MQTTDeserialize_connack(&session_present, &rc, (unsigned char *)pkt, (int)len) != 1) return -1;
    return rc;
}

bool mqtt_acked(MqttClient *c, unsigned short id) {
    for (int i = 0; i < c->n_acks; i++) {
        if (c->acks[i] != id) continue;
        memmove(c->acks + i, c->acks + i + 1, sizeof c->acks[0] * (size_t)(c->n_acks - i - 1));
        c->n_acks--;
        return true;
    }
    return false;
}

/* Buffer received bytes and dispatch complete packets; 1 if want_type was seen. */
static int ingest(MqttClient *c, const uint8_t *tmp, ssize_t n, MqttOnMessage cb, void *ud, int want_type) {
    size_t off = 0;
    if (c->discard) {
        size_t d = (size_t)n < c->discard ? (size_t)n : c->discard;
        c->discard -= d;
        off = d;
    }
    int found = 0;
    while (off < (size_t)n) {
        size_t take = (size_t)n - off;
        if (c->rlen + take > RBUF_CAP) take = RBUF_CAP - c->rlen;
        memcpy(c->rbuf + c->rlen, tmp + off, take);
        c->rlen += take;
        off += take;
        for (;;) {
            size_t total = packet_length(c->rbuf, c->rlen);
            if (total == SIZE_MAX) return -1;
            if (total == 0) break;
            if (total > RBUF_CAP) {
                /* Too large to hold: drop it and stay in sync. */
                c->discard = total - c->rlen;
                c->rlen = 0;
                break;
            }
            if (total > c->rlen) break;
            if (want_type && (c->rbuf[0] >> 4) == want_type) found = 1;
            if (on_packet(c, c->rbuf, total, cb, ud) < 0) return -1;
            memmove(c->rbuf, c->rbuf + total, c->rlen - total);
            c->rlen -= total;
        }
        if (c->discard && off < (size_t)n) {
            size_t d = (size_t)n - off < c->discard ? (size_t)n - off : c->discard;
            c->discard -= d;
            off += d;
        }
    }
    return found;
}

/* Read what is available (waiting up to timeout_ms) and dispatch complete packets. */
static int pump(MqttClient *c, int timeout_ms, MqttOnMessage cb, void *ud, int want_type) {
    if (!io_buffered(c)) {
        int r = wait_fd(c->sock, false, timeout_ms);
        if (r < 0) return -1;
        if (r == 0) return 0;
    }
    uint8_t tmp[4096];
    ssize_t n = io_recv(c, tmp, sizeof tmp);
    if (n == IO_AGAIN) return 0;
    if (n <= 0) return -1;
    return ingest(c, tmp, n, cb, ud, want_type);
}

void mqtt_feed_for_test(MqttClient *c, const uint8_t *data, size_t len) {
    ingest(c, data, (ssize_t)len, NULL, NULL, 0);
}

int mqtt_connect(MqttClient *c, const char *will_topic, const char *will_payload, int will_retain,
                 int keepalive_s) {
    if (c->sock >= 0) close(c->sock);
    c->rlen = c->discard = 0;
    c->pings_outstanding = 0;
    c->connack_rc = -1;
    c->n_acks = 0;
    c->keepalive_s = keepalive_s;
    c->err = MQTT_ERR_UNREACHABLE;
    c->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (c->sock < 0) return -1;
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(c->sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    set_nonblocking(c->sock, true);
    int r = connect(c->sock, (struct sockaddr *)&c->addr, sizeof c->addr);
    if (r < 0 && errno != EINPROGRESS && errno != EWOULDBLOCK) return -1;
    if (r < 0) {
        if (wait_fd(c->sock, true, IO_TIMEOUT_MS) <= 0) return -1;
        int err = 0;
        socklen_t elen = sizeof err;
        if (getsockopt(c->sock, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err) return -1;
    }
    c->err = MQTT_ERR_TLS;
#ifdef MQTT_TLS
    if (c->tls && tls_handshake(c) < 0) return -1;
#endif
    c->err = c->tls ? MQTT_ERR_TLS : MQTT_ERR_UNREACHABLE; /* until the broker answers CONNECT */

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.clientID.cstring = c->client_id;
    data.keepAliveInterval = (unsigned short)keepalive_s;
    data.cleansession = 1;
    if (will_topic) {
        data.willFlag = 1;
        data.will.topicName.cstring = (char *)will_topic;
        data.will.message.cstring = (char *)will_payload;
        data.will.retained = (unsigned char)will_retain;
        data.will.qos = 1;
    }
    uint8_t buf[512];
    int len = MQTTSerialize_connect(buf, sizeof buf, &data);
    if (len <= 0 || send_all(c, buf, (size_t)len) < 0) return -1;

    uint64_t deadline = mono_ms() + IO_TIMEOUT_MS;
    while (mono_ms() < deadline) {
        int remaining = (int)(deadline - mono_ms());
        int got = pump(c, remaining, NULL, NULL, CONNACK);
        if (got < 0) return -1;
        if (got > 0) {
            /* A TLS broker refusing CONNECT did not accept our identity. */
            c->err = c->connack_rc == 0 ? MQTT_ERR_NONE : c->tls ? MQTT_ERR_KEY : MQTT_ERR_UNREACHABLE;
            return c->connack_rc == 0 ? 0 : -1;
        }
    }
    return -1;
}

int mqtt_subscribe(MqttClient *c, const char *filter, int qos) {
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)filter;
    uint8_t buf[300];
    int len = MQTTSerialize_subscribe(buf, sizeof buf, 0, c->next_id++, 1, &topic, &qos);
    return (len <= 0 || send_all(c, buf, (size_t)len) < 0) ? -1 : 0;
}

int mqtt_publish(MqttClient *c, const char *topic, const void *payload, size_t len, int qos, int retain) {
    MQTTString t = MQTTString_initializer;
    t.cstring = (char *)topic;
    size_t cap = len + strlen(topic) + 16;
    uint8_t *buf = malloc(cap);
    if (!buf) return -1;
    unsigned short id = qos ? c->next_id++ : 0;
    if (c->next_id == 0) c->next_id = 1;
    int n = MQTTSerialize_publish(buf, (int)cap, 0, qos, (unsigned char)retain, id, t,
                                  (unsigned char *)payload, (int)len);
    int r = (n <= 0 || send_all(c, buf, (size_t)n) < 0) ? -1 : id;
    free(buf);
    return r;
}

int mqtt_poll(MqttClient *c, int timeout_ms, MqttOnMessage cb, void *ud) {
    if (c->sock < 0) return -1;
    if (pump(c, timeout_ms, cb, ud, 0) < 0) return -1;
    if (mono_ms() - c->last_send_ms >= (uint64_t)c->keepalive_s * 500) {
        if (c->pings_outstanding >= 2) return -1;
        uint8_t buf[4];
        int len = MQTTSerialize_pingreq(buf, sizeof buf);
        if (len <= 0 || send_all(c, buf, (size_t)len) < 0) return -1;
        c->pings_outstanding++;
    }
    return 0;
}

void mqtt_destroy(MqttClient *c) {
    if (!c) return;
    if (c->sock >= 0) {
        uint8_t buf[4];
        int len = MQTTSerialize_disconnect(buf, sizeof buf);
        if (len > 0) io_send(c, buf, (size_t)len);
#ifdef MQTT_TLS
        if (c->tls_ready) mbedtls_ssl_close_notify(&c->ssl);
#endif
        close(c->sock);
    }
#ifdef MQTT_TLS
    tls_free(c);
#endif
    psk_wipe(&c->psk);
    free(c->rbuf);
    free(c);
}
