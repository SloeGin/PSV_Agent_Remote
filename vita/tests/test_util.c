#include "test.h"
#include "util.h"

static void t_copy_truncates_on_boundary(void) {
    char d[5]; /* "中" 是 3 个字节 */
    CHECK(utf8_copy(d, sizeof d, "中中", 6) == 3);
    CHECK_STR(d, "中");
}

static void t_copy_replaces_invalid(void) {
    char d[8];
    utf8_copy(d, sizeof d, "a\xff" "b", 3);
    CHECK_STR(d, "a?b");
}

static void t_copy_stops_at_src_len(void) {
    char d[8];
    CHECK(utf8_copy(d, sizeof d, "abcdef", 2) == 2);
    CHECK_STR(d, "ab");
}

static void t_valid(void) {
    CHECK(utf8_valid("中", 3));
    CHECK(!utf8_valid("\xe4\xb8", 2));
    CHECK(!utf8_valid("\xc0\x80", 2)); /* overlong */
}

static void t_utf16_roundtrip(void) {
    uint16_t w[16];
    char back[32];
    size_t n = utf8_to_utf16("a中😀", w, 16);
    CHECK(n == 4);                       /* a, 中, surrogate pair */
    CHECK(w[0] == 'a' && w[1] == 0x4E2D && w[2] == 0xD83D && w[3] == 0xDE00 && w[4] == 0);
    CHECK(utf16_to_utf8(w, back, sizeof back) == strlen("a中😀"));
    CHECK_STR(back, "a中😀");
}

static void t_utf16_limits(void) {
    uint16_t w[3];
    CHECK(utf8_to_utf16("中中中", w, 3) == 2); /* room for 2 units + NUL */
    char small[4];
    uint16_t src[] = {0x4E2D, 0x4E2D, 0};
    CHECK(utf16_to_utf8(src, small, sizeof small) == 3); /* second char does not fit */
    CHECK_STR(small, "中");
    uint16_t lone[] = {0xD83D, 'x', 0};      /* unpaired surrogate is replaced */
    char out[8];
    utf16_to_utf8(lone, out, sizeof out);
    CHECK_STR(out, "?x");
}

int main(void) {
    RUN(t_utf16_roundtrip);
    RUN(t_utf16_limits);
    RUN(t_copy_truncates_on_boundary);
    RUN(t_copy_replaces_invalid);
    RUN(t_copy_stops_at_src_len);
    RUN(t_valid);
    return TEST_RESULT();
}
