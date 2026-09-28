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

/* True iff a, b and tol are all finite, tol is non-negative, and a and b are
 * within tol of each other. A plain `fabs(a - b) <= tol` silently accepts
 * non-finite a, b, or tol: any comparison against NaN is false, and
 * INFINITY - INFINITY is NaN, so `fabs(INFINITY - INFINITY) > tol` is also
 * false. Reject those cases explicitly instead of letting the check pass. */
static inline int test_close(double a, double b, double tol)
{
    if (!isfinite(a) || !isfinite(b) || !isfinite(tol) || tol < 0.0) {
        return 0;
    }
    return fabs(a - b) <= tol;
}

#define CHECK_CLOSE(a, b, tol)                                                \
    do {                                                                      \
        double _a = (a);                                                      \
        double _b = (b);                                                      \
        double _tol = (tol);                                                  \
        if (!test_close(_a, _b, _tol)) {                                      \
            /* mirrors test_close's own validity guard, to pick a message */  \
            if (isfinite(_a) && isfinite(_b) && isfinite(_tol) &&             \
                _tol >= 0.0) {                                                \
                printf("FAIL %s:%d  |%.12g - %.12g| = %.3g > %.3g\n",         \
                       __FILE__, __LINE__, _a, _b, fabs(_a - _b), _tol);      \
            } else {                                                          \
                printf("FAIL %s:%d  non-finite CHECK_CLOSE(%.12g, %.12g, "    \
                       "%.12g)\n",                                            \
                       __FILE__, __LINE__, _a, _b, _tol);                     \
            }                                                                 \
            g_fail++;                                                        \
        }                                                                     \
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
