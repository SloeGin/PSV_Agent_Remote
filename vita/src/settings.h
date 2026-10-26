#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "input.h"
#include "net.h"

#define TOAST_MS 2000

/* Rows, top to bottom: each broker, "Add broker", Sounds, Front touch, Dim screen after, Exit. */
typedef enum { ROW_BROKER, ROW_ADD, ROW_SOUND, ROW_TOUCH, ROW_DIM, ROW_EXIT } SettingsRow;

typedef struct {
    bool open;
    int cursor;
    char toast[64];
    uint64_t toast_until_ms;
    bool want_ime;
} SettingsState;

typedef enum { SET_NONE, SET_CHANGED, SET_CLOSED, SET_EXIT } SettingsResult;

int settings_row_count(const AppConfig *c);
SettingsRow settings_row_at(const AppConfig *c, int index);
int settings_row_index(const AppConfig *c, SettingsRow row); /* first row of that kind */

SettingsResult settings_handle(SettingsState *s, AppConfig *c, const Actions *a, uint64_t now);
SettingsResult settings_ime_result(SettingsState *s, AppConfig *c, const char *text, uint64_t now);

/* What to show for a broker's connection error; NULL for NETERR_NONE. */
const char *settings_error_text(NetError e);
