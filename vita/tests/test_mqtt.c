#include <stdio.h>

#include "mqtt_client.h"
#include "psk.h"
#include "test.h"

static void t_connack_codes(void) {
    const uint8_t ok[] = {0x20, 0x02, 0x00, 0x00};
    const uint8_t refused_auth[] = {0x20, 0x02, 0x00, 0x05};
    const uint8_t garbage[] = {0x20, 0x01, 0x00};
    CHECK(mqtt_connack_rc(ok, sizeof ok) == 0);
    CHECK(mqtt_connack_rc(refused_auth, sizeof refused_auth) == 5);
    CHECK(mqtt_connack_rc(garbage, sizeof garbage) < 0);
}

static void t_puback_tracking(void) {
    MqttClient *c = mqtt_create("127.0.0.1", 1883, "t");
    CHECK(c != NULL);
    CHECK(!mqtt_acked(c, 7));
    const uint8_t puback[] = {0x40, 0x02, 0x00, 0x07};
    mqtt_feed_for_test(c, puback, sizeof puback);
    CHECK(mqtt_acked(c, 7));
    CHECK(!mqtt_acked(c, 7)); /* consumed */
    mqtt_destroy(c);
}

static void t_last_error_unreachable(void) {
    MqttClient *c = mqtt_create("127.0.0.1", 1, "t");
    CHECK(c != NULL);
    CHECK(mqtt_connect(c, NULL, NULL, 0, 30) != 0);
    CHECK(mqtt_last_error(c) == MQTT_ERR_UNREACHABLE);
    mqtt_destroy(c);
}

static void t_tls_unavailable_on_host(void) {
    Psk psk;
    memset(&psk, 0, sizeof psk);
    snprintf(psk.identity, sizeof psk.identity, "psv");
    CHECK(mqtt_create_tls("127.0.0.1", 8883, "t", &psk) == NULL);
    MqttClient *c = mqtt_create_tls("127.0.0.1", 1883, "t", NULL); /* NULL psk = plain */
    CHECK(c != NULL);
    mqtt_destroy(c);
}

int main(void) {
    RUN(t_connack_codes);
    RUN(t_puback_tracking);
    RUN(t_last_error_unreachable);
    RUN(t_tls_unavailable_on_host);
    return TEST_RESULT();
}
