#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <math.h>
#include <stdio.h>

static int g_fail = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            g_fail++;                                                          \
        }                                                                      \
    } while (0)

#define CHECK_CLOSE(a, b, tol)                                                 \
    do {                                                                       \
        double _a = (a);                                                       \
        double _b = (b);                                                       \
        double _d = fabs(_a - _b);                                             \
        if (_d > (tol)) {                                                      \
            printf("FAIL %s:%d  |%.12g - %.12g| = %.3g > %.3g\n", __FILE__,   \
                   __LINE__, _a, _b, _d, (double)(tol));                      \
            g_fail++;                                                          \
        }                                                                      \
    } while (0)

#define TEST_END()                                                             \
    do {                                                                       \
        if (g_fail) {                                                          \
            printf("%d CHECK(S) FAILED\n", g_fail);                           \
            return 1;                                                          \
        }                                                                      \
        printf("OK\n");                                                       \
        return 0;                                                              \
    } while (0)

#endif
