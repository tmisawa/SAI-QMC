#include "test_util.h"
#include "field.h"

#include <math.h>

int main(void)
{
    Rng r;
    rng_seed(&r, 12345);

    Field f;
    field_init(&f, 4, 10, 4.0, 0.1, &r);
    CHECK_CLOSE(cosh(f.lambda), exp(0.1 * 4.0 / 2.0), 1e-12);
    CHECK_CLOSE(field_N(&f, 1.0, 1), exp(-2.0 * f.lambda) - 1.0, 1e-12);
    CHECK_CLOSE(field_N(&f, -1.0, 1), exp(2.0 * f.lambda) - 1.0, 1e-12);
    for (int k = 0; k < f.L * f.n; k++) {
        CHECK(f.s[k] == 1 || f.s[k] == -1);
    }
    field_free(&f);

    Rng r1;
    Rng r2;
    rng_seed(&r1, 99);
    rng_seed(&r2, 99);
    for (int i = 0; i < 8; i++) {
        const double x1 = rng_double(&r1);
        const double x2 = rng_double(&r2);
        CHECK(x1 >= 0.0);
        CHECK(x1 < 1.0);
        CHECK_CLOSE(x1, x2, 0.0);
    }

    TEST_END();
}
