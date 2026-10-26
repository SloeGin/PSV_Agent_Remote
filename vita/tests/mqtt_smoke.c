/* Integration test against a real broker: MQTT_HOST (default 192.168.2.222). */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mqtt_client.h"

#define BIG 70000

static size_t got_len = 0;
static char big_topic[128];

static void on_msg(const char *topic, const uint8_t *payload, size_t len, void *ud) {
    (void)ud;
    if (!strcmp(topic, big_topic) && len == BIG && payload[0] == 'x' && payload[BIG - 1] == 'x') got_len = len;
}

int main(void) {
    const char *host = getenv("MQTT_HOST") ? getenv("MQTT_HOST") : "192.168.2.222";
    srand((unsigned)time(NULL));
    char base[64];
    snprintf(base, sizeof base, "claude/test/smoke%06d", rand() % 1000000);
    snprintf(big_topic, sizeof big_topic, "%s/big", base);

    MqttClient *c = mqtt_create(host, 1883, "psv-smoke");
    if (!c || mqtt_connect(c, "claude/test/will", "0", 0, 30) != 0) {
        fprintf(stderr, "connect to %s failed\n", host);
        return 1;
    }
    char filter[80];
    snprintf(filter, sizeof filter, "%s/#", base);
    if (mqtt_subscribe(c, filter, 0) != 0) return 1;

    char *payload = malloc(BIG);
    memset(payload, 'x', BIG);
    int id = mqtt_publish(c, big_topic, payload, BIG, 1, 1);
    if (id <= 0) return 1;
    free(payload);

    bool acked = false;
    for (int i = 0; i < 100 && !(got_len && acked); i++) {
        if (mqtt_acked(c, (unsigned short)id)) acked = true;
        if (mqtt_poll(c, 50, on_msg, NULL) < 0) {
            fprintf(stderr, "poll failed\n");
            return 1;
        }
    }
    if (!acked) {
        fprintf(stderr, "no PUBACK for packet %d\n", id);
        return 1;
    }
    mqtt_publish(c, big_topic, "", 0, 1, 1); /* clear the retained message */
    for (int i = 0; i < 4; i++) mqtt_poll(c, 50, NULL, NULL);
    mqtt_destroy(c);
    if (got_len != BIG) {
        fprintf(stderr, "did not receive the %d-byte message (got %zu)\n", BIG, got_len);
        return 1;
    }
    puts("SMOKE PASS");
    return 0;
}
