#include "settings.h"

#include <stdio.h>
#include <string.h>

static const int DIM_STEPS[] = {60, 300, 600, 0}; /* seconds; 0 = never */

static void toast(SettingsState *s, const char *msg, uint64_t now) {
    snprintf(s->toast, sizeof s->toast, "%s", msg);
    s->toast_until_ms = now + TOAST_MS;
}

int settings_row_count(const AppConfig *c) { return c->n_brokers + 5; }

SettingsRow settings_row_at(const AppConfig *c, int index) {
    if (index < c->n_brokers) return ROW_BROKER;
    return (SettingsRow)(ROW_ADD + (index - c->n_brokers));
}

int settings_row_index(const AppConfig *c, SettingsRow row) {
    return row == ROW_BROKER ? 0 : c->n_brokers + (row - ROW_ADD);
}

static int next_dim(int current) {
    int n = (int)(sizeof DIM_STEPS / sizeof *DIM_STEPS);
    for (int i = 0; i < n; i++)
        if (DIM_STEPS[i] == current) return DIM_STEPS[(i + 1) % n];
    return DIM_STEPS[0];
}

SettingsResult settings_handle(SettingsState *s, AppConfig *c, const Actions *a, uint64_t now) {
    if (a->back || a->settings) {
        s->open = false;
        return SET_CLOSED;
    }
    int step = a->scroll_lines ? a->scroll_lines : a->sel_dir;
    if (step) {
        s->cursor += step > 0 ? 1 : -1;
        if (s->cursor < 0) s->cursor = 0;
        if (s->cursor >= settings_row_count(c)) s->cursor = settings_row_count(c) - 1;
    }
    SettingsRow row = settings_row_at(c, s->cursor);
    if (a->confirm) {
        switch (row) {
        case ROW_BROKER: c->active = s->cursor; return SET_CHANGED;
        case ROW_ADD: s->want_ime = true; return SET_NONE;
        case ROW_SOUND: c->sound = !c->sound; return SET_CHANGED;
        case ROW_TOUCH: c->front_touch = !c->front_touch; return SET_CHANGED;
        case ROW_DIM: c->dim_after_s = next_dim(c->dim_after_s); return SET_CHANGED;
        case ROW_EXIT: return SET_EXIT;
        }
    }
    if (a->square && row == ROW_BROKER) {
        if (!config_remove_broker(c, s->cursor)) {
            toast(s, "Keep at least one broker", now);
            return SET_NONE;
        }
        return SET_CHANGED;
    }
    return SET_NONE;
}

SettingsResult settings_ime_result(SettingsState *s, AppConfig *c, const char *text, uint64_t now) {
    Broker b;
    if (!broker_parse(text, &b)) {
        toast(s, "Use an IP like 192.168.1.20:1883 or tls://IP", now);
        return SET_NONE;
    }
    for (int i = 0; i < c->n_brokers; i++)
        if (!b.tls && c->brokers[i].tls && !strcmp(c->brokers[i].host, b.host) && c->brokers[i].port == b.port) {
            toast(s, "Remove it first to make it unencrypted", now); /* no downgrade by a missing prefix */
            return SET_NONE;
        }
    switch (config_add_broker(c, &b)) {
    case BROKER_ADDED: toast(s, "Broker added", now); return SET_CHANGED;
    case BROKER_REPLACED: toast(s, b.tls ? "Now encrypted" : "Now unencrypted", now); return SET_CHANGED;
    case BROKER_EXISTS:
    case BROKER_FULL: break;
    }
    toast(s, "Already listed, or the list is full", now);
    return SET_NONE;
}

const char *settings_error_text(NetError e) {
    switch (e) {
    case NETERR_UNREACHABLE: return "Can't reach broker";
    case NETERR_KEY: return "Key rejected";
    case NETERR_TLS: return "TLS error";
    case NETERR_NO_KEY: return "No key: pair this PSV first";
    case NETERR_NONE: break;
    }
    return NULL;
}
