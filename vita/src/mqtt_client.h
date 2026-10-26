#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Minimal MQTT 3.1.1 client over a plain TCP socket (POSIX/newlib API).
 * Not thread-safe: use one client from one thread. */

typedef void (*MqttOnMessage)(const char *topic, const uint8_t *payload, size_t len, void *ud);

typedef struct MqttClient MqttClient;

struct Psk;

typedef enum { MQTT_ERR_NONE, MQTT_ERR_UNREACHABLE, MQTT_ERR_KEY, MQTT_ERR_TLS } MqttError;

/* host must be an IPv4 literal. */
MqttClient *mqtt_create(const char *host, int port, const char *client_id);

/* With a psk: TLS 1.2 with that pre-shared key, never plaintext. The key is
 * copied into the client and wiped on destroy. NULL psk = mqtt_create. NULL
 * when TLS is not compiled in (host builds). */
MqttClient *mqtt_create_tls(const char *host, int port, const char *client_id, const struct Psk *psk);

/* Why the last mqtt_connect failed. */
MqttError mqtt_last_error(const MqttClient *c);

/* 0 on success. TCP connect and CONNACK each time out after 3 s. */
int mqtt_connect(MqttClient *c, const char *will_topic, const char *will_payload, int will_retain,
                 int keepalive_s);

int mqtt_subscribe(MqttClient *c, const char *filter, int qos);
/* Returns the packet id for QoS 1 (check it with mqtt_acked), 0 for QoS 0, < 0 on error. */
int mqtt_publish(MqttClient *c, const char *topic, const void *payload, size_t len, int qos, int retain);

/* True once the broker has acknowledged packet id (consumes the ack). */
bool mqtt_acked(MqttClient *c, unsigned short id);

/* CONNACK return code (0 = accepted), or -1 if pkt is not a valid CONNACK. */
int mqtt_connack_rc(const uint8_t *pkt, size_t len);

/* Process bytes as if they had arrived on the socket (host tests only). */
void mqtt_feed_for_test(MqttClient *c, const uint8_t *data, size_t len);

/* Waits up to timeout_ms for data, dispatches every complete PUBLISH to cb,
 * and sends keepalive pings. Returns < 0 once the connection is unusable. */
int mqtt_poll(MqttClient *c, int timeout_ms, MqttOnMessage cb, void *ud);

void mqtt_destroy(MqttClient *c);
