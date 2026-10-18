// Мінімальна підтримка хостових тестів чистої логіки (без фреймворку).
#pragma once
#include <stdio.h>

static int g_tests_run;
static int g_tests_passed;

#define CHECK(cond)                                                         \
    do {                                                                    \
        g_tests_run++;                                                      \
        if (cond) {                                                         \
            g_tests_passed++;                                               \
        } else {                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                   \
    } while (0)

// Друкує підсумок у форматі, який розбирає run_tests.sh: "TESTS <усього> <пройдено>".
static inline int test_summary(void)
{
    printf("TESTS %d %d\n", g_tests_run, g_tests_passed);
    return g_tests_run == g_tests_passed ? 0 : 1;
}
