# Host tests for the platform-independent parts of the Vita app.
#   make -C vita/tests            build and run every test_*.c
#   make -C vita/tests mqtt_smoke build the broker smoke test (run it by hand)

CC ?= cc
SAN = -fsanitize=address,undefined
INCLUDES = -I../src -I../third_party/cJSON -I../third_party/MQTTPacket
CFLAGS = -std=c11 -Wall -Wextra -Werror -g $(SAN) $(INCLUDES)
# Vendored code is compiled as-is, without -Werror.
VENDOR_CFLAGS = -std=c11 -g -w $(SAN) $(INCLUDES)
LDFLAGS = $(SAN)

PURE = util config protocol model textlayout input settings view outbox reconnect sessionlist markdown psk
SRCS = $(wildcard $(addprefix ../src/,$(addsuffix .c,$(PURE))))
VENDOR_OBJS = cJSON.o
MQTT_SRCS = ../src/mqtt_client.c ../src/util.c ../src/psk.c
MQTT_VENDOR = $(wildcard ../third_party/MQTTPacket/*.c)

TESTS = $(basename $(wildcard test_*.c))

all: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done; echo "ALL PASS"

cJSON.o: ../third_party/cJSON/cJSON.c
	$(CC) $(VENDOR_CFLAGS) -c -o $@ $<

mqttpacket.a: $(MQTT_VENDOR)
	rm -f $@ && for f in $(MQTT_VENDOR); do $(CC) $(VENDOR_CFLAGS) -c -o $$(basename $$f .c).o $$f; done
	ar rcs $@ $(notdir $(MQTT_VENDOR:.c=.o))

test_%: test_%.c $(SRCS) $(VENDOR_OBJS) test.h
	$(CC) $(CFLAGS) -o $@ $< $(SRCS) $(VENDOR_OBJS) $(LDFLAGS)

test_mqtt: test_mqtt.c $(MQTT_SRCS) mqttpacket.a test.h
	$(CC) $(CFLAGS) -o $@ $< $(MQTT_SRCS) mqttpacket.a $(LDFLAGS)

mqtt_smoke: mqtt_smoke.c $(MQTT_SRCS) mqttpacket.a
	$(CC) $(CFLAGS) -o $@ $< $(MQTT_SRCS) mqttpacket.a $(LDFLAGS)

clean:
	rm -rf $(TESTS) mqtt_smoke *.o *.a *.dSYM

.PHONY: all clean
