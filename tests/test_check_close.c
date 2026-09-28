#include "test_util.h"

/* Regression test for the close-comparison predicate used by CHECK_CLOSE.
 * These are negative controls from an independent review: the predicate
 * used to be `fabs(a - b) <= tol`, which is false (so the check silently
 * passes) whenever a, b, or tol is non-finite, because any comparison
 * against NaN is false and INFINITY - INFINITY is NaN. Call test_close()
 * directly (not CHECK_CLOSE's own failing path) so this test does not
 * depend on how CHECK_CLOSE formats or reports a failure. */
int main(void)
{
    /* Non-finite operands or tolerance must be rejected, not silently pass. */
    CHECK(test_close(NAN, 1.0, 1e-12) == 0);
    CHECK(test_close(1.0, NAN, 1e-12) == 0);
    CHECK(test_close(INFINITY, INFINITY, 1e-12) == 0);
    CHECK(test_close(-INFINITY, -INFINITY, 1e-12) == 0);
    CHECK(test_close(1.0, 1.0, NAN) == 0);
    CHECK(test_close(1.0, 1.0, -1.0) == 0);

    /* Ordinary finite comparisons still behave as before. */
    CHECK(test_close(1.0, 1.0, 0.0) == 1);
    CHECK(test_close(1.0, 1.0 + 1e-13, 1e-12) == 1);

    TEST_END();
}
