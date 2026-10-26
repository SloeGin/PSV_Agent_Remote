/* Minimal host test harness: CHECK/CHECK_STR record failures, RUN calls a test. */
#pragma once

#include <stdio.h>
#include <string.h>

static int test_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            test_failures++;                                                 \
        }                                                                    \
    } while (0)

#define CHECK_STR(a, b)                                                       \
    do {                                                                      \
        const char *a_ = (a), *b_ = (b);                                      \
        if (strcmp(a_, b_) != 0) {                                            \
            fprintf(stderr, "%s:%d: CHECK_STR failed: \"%s\" != \"%s\"\n",    \
                    __FILE__, __LINE__, a_, b_);                              \
            test_failures++;                                                  \
        }                                                                     \
    } while (0)

#define RUN(fn) fn()

#define TEST_RESULT() (test_failures ? 1 : 0)
