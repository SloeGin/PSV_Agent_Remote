#include "util.h"

#include <string.h>

size_t utf8_seq_len(const char *s, size_t n) {
    const unsigned char *u = (const unsigned char *)s;
    if (n == 0) return 0;
    if (u[0] < 0x80) return 1;
    size_t len;
    unsigned min;
    if ((u[0] & 0xE0) == 0xC0) { len = 2; min = 0x80; }
    else if ((u[0] & 0xF0) == 0xE0) { len = 3; min = 0x800; }
    else if ((u[0] & 0xF8) == 0xF0) { len = 4; min = 0x10000; }
    else return 0;
    if (n < len) return 0;
    unsigned cp = u[0] & (0x7F >> len);
    for (size_t i = 1; i < len; i++) {
        if ((u[i] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (u[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return len;
}

size_t utf8_copy(char *dst, size_t cap, const char *src, size_t src_len) {
    if (cap == 0) return 0;
    size_t out = 0, i = 0;
    while (i < src_len) {
        size_t len = utf8_seq_len(src + i, src_len - i);
        if (len == 0) {
            if (out + 1 > cap - 1) break;
            dst[out++] = '?';
            i++;
            continue;
        }
        if (out + len > cap - 1) break;
        for (size_t k = 0; k < len; k++) dst[out++] = src[i + k];
        i += len;
    }
    dst[out] = '\0';
    return out;
}

bool utf8_valid(const char *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t n = utf8_seq_len(s + i, len - i);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

size_t utf8_to_utf16(const char *src, uint16_t *dst, size_t cap) {
    if (cap == 0) return 0;
    size_t out = 0, i = 0, n = strlen(src);
    while (i < n) {
        size_t len = utf8_seq_len(src + i, n - i);
        const unsigned char *u = (const unsigned char *)src + i;
        uint32_t cp;
        if (len == 0) cp = '?', len = 1;
        else if (len == 1) cp = u[0];
        else if (len == 2) cp = ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
        else if (len == 3) cp = ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
        else cp = ((u[0] & 0x07u) << 18) | ((u[1] & 0x3Fu) << 12) | ((u[2] & 0x3Fu) << 6) | (u[3] & 0x3Fu);
        size_t units = cp >= 0x10000 ? 2 : 1;
        if (out + units + 1 > cap) break;
        if (units == 2) {
            cp -= 0x10000;
            dst[out++] = (uint16_t)(0xD800 + (cp >> 10));
            dst[out++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            dst[out++] = (uint16_t)cp;
        }
        i += len;
    }
    dst[out] = 0;
    return out;
}

size_t utf16_to_utf8(const uint16_t *src, char *dst, size_t cap) {
    if (cap == 0) return 0;
    size_t out = 0;
    for (size_t i = 0; src[i]; i++) {
        uint32_t cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = '?';
        }
        char buf[4];
        size_t len;
        if (cp < 0x80) buf[0] = (char)cp, len = 1;
        else if (cp < 0x800) buf[0] = (char)(0xC0 | (cp >> 6)), buf[1] = (char)(0x80 | (cp & 0x3F)), len = 2;
        else if (cp < 0x10000)
            buf[0] = (char)(0xE0 | (cp >> 12)), buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F)), buf[2] = (char)(0x80 | (cp & 0x3F)), len = 3;
        else
            buf[0] = (char)(0xF0 | (cp >> 18)), buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F)),
            buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F)), buf[3] = (char)(0x80 | (cp & 0x3F)), len = 4;
        if (out + len + 1 > cap) break;
        memcpy(dst + out, buf, len);
        out += len;
    }
    dst[out] = '\0';
    return out;
}
