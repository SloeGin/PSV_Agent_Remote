#include "psk.h"
#include "test.h"

static char line[160];

static const char *key_line(const char *id, const char *first, const char *last, int n_hex) {
    /* id ':' then n_hex hex digits: first two chars, then '0's, then last two chars */
    int k = snprintf(line, sizeof line, "%s:", id);
    for (int i = 0; i < n_hex; i++) line[k + i] = '0';
    if (n_hex >= 4) {
        memcpy(line + k, first, 2);
        memcpy(line + k + n_hex - 2, last, 2);
    }
    line[k + n_hex] = 0;
    return line;
}

static void t_psk_valid(void) {
    Psk p;
    CHECK(psk_parse(key_line("psv", "01", "ff", 64), &p));
    CHECK_STR(p.identity, "psv");
    CHECK(p.key[0] == 0x01 && p.key[31] == 0xff && p.key[5] == 0);
}

static void t_psk_tolerates_crlf_case_space(void) {
    Psk p;
    char buf[200];
    snprintf(buf, sizeof buf, "%s\r\n", key_line("psv", "AB", "CD", 64));
    CHECK(psk_parse(buf, &p));
    CHECK(p.key[0] == 0xab && p.key[31] == 0xcd);
    snprintf(buf, sizeof buf, "\n  %s  \n", key_line("psv", "ab", "cd", 64));
    CHECK(psk_parse(buf, &p));
    CHECK_STR(p.identity, "psv");
}

static void t_psk_rejects_bad(void) {
    Psk p;
    CHECK(!psk_parse(key_line("psv", "01", "ff", 63), &p));
    CHECK(!psk_parse(key_line("psv", "01", "ff", 65), &p));
    CHECK(!psk_parse(key_line("psv", "zz", "ff", 64), &p));
    CHECK(!psk_parse(key_line("", "01", "ff", 64), &p));
    char buf[200];
    snprintf(buf, sizeof buf, "psv%s", key_line("", "01", "ff", 64) + 1); /* no colon */
    CHECK(!psk_parse(buf, &p));
    CHECK(!psk_parse("", &p));
}

static void t_psk_load_missing(void) {
    Psk p;
    CHECK(!psk_load("/nonexistent/psk.txt", &p));
}

static void t_psk_wipe(void) {
    Psk p;
    CHECK(psk_parse(key_line("psv", "01", "ff", 64), &p));
    psk_wipe(&p);
    int sum = 0;
    for (int i = 0; i < PSK_KEY_LEN; i++) sum |= p.key[i];
    CHECK(sum == 0 && p.identity[0] == 0);
}

int main(void) {
    RUN(t_psk_valid);
    RUN(t_psk_tolerates_crlf_case_space);
    RUN(t_psk_rejects_bad);
    RUN(t_psk_load_missing);
    RUN(t_psk_wipe);
    return TEST_RESULT();
}
