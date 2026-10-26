#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/system_param.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include "audio.h"
#include "config.h"
#include "input.h"
#include "model.h"
#include "net.h"
#include "protocol.h"
#include "sessionlist.h"
#include "settings.h"
#include "ui.h"
#include "util.h"
#include "view.h"

#define BANNER_MS 3000
#define BROKER_IME_MAX 21
#define TEXT_IME_MAX 200
#define RESPONSE_BYTES 8192

static uint64_t now_ms(void) { return sceKernelGetProcessTimeWide() / 1000; }

static InputFrame read_input(uint64_t now) {
    InputFrame f;
    memset(&f, 0, sizeof f);
    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);
    sceCtrlPeekBufferPositive(0, &pad, 1);
    f.buttons = pad.buttons;
    f.lx = pad.lx;
    f.ly = pad.ly;
    f.rx = pad.rx;
    f.ry = pad.ry;
    SceTouchData t;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) > 0 && t.reportNum > 0) {
        f.front_down = true;
        f.front_x = t.report[0].x / 2;
        f.front_y = t.report[0].y / 2;
    }
    if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &t, 1) > 0 && t.reportNum > 0) {
        f.rear_down = true;
        f.rear_x = t.report[0].x;
        f.rear_y = t.report[0].y;
    }
    f.now_ms = now;
    return f;
}

static bool any_input(const InputFrame *f) {
    return f->buttons || f->front_down || f->rear_down || abs(f->lx - 128) > 40 || abs(f->ly - 128) > 40 ||
           abs(f->rx - 128) > 60 || abs(f->ry - 128) > 60;
}

static void init_dialogs(void) {
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    memset(&init, 0, sizeof init);
    memset(&boot, 0, sizeof boot);
    sceAppUtilInit(&init, &boot);
    SceCommonDialogConfigParam cfg;
    sceCommonDialogConfigParamInit(&cfg);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, (int *)&cfg.language);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int *)&cfg.enterButtonAssign);
    sceCommonDialogSetConfigParam(&cfg);
}

/* ---------- system keyboard ---------- */

typedef enum { IME_NONE, IME_BROKER, IME_NOTE, IME_OTHER } ImePurpose;

static SceWChar16 g_ime_title[64];
static SceWChar16 g_ime_text[TEXT_IME_MAX + 1];

static bool start_ime(ImePurpose purpose, const char *title, const char *initial) {
    utf8_to_utf16(title, (uint16_t *)g_ime_title, sizeof g_ime_title / sizeof *g_ime_title);
    utf8_to_utf16(initial ? initial : "", (uint16_t *)g_ime_text, TEXT_IME_MAX + 1);
    SceImeDialogParam p;
    sceImeDialogParamInit(&p);
    p.supportedLanguages = 0;
    p.languagesForced = SCE_FALSE;
    p.type = purpose == IME_BROKER ? SCE_IME_TYPE_URL : SCE_IME_TYPE_DEFAULT;
    p.option = 0;
    p.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    p.title = g_ime_title;
    p.maxTextLength = purpose == IME_BROKER ? BROKER_IME_MAX : TEXT_IME_MAX;
    p.initialText = g_ime_text;
    p.inputTextBuffer = g_ime_text;
    return sceImeDialogInit(&p) >= 0;
}

/* True when the dialog has closed; out is "" if it was cancelled. */
static bool poll_ime(char *out, size_t cap) {
    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return false;
    SceImeDialogResult res;
    memset(&res, 0, sizeof res);
    sceImeDialogGetResult(&res);
    out[0] = '\0';
    if (res.button == SCE_IME_DIALOG_BUTTON_ENTER) utf16_to_utf8((const uint16_t *)g_ime_text, out, cap);
    sceImeDialogTerm();
    return true;
}

/* ---------- app ---------- */

typedef struct {
    Model model;
    AppConfig cfg;
    SettingsState settings;
    SessionList list;
    UiView view;
    ImePurpose ime;
    char ime_sid[64], ime_rid[24]; /* the request the keyboard was opened for */
    char note[OTHER_BYTES];
    ScrollState sheet_scroll;
    char turn_sid[64];
    bool turn_follow; /* keep the newest part of the turn in view */
    int turn_delta, sheet_delta;
    uint64_t last_activity;
    bool wake_touch; /* the finger that woke the screen must not also press what it lands on */
    unsigned tables_rev; /* ui_turn_rev() the table focus and pans belong to */
    PowerWatch power;    /* plug in → desk mode, unplug → sofa mode */
} App;

static void show_toast(App *app, const char *msg, uint64_t now) {
    snprintf(app->view.toast, sizeof app->view.toast, "%s", msg);
    app->view.toast_until_ms = now + TOAST_MS;
}

static void wake(App *app, uint64_t now) {
    app->last_activity = now;
    app->view.dimmed = false;
}

static const char *project_of(const Model *m, const char *sid) {
    for (int i = 0; i < m->n; i++)
        if (!strcmp(m->s[i].sid, sid)) return m->s[i].project;
    return "?";
}

static void handle_messages(App *app, uint64_t now) {
    InMsg msg;
    while (net_pop(&msg)) {
        char sid[64], rid[24];
        unsigned ev = 0;
        TopicKind kind = protocol_parse_topic(msg.topic, sid, rid);
        if (kind == TOPIC_STATUS) {
            static StatusMsg st; /* large; keep it off the stack */
            if (msg.len == 0) ev = model_on_status(&app->model, sid, NULL, now);
            else if (protocol_parse_status((const char *)msg.payload, msg.len, &st))
                ev = model_on_status(&app->model, sid, &st, now);
        } else if (kind == TOPIC_REQUEST) {
            Request rq;
            memset(&rq, 0, sizeof rq);
            if (msg.len == 0) ev = model_on_request(&app->model, sid, rid, NULL, now);
            else if (protocol_parse_request((const char *)msg.payload, msg.len, &rq))
                ev = model_on_request(&app->model, sid, rid, &rq, now);
        } else if (kind == TOPIC_TURN) {
            TurnItem *items = NULL;
            int n = 0;
            if (msg.len == 0) model_on_turn(&app->model, sid, NULL, 0, now);
            else if (protocol_parse_turn((const char *)msg.payload, msg.len, &items, &n))
                model_on_turn(&app->model, sid, items, n, now);
        }
        free(msg.payload);
        if (ev & MEV_NEW_REQUEST) {
            if (app->cfg.sound) audio_play(SOUND_REQUEST);
            wake(app, now);
        }
        if ((ev & MEV_WAITING) && app->cfg.mode == MODE_DESK) { /* the decision is on the computer */
            if (app->cfg.sound) audio_play(SOUND_REQUEST);
            wake(app, now);
        }
        if (ev & MEV_IDLE) {
            if (app->cfg.sound) audio_play(SOUND_IDLE);
            snprintf(app->view.banner_project, sizeof app->view.banner_project, "%s", project_of(&app->model, sid));
            app->view.banner_until_ms = now + BANNER_MS;
            wake(app, now);
        }
    }
}

static void publish_response(const Outcome *o, const char *payload, int n) {
    if (n <= 0) return;
    char topic[160];
    snprintf(topic, sizeof topic, "claude/session/%s/response/%s", o->sid, o->rid);
    net_publish(topic, payload, (size_t)n, 1, 0);
}

static void send_outcome(App *app, const Outcome *o, uint64_t now) {
    static char buf[RESPONSE_BYTES];
    const Request *r = model_current_request(&app->model);
    switch (o->kind) {
    case OUT_SEND_CHOICE:
        publish_response(o, buf, protocol_build_approval(o->rid, o->choice, !strcmp(o->choice, "deny_note") ? app->note : NULL,
                                                         buf, sizeof buf));
        break;
    case OUT_SEND_ANSWERS:
        if (r) publish_response(o, buf, protocol_build_answers(r, buf, sizeof buf));
        break;
    case OUT_SEND_MAC:
        publish_response(o, buf, protocol_build_mac(o->rid, buf, sizeof buf));
        break;
    case OUT_NEED_NOTE:
        snprintf(app->ime_sid, sizeof app->ime_sid, "%s", o->sid);
        snprintf(app->ime_rid, sizeof app->ime_rid, "%s", o->rid);
        app->ime = start_ime(IME_NOTE, "Note for Claude", "") ? IME_NOTE : IME_NONE;
        break;
    case OUT_NEED_OTHER:
        snprintf(app->ime_sid, sizeof app->ime_sid, "%s", o->sid);
        snprintf(app->ime_rid, sizeof app->ime_rid, "%s", o->rid);
        app->ime = start_ime(IME_OTHER, "Your answer", r && r->step < r->n_questions ? r->other[r->step] : "")
                       ? IME_OTHER : IME_NONE;
        break;
    case OUT_PICK_FIRST:
        show_toast(app, r && r->kind == REQ_QUESTION && model_row_kind(r, app->model.sel < 0 ? 0 : app->model.sel,
                                                                        &(int){0}) == ROW_NEXT && app->model.sel >= 0
                            ? "Pick at least one"
                            : "Pick an option first",
                   now);
        break;
    case OUT_NONE:
        break;
    }
}

static void finish_ime(App *app, const char *text, uint64_t now) {
    Outcome o;
    switch (app->ime) {
    case IME_BROKER:
        if (text[0] && settings_ime_result(&app->settings, &app->cfg, text, now) == SET_CHANGED) {
            config_save(&app->cfg, CONFIG_PATH);
            net_set_brokers(&app->cfg);
        }
        break;
    case IME_NOTE:
        if (!text[0]) break;
        utf8_copy(app->note, sizeof app->note, text, strlen(text));
        if (model_submit_note(&app->model, app->ime_sid, app->ime_rid, now, &o)) send_outcome(app, &o, now);
        else show_toast(app, "That request is gone; nothing was sent", now);
        break;
    case IME_OTHER:
        if (text[0]) model_submit_other(&app->model, app->ime_sid, app->ime_rid, text, now);
        break;
    case IME_NONE:
        break;
    }
    app->ime = IME_NONE;
}

static void toggle_mode(App *app, uint64_t now, bool toast) {
    app->cfg.mode = model_toggle_mode(&app->model);
    net_set_mode(app->cfg.mode);
    config_save(&app->cfg, CONFIG_PATH);
    if (toast)
        show_toast(app, app->cfg.mode == MODE_DESK ? "Desk mode · approvals stay on your Mac"
                                                   : "Sofa mode · approvals come to this Vita", now);
}

static void handle_main_actions(App *app, const Actions *a, uint64_t now) {
    Model *m = &app->model;
    if (a->settings) {
        app->settings.open = true;
        app->settings.cursor = app->cfg.active;
        return;
    }
    if (a->sessions) {
        list_open(&app->list, m);
        return;
    }
    if (a->session_dir) model_switch(m, a->session_dir, now);
    if (a->toggle_mode) toggle_mode(app, now, true);
    if (a->square) model_toggle_sheet(m, now);

    const Request *r = model_current_request(m);
    bool sheet = r && !m->sheet_hidden;
    bool list = sheet && r->kind == REQ_QUESTION && r->step < r->n_questions;
    if (list) {
        int lines = actions_cursor_lines(a);
        int d = lines ? lines : a->sel_dir;
        if (d) model_move_sel(m, d, now);
    } else {
        if (a->sel_dir) model_move_sel(m, a->sel_dir, now);
        if (sheet && r->kind == REQ_APPROVAL) app->sheet_delta += a->scroll_lines;
        else if (!sheet) app->turn_delta += a->scroll_lines;
    }
    /* A front tap acts on what it lands on, like a phone; options still honour the lockout. */
    bool tapped_option = false;
    if (a->tap) {
        Hit h = ui_hit_test(a->tap_x, a->tap_y);
        if (h.kind == HIT_TAB) model_select(m, h.index, now);
        else if (h.kind == HIT_PEEK) model_toggle_sheet(m, now);
        else if (h.kind == HIT_OPTION && sheet) {
            model_set_sel(m, h.index);
            tapped_option = true;
        }
    }
    if (a->back) model_back(m, now);
    if (a->confirm || tapped_option) {
        Outcome o;
        model_activate(m, now, &o);
        send_outcome(app, &o, now);
    }
}

static void handle_list_actions(App *app, const Actions *in, uint64_t now) {
    Actions tap = *in, *a = &tap;
    if (a->tap) { /* a tap picks the row or dialog button it lands on, then confirms it */
        Hit h = ui_hit_test(a->tap_x, a->tap_y);
        if (h.kind == HIT_LIST_ROW && !app->list.confirming) app->list.cursor = h.index, a->confirm = true;
        else if (h.kind == HIT_DIALOG && app->list.confirming) app->list.confirm_sel = h.index, a->confirm = true;
    }
    if (a->toggle_mode) toggle_mode(app, now, true);
    char sid[64] = "";
    if (list_handle(&app->list, &app->model, a, now, sid) == LIST_REMOVED) {
        char topic[160];
        snprintf(topic, sizeof topic, "claude/session/%s/status", sid);
        net_publish(topic, "", 0, 0, 1);
        snprintf(topic, sizeof topic, "claude/session/%s/turn", sid);
        net_publish(topic, "", 0, 0, 1);
    }
}

/* Returns true when the user chose "Exit Agent Remote". */
static bool handle_settings_actions(App *app, const Actions *in, uint64_t now) {
    Actions tap = *in, *a = &tap;
    if (a->tap) {
        Hit h = ui_hit_test(a->tap_x, a->tap_y);
        if (h.kind == HIT_SETTING) app->settings.cursor = h.index, a->confirm = true;
    }
    if (a->toggle_mode) toggle_mode(app, now, false);
    SettingsResult res = settings_handle(&app->settings, &app->cfg, a, now);
    if (res == SET_EXIT) return true;
    if (app->settings.want_ime) {
        app->settings.want_ime = false;
        app->ime = start_ime(IME_BROKER, "Broker IP:port", "") ? IME_BROKER : IME_NONE;
    }
    if (res == SET_CHANGED) {
        config_save(&app->cfg, CONFIG_PATH);
        net_set_brokers(&app->cfg);
    }
    return false;
}

/* Right stick and sideways drags pan tables in the turn view; the focus follows
 * what is on screen. Nothing pans while the decision sheet covers the turn. */
static void update_tables(App *app, const Actions *a) {
    UiView *v = &app->view;
    if (ui_turn_rev() != app->tables_rev) {
        app->tables_rev = ui_turn_rev();
        v->table_focus = -1;
        memset(v->table_pan, 0, sizeof v->table_pan);
    }
    const Request *r = model_current_request(&app->model);
    bool covered = (r && !app->model.sheet_hidden) || app->list.open || app->settings.open;
    int n = ui_table_count();
    bool vis[UI_MAX_TABLES];
    for (int i = 0; i < n; i++) vis[i] = ui_table_visible_overflow(i);
    v->table_focus = table_focus_pick(vis, n, v->table_focus, covered ? 0 : a->table_dir);
    if (!covered) {
        if (v->table_focus >= 0) v->table_pan[v->table_focus] += a->pan_px;
        if (a->touch_pan_px) {
            Hit h = ui_hit_test(a->touch_x, a->touch_y);
            if (h.kind == HIT_TABLE && h.index < n && vis[h.index]) {
                v->table_focus = h.index;
                v->table_pan[h.index] += a->touch_pan_px;
            }
        }
    }
    for (int i = 0; i < n; i++) v->table_pan[i] = table_pan_clamp(v->table_pan[i], ui_table_width(i), ui_table_view_w());
}

/* Turn view sticks to the newest lines unless the user scrolled up. */
static void update_scrolls(App *app, uint64_t now) {
    (void)now;
    const Session *s = model_current(&app->model);
    const Request *r = model_current_request(&app->model);
    const char *sid = s ? s->sid : "";
    if (strcmp(sid, app->turn_sid)) {
        snprintf(app->turn_sid, sizeof app->turn_sid, "%s", sid);
        app->turn_follow = true;
    }
    int max = ui_turn_max_scroll();
    if (app->turn_delta) {
        app->view.turn_scroll += app->turn_delta;
        app->turn_follow = app->view.turn_scroll >= max;
    }
    if (app->turn_follow) app->view.turn_scroll = max;
    if (app->view.turn_scroll > max) app->view.turn_scroll = max;
    if (app->view.turn_scroll < 0) app->view.turn_scroll = 0;
    app->view.sheet_scroll = scroll_update(&app->sheet_scroll, sid, r ? r->rid : "", app->sheet_delta,
                                           ui_sheet_max_scroll(), 0);
    app->turn_delta = app->sheet_delta = 0;
}

int main(void) {
    static App app;
    config_defaults(&app.cfg);
    config_migrate_dir(OLD_DATA_DIR, DATA_DIR); /* keeps config.txt and psk.txt across the rename */
    config_load(&app.cfg, CONFIG_PATH);
    model_init(&app.model, app.cfg.mode);
    app.turn_follow = true;

    vita2d_init();
    init_dialogs();
    ui_init();
    audio_init();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
    net_start(&app.cfg, app.cfg.mode);

    InputState input;
    input_init(&input);
    input_set_front_enabled(&input, app.cfg.front_touch);
    app.last_activity = now_ms();

    for (;;) {
        uint64_t now = now_ms();
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT); /* we dim ourselves; never suspend */

        if (input.front_enabled != (app.cfg.front_touch != 0)) input_set_front_enabled(&input, app.cfg.front_touch);
        InputFrame frame = read_input(now);
        Actions a = input_map(&input, &frame);
        bool quit = false;
        if (app.wake_touch) {
            a.tap = false;
            if (!frame.front_down) app.wake_touch = false;
        }

        if (app.ime != IME_NONE) {
            char text[OTHER_BYTES * 2];
            if (poll_ime(text, sizeof text)) finish_ime(&app, text, now);
        } else if (any_input(&frame) || actions_any(&a)) {
            bool was_dimmed = app.view.dimmed;
            if (was_dimmed && frame.front_down) app.wake_touch = true;
            wake(&app, now);
            if (!was_dimmed) {
                if (app.list.open) handle_list_actions(&app, &a, now);
                else if (app.settings.open) quit = handle_settings_actions(&app, &a, now);
                else handle_main_actions(&app, &a, now);
            }
        }

        Actions none;
        memset(&none, 0, sizeof none);
        update_tables(&app, app.ime == IME_NONE && !app.view.dimmed ? &a : &none);
        handle_messages(&app, now);
        model_tick(&app.model, now);
        update_scrolls(&app, now);

        int connected = net_connected_index();
        if (connected >= 0 && connected != app.cfg.active) {
            app.cfg.active = connected;
            config_save(&app.cfg, CONFIG_PATH);
        }
        if (app.cfg.dim_after_s > 0 && now - app.last_activity > (uint64_t)app.cfg.dim_after_s * 1000)
            app.view.dimmed = true;

        app.view.net = net_status();
        static uint64_t battery_next;
        if (now >= battery_next) { /* cheap, but no need to ask every frame */
            battery_next = now + 1000;
            app.view.battery_pct = scePowerGetBatteryLifePercent();
            app.view.charging = scePowerIsBatteryCharging() == SCE_TRUE;
            app.view.power_online = scePowerIsPowerOnline() == SCE_TRUE;
            PowerChange ch = power_watch(&app.power, app.view.power_online);
            Mode want = ch == POWER_TO_DESK ? MODE_DESK : MODE_SOFA;
            if (ch != POWER_NO_CHANGE && app.cfg.mode != want) {
                toggle_mode(&app, now, false);
                show_toast(&app, want == MODE_DESK ? "Plugged in · desk mode, approvals stay on your Mac"
                                                   : "Unplugged · sofa mode, approvals come to this Vita", now);
            }
        }
        app.view.net_err = app.view.net == NET_UP ? NETERR_NONE : net_broker_error(app.cfg.active);

        /* vita2d_start_drawing resets the vertex pool and overwrites last frame's
         * vertices and circle indices; wait until the GPU has finished with them. */
        vita2d_wait_rendering_done();
        vita2d_start_drawing();
        vita2d_clear_screen();
        if (app.list.open) ui_draw_sessions(&app.model, &app.list, &app.view, now);
        else if (app.settings.open) ui_draw_settings(&app.model, &app.cfg, &app.settings, connected, &app.view, now);
        else ui_draw(&app.model, &app.view, now);
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
        if (quit) break;
    }

    net_stop();
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
