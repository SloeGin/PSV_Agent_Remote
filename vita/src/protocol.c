#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "util.h"

#define SESSION_PREFIX "claude/session/"
#define DEFAULT_TIMEOUT_S 120

static const char *const CHOICES[] = {"allow", "allow_always", "deny", "deny_note", "deny_stop"};

/* Copy [s, s+n) into dst[cap] if it is non-empty, fits, and has no '/'. */
static bool take_segment(const char *s, size_t n, char *dst, size_t cap) {
    if (n == 0 || n >= cap || memchr(s, '/', n)) return false;
    memcpy(dst, s, n);
    dst[n] = '\0';
    return true;
}

TopicKind protocol_parse_topic(const char *topic, char sid[64], char rid[24]) {
    size_t plen = strlen(SESSION_PREFIX);
    if (strncmp(topic, SESSION_PREFIX, plen) != 0) return TOPIC_OTHER;
    const char *s = topic + plen;
    const char *slash = strchr(s, '/');
    if (!slash || !take_segment(s, (size_t)(slash - s), sid, 64)) return TOPIC_OTHER;
    const char *rest = slash + 1;
    if (strcmp(rest, "status") == 0) return TOPIC_STATUS;
    if (strcmp(rest, "turn") == 0) return TOPIC_TURN;
    if (strncmp(rest, "request/", 8) == 0) {
        const char *r = rest + 8;
        if (take_segment(r, strlen(r), rid, 24)) return TOPIC_REQUEST;
    }
    return TOPIC_OTHER;
}

static cJSON *parse_object(const char *json, size_t len) {
    if (len == 0 || len > PROTOCOL_MAX_PAYLOAD || !utf8_valid(json, len)) return NULL;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root && !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

/* Copy a string field into dst; missing is "" unless required. */
static bool get_str(const cJSON *obj, const char *key, char *dst, size_t cap, bool required) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item)) {
        dst[0] = '\0';
        return !required && item == NULL;
    }
    utf8_copy(dst, cap, item->valuestring, strlen(item->valuestring));
    return true;
}

bool protocol_parse_status(const char *json, size_t len, StatusMsg *out) {
    cJSON *root = parse_object(json, len);
    if (!root) return false;
    char state[16];
    bool ok = get_str(root, "state", state, sizeof state, true);
    if (ok) {
        if (!strcmp(state, "running")) out->state = ST_RUNNING;
        else if (!strcmp(state, "waiting")) out->state = ST_WAITING;
        else if (!strcmp(state, "idle")) out->state = ST_IDLE;
        else ok = false;
    }
    ok = ok && get_str(root, "project", out->project, sizeof out->project, false);
    ok = ok && get_str(root, "cwd", out->cwd, sizeof out->cwd, false);
    ok = ok && get_str(root, "summary", out->summary, sizeof out->summary, false);
    cJSON_Delete(root);
    return ok;
}

static bool is_choice(const char *id) {
    for (size_t i = 0; i < sizeof CHOICES / sizeof *CHOICES; i++)
        if (!strcmp(id, CHOICES[i])) return true;
    return false;
}

static bool parse_questions(const cJSON *arr, Request *out) {
    int n = cJSON_GetArraySize(arr);
    if (!cJSON_IsArray(arr) || n < 1) return false;
    if (n > MAX_QUESTIONS) n = MAX_QUESTIONS;
    out->questions = calloc((size_t)n, sizeof(Question));
    if (!out->questions) return false;
    for (int i = 0; i < n; i++) {
        const cJSON *q = cJSON_GetArrayItem(arr, i);
        Question *dst = &out->questions[i];
        if (!get_str(q, "question", dst->question, sizeof dst->question, true) ||
            !get_str(q, "header", dst->header, sizeof dst->header, false))
            return false;
        dst->multi = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(q, "multi"));
        const cJSON *o;
        cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(q, "options")) {
            if (dst->n_opts >= MAX_QOPTS) break;
            QOption *qo = &dst->opts[dst->n_opts];
            if (get_str(o, "label", qo->label, sizeof qo->label, true) &&
                get_str(o, "description", qo->desc, sizeof qo->desc, false))
                dst->n_opts++;
        }
        if (dst->n_opts == 0) return false;
    }
    out->n_questions = n;
    return true;
}

bool protocol_parse_request(const char *json, size_t len, Request *out) {
    out->body = NULL;
    out->questions = NULL;
    out->n_questions = 0;
    cJSON *root = parse_object(json, len);
    if (!root) return false;
    char kind[16];
    bool ok = get_str(root, "rid", out->rid, sizeof out->rid, true) &&
              get_str(root, "title", out->title, sizeof out->title, true) &&
              get_str(root, "tool", out->tool, sizeof out->tool, false) &&
              get_str(root, "cwd", out->cwd, sizeof out->cwd, false) &&
              get_str(root, "kind", kind, sizeof kind, false);
    out->kind = ok && !strcmp(kind, "question") ? REQ_QUESTION : REQ_APPROVAL;
    if (ok && out->kind == REQ_QUESTION) {
        ok = parse_questions(cJSON_GetObjectItemCaseSensitive(root, "questions"), out);
    } else if (ok) {
        const cJSON *body = cJSON_GetObjectItemCaseSensitive(root, "body");
        ok = cJSON_IsString(body);
        if (ok) {
            size_t n = strlen(body->valuestring);
            out->body = malloc(n + 1);
            ok = out->body != NULL;
            if (ok) utf8_copy(out->body, n + 1, body->valuestring, n);
        }
        out->n_opts = 0;
        const cJSON *opt;
        cJSON_ArrayForEach(opt, cJSON_GetObjectItemCaseSensitive(root, "options")) {
            if (out->n_opts >= (int)(sizeof out->opts / sizeof *out->opts)) break;
            Option *o = &out->opts[out->n_opts];
            if (get_str(opt, "id", o->id, sizeof o->id, true) && is_choice(o->id) &&
                get_str(opt, "label", o->label, sizeof o->label, true))
                out->n_opts++;
        }
    }
    if (ok) {
        const cJSON *timeout = cJSON_GetObjectItemCaseSensitive(root, "timeout_s");
        double d = cJSON_IsNumber(timeout) ? timeout->valuedouble : 0;
        int t = d >= 1 && d <= 86400 ? (int)d : 0;
        out->timeout_s = t > 0 ? t : DEFAULT_TIMEOUT_S;
        out->sent_choice[0] = '\0';
        out->recv_ms = 0;
        out->step = 0;
        memset(out->picked, 0, sizeof out->picked);
        memset(out->other, 0, sizeof out->other);
    } else {
        request_free(out);
    }
    cJSON_Delete(root);
    return ok;
}

bool protocol_parse_turn(const char *json, size_t len, TurnItem **items, int *n) {
    *items = NULL;
    *n = 0;
    cJSON *root = parse_object(json, len);
    if (!root) return false;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return false;
    }
    int count = cJSON_GetArraySize(arr);
    if (count > MAX_TURN_ITEMS) count = MAX_TURN_ITEMS;
    TurnItem *out = calloc((size_t)(count ? count : 1), sizeof(TurnItem));
    int k = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (!out || k >= count) break;
        char kind[16];
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(it, "t");
        if (!get_str(it, "k", kind, sizeof kind, true) || !cJSON_IsString(t)) continue;
        TurnItem *d = &out[k];
        if (!strcmp(kind, "you")) d->kind = TURN_YOU;
        else if (!strcmp(kind, "claude")) d->kind = TURN_CLAUDE;
        else if (!strcmp(kind, "tool")) d->kind = TURN_TOOL;
        else if (!strcmp(kind, "note")) d->kind = TURN_NOTE;
        else continue;
        get_str(it, "n", d->tool, sizeof d->tool, false);
        size_t tl = strlen(t->valuestring);
        d->text = malloc(tl + 1);
        if (!d->text) continue;
        utf8_copy(d->text, tl + 1, t->valuestring, tl);
        k++;
    }
    cJSON_Delete(root);
    if (!out) return false;
    *items = out;
    *n = k;
    return true;
}

/* Print `obj` compactly into out; -1 if it does not fit. Frees obj. */
static int print_json(cJSON *obj, char *out, size_t cap) {
    int ok = obj && cJSON_PrintPreallocated(obj, out, (int)cap, 0);
    cJSON_Delete(obj);
    return ok ? (int)strlen(out) : -1;
}

int protocol_build_approval(const char *rid, const char *choice, const char *note, char *out, size_t cap) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "rid", rid);
    cJSON_AddStringToObject(o, "choice", choice);
    if (note) cJSON_AddStringToObject(o, "note", note);
    return print_json(o, out, cap);
}

int protocol_build_answers(const Request *r, char *out, size_t cap) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "rid", r->rid);
    cJSON *answers = cJSON_AddArrayToObject(o, "answers");
    for (int q = 0; q < r->n_questions; q++) {
        cJSON *a = cJSON_CreateObject();
        cJSON *sel = cJSON_AddArrayToObject(a, "selected");
        for (int i = 0; i < 8; i++)
            if (r->picked[q] & (1u << i)) cJSON_AddItemToArray(sel, cJSON_CreateNumber(i));
        if (r->other[q][0]) cJSON_AddStringToObject(a, "other", r->other[q]);
        cJSON_AddItemToArray(answers, a);
    }
    return print_json(o, out, cap);
}

int protocol_build_mac(const char *rid, char *out, size_t cap) {
    return protocol_build_approval(rid, "mac", NULL, out, cap);
}

bool protocol_is_offline_flag(const char *topic, const uint8_t *payload, size_t len) {
    return !strcmp(topic, TOPIC_PSV_ONLINE) && len == 1 && payload[0] == '0';
}

int protocol_build_response(const char *rid, const char *choice, char *out, size_t cap) {
    return protocol_build_approval(rid, choice, NULL, out, cap);
}
