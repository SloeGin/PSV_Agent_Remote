#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Length of the valid UTF-8 sequence starting at s (at most n bytes), or 0. */
size_t utf8_seq_len(const char *s, size_t n);

/* Copy at most cap-1 bytes of src[0..src_len) into dst without splitting a
 * character, replacing each invalid byte with '?'. Always NUL-terminates
 * (when cap > 0) and returns the number of bytes written. */
size_t utf8_copy(char *dst, size_t cap, const char *src, size_t src_len);

bool utf8_valid(const char *s, size_t len);

/* UTF-8 -> NUL-terminated UTF-16 (at most cap units incl. NUL); returns units written. */
size_t utf8_to_utf16(const char *src, uint16_t *dst, size_t cap);
/* NUL-terminated UTF-16 -> UTF-8 without splitting characters; unpaired
 * surrogates become '?'. Returns bytes written (excluding NUL). */
size_t utf16_to_utf8(const uint16_t *src, char *dst, size_t cap);
