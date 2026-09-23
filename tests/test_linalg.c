#include "test_util.h"
#include "linalg.h"

int main(void)
{
    const int n = 2;
    double A[4] = {1.0, 3.0, 2.0, 4.0};
    double B[4] = {1.0, 0.0, 0.0, 1.0};
    double C[4];

    la_matmul(n, A, B, C);
    CHECK_CLOSE(C[0], 1.0, 1e-12);
    CHECK_CLOSE(C[1], 3.0, 1e-12);
    CHECK_CLOSE(C[2], 2.0, 1e-12);
    CHECK_CLOSE(C[3], 4.0, 1e-12);

    double Ainv[4];
    CHECK(la_inverse(n, A, Ainv) == 0);

    double P[4];
    la_matmul(n, A, Ainv, P);
    CHECK_CLOSE(P[0], 1.0, 1e-12);
    CHECK_CLOSE(P[1], 0.0, 1e-12);
    CHECK_CLOSE(P[2], 0.0, 1e-12);
    CHECK_CLOSE(P[3], 1.0, 1e-12);

    double A2[4] = {1.0, 3.0, 2.0, 4.0};
    int sgn = 0;
    double lad = 0.0;
    CHECK(la_logdet(n, A2, &sgn, &lad) == 0);
    CHECK(sgn == -1);
    CHECK_CLOSE(exp(lad), 2.0, 1e-12);

    double S[4] = {2.0, 1.0, 1.0, 2.0};
    double E[4];
    la_expm_sym(n, S, 1.0, E);
    const double a = (exp(3.0) + exp(1.0)) / 2.0;
    const double b = (exp(3.0) - exp(1.0)) / 2.0;
    CHECK_CLOSE(E[0], a, 1e-9);
    CHECK_CLOSE(E[1], b, 1e-9);
    CHECK_CLOSE(E[2], b, 1e-9);
    CHECK_CLOSE(E[3], a, 1e-9);

    TEST_END();
}
