#include "settings.h"
#include "test.h"

static Actions A(void) {
    Actions a;
    memset(&a, 0, sizeof a);
    return a;
}

static void t_switch_active(void) {
    AppConfig c;
    config_defaults(&c);
    Broker b;
    broker_parse("10.0.0.5", &b);
    config_add_broker(&c, &b);
    SettingsState s = {.open = true};
    Actions a = A();
    a.sel_dir = 1;
    settings_handle(&s, &c, &a, 0);
    CHECK(s.cursor == 1);
    a = A();
    a.confirm = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_CHANGED);
    CHECK(c.active == 1);
}

static void t_cursor_bounds(void) {
    AppConfig c;
    config_defaults(&c);
    SettingsState s = {.open = true};
    Actions a = A();
    a.scroll_lines = -1;
    settings_handle(&s, &c, &a, 0);
    CHECK(s.cursor == 0);
    a.scroll_lines = 1;
    for (int i = 0; i < 10; i++) settings_handle(&s, &c, &a, 0);
    CHECK(s.cursor == 5); /* rows: 1 broker, add, sounds, front touch, dim, exit */
}

static SettingsState at_row(const AppConfig *c, SettingsRow row) {
    SettingsState s = {.open = true};
    s.cursor = settings_row_index(c, row);
    return s;
}

static void t_rows(void) {
    AppConfig c;
    config_defaults(&c);
    CHECK(settings_row_count(&c) == 6);
    CHECK(settings_row_at(&c, 0) == ROW_BROKER);
    CHECK(settings_row_at(&c, 1) == ROW_ADD);
    CHECK(settings_row_at(&c, 3) == ROW_TOUCH);
    CHECK(settings_row_at(&c, 5) == ROW_EXIT);
}

static void t_sound_toggle(void) {
    AppConfig c;
    config_defaults(&c);
    SettingsState s = at_row(&c, ROW_SOUND);
    Actions a = A();
    a.confirm = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_CHANGED);
    CHECK(c.sound == 0);
    settings_handle(&s, &c, &a, 0);
    CHECK(c.sound == 1);
}

static void t_front_touch_toggle(void) {
    AppConfig c;
    config_defaults(&c);
    CHECK(c.front_touch == 1);
    SettingsState s = at_row(&c, ROW_TOUCH);
    Actions a = A();
    a.confirm = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_CHANGED);
    CHECK(c.front_touch == 0);
    settings_handle(&s, &c, &a, 0);
    CHECK(c.front_touch == 1);
}

static void t_dim_cycles(void) {
    AppConfig c;
    config_defaults(&c); /* 300 s */
    SettingsState s = at_row(&c, ROW_DIM);
    Actions a = A();
    a.confirm = true;
    int seen[4];
    for (int i = 0; i < 4; i++) {
        CHECK(settings_handle(&s, &c, &a, 0) == SET_CHANGED);
        seen[i] = c.dim_after_s;
    }
    CHECK(seen[0] == 600 && seen[1] == 0 && seen[2] == 60 && seen[3] == 300);
}

static void t_exit_row(void) {
    AppConfig c;
    config_defaults(&c);
    SettingsState s = at_row(&c, ROW_EXIT);
    Actions a = A();
    a.confirm = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_EXIT);
}

static void t_square_only_on_broker_rows(void) {
    AppConfig c;
    config_defaults(&c);
    Broker b;
    broker_parse("10.0.0.5", &b);
    config_add_broker(&c, &b);
    SettingsState s = at_row(&c, ROW_SOUND);
    Actions a = A();
    a.square = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_NONE);
    CHECK(c.n_brokers == 2);
}

static void t_add_row_requests_ime(void) {
    AppConfig c;
    config_defaults(&c);
    SettingsState s = {.open = true, .cursor = 1};
    Actions a = A();
    a.confirm = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_NONE);
    CHECK(s.want_ime);
}

static void t_ime_result(void) {
    AppConfig c;
    config_defaults(&c);
    SettingsState s = {.open = true};
    CHECK(settings_ime_result(&s, &c, "10.0.0.9:1884", 0) == SET_CHANGED);
    CHECK(c.n_brokers == 2);
    CHECK(settings_ime_result(&s, &c, "nope", 0) == SET_NONE);
    CHECK(c.n_brokers == 2);
    CHECK(s.toast[0]);
    CHECK(settings_ime_result(&s, &c, "10.0.0.9:1884", 0) == SET_NONE); /* duplicate */
}

static void t_ime_replace_toast(void) {
    AppConfig c;
    config_defaults(&c);
    SettingsState s = {.open = true};
    CHECK(settings_ime_result(&s, &c, "tls://192.168.2.222:1883", 0) == SET_CHANGED);
    CHECK_STR(s.toast, "Now encrypted");
    CHECK(c.n_brokers == 1 && c.brokers[0].tls);
    CHECK(settings_ime_result(&s, &c, "192.168.2.222:1883", 0) == SET_NONE); /* no silent downgrade */
    CHECK(c.brokers[0].tls);
    CHECK_STR(s.toast, "Remove it first to make it unencrypted");
}

static void t_error_texts(void) {
    CHECK(settings_error_text(NETERR_NONE) == NULL);
    CHECK_STR(settings_error_text(NETERR_UNREACHABLE), "Can't reach broker");
    CHECK_STR(settings_error_text(NETERR_KEY), "Key rejected");
    CHECK_STR(settings_error_text(NETERR_TLS), "TLS error");
    CHECK_STR(settings_error_text(NETERR_NO_KEY), "No key: pair this PSV first");
}

static void t_cannot_delete_last(void) {
    AppConfig c;
    config_defaults(&c);
    SettingsState s = {.open = true};
    Actions a = A();
    a.square = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_NONE);
    CHECK(c.n_brokers == 1);
    CHECK(s.toast[0]);
    CHECK(s.toast_until_ms == 2000);
}

static void t_delete(void) {
    AppConfig c;
    config_defaults(&c);
    Broker b;
    broker_parse("10.0.0.5", &b);
    config_add_broker(&c, &b);
    SettingsState s = {.open = true, .cursor = 1};
    Actions a = A();
    a.square = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_CHANGED);
    CHECK(c.n_brokers == 1);
    CHECK(s.cursor <= c.n_brokers);
}

static void t_close(void) {
    AppConfig c;
    config_defaults(&c);
    SettingsState s = {.open = true};
    Actions a = A();
    a.back = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_CLOSED);
    CHECK(!s.open);
    s.open = true;
    a = A();
    a.settings = true;
    CHECK(settings_handle(&s, &c, &a, 0) == SET_CLOSED);
}

int main(void) {
    RUN(t_rows);
    RUN(t_sound_toggle);
    RUN(t_front_touch_toggle);
    RUN(t_dim_cycles);
    RUN(t_exit_row);
    RUN(t_square_only_on_broker_rows);
    RUN(t_switch_active);
    RUN(t_cursor_bounds);
    RUN(t_add_row_requests_ime);
    RUN(t_ime_result);
    RUN(t_ime_replace_toast);
    RUN(t_error_texts);
    RUN(t_cannot_delete_last);
    RUN(t_delete);
    RUN(t_close);
    return TEST_RESULT();
}
