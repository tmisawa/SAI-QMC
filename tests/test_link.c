#include "test_util.h"

extern void dsyev_(const char *, const char *, const int *, double *,
                   const int *, double *, double *, const int *, int *);

int main(void)
{
    int n = 2;
    int lwork = 64;
    int info = 0;
    double A[4] = {2.0, 1.0, 1.0, 2.0};
    double w[2] = {0.0, 0.0};
    double work[64];

    dsyev_("V", "U", &n, A, &n, w, work, &lwork, &info);
    CHECK(info == 0);
    CHECK_CLOSE(w[0], 1.0, 1e-12);
    CHECK_CLOSE(w[1], 3.0, 1e-12);
    TEST_END();
}
