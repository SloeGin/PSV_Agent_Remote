#pragma once

#include <stddef.h>

#include "model.h"

#define PROTOCOL_MAX_PAYLOAD 131072

typedef enum { TOPIC_OTHER, TOPIC_STATUS, TOPIC_REQUEST, TOPIC_TURN } TopicKind;

TopicKind protocol_parse_topic(const char *topic, char sid[64], char rid[24]);

bool protocol_parse_status(const char *json, size_t len, StatusMsg *out);

/* On success out->body is malloc'd and owned by the caller. On failure nothing
 * is left allocated and out->body is NULL. */
bool protocol_parse_request(const char *json, size_t len, Request *out);

#define TOPIC_PSV_ONLINE "claude/psv/online"

/* True for a retained "offline" on our presence topic. A stale will from an
 * earlier connection can land after we came online, so we must re-assert. */
bool protocol_is_offline_flag(const char *topic, const uint8_t *payload, size_t len);

/* items is calloc'd (free with turn_free); unknown kinds are skipped. */
bool protocol_parse_turn(const char *json, size_t len, TurnItem **items, int *n);

/* Response builders. Each returns the length written, or -1 if it does not fit. */
int protocol_build_approval(const char *rid, const char *choice, const char *note, char *out, size_t cap);
int protocol_build_answers(const Request *r, char *out, size_t cap);
int protocol_build_mac(const char *rid, char *out, size_t cap);
int protocol_build_response(const char *rid, const char *choice, char *out, size_t cap);
