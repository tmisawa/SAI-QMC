#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "linalg.h"
#include "model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void check_close_mat(int n, const double *a, const double *b,
                            double tol)
{
    double max_abs = 0.0;
    double num = 0.0;
    double den = 0.0;
    for (int i = 0; i < n * n; i++) {
        const double d = fabs(a[i] - b[i]);
        if (d > max_abs) {
            max_abs = d;
        }
        num += d * d;
        den += b[i] * b[i];
    }
    const double rel = sqrt(num) / (sqrt(den) + 1e-30);
    CHECK(max_abs < tol || rel < tol);
}

static int should_check_boundary(int tau, int Ltr)
{
    return tau == Ltr / 4 || tau == Ltr / 2 || tau == (3 * Ltr) / 4;
}

static void run_lowtemp_case(int Lx, int Ltr, int stab, double U,
                             double dtau, double sigma)
{
    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 9001 + Lx * 17 + Ltr * 3 + stab);
    Field f;
    field_init(&f, Lx, Ltr, U, dtau, &r);

    Green G;
    Green Ref;
    green_alloc(&G, &m, &f, sigma);
    green_alloc(&Ref, &m, &f, sigma);
    GreenStack st;
    green_stack_alloc(&st, Lx, Ltr, stab);
    green_stack_build(&G, &st);

    UDV left;
    UDV combined;
    udv_init(&left, Lx);
    udv_init(&combined, Lx);
    udv_identity(&left);

    for (int j = 0; j < st.M; j++) {
        const int begin = st.b[j];
        const int len = st.b[j + 1] - st.b[j];
        const int tau = st.b[j + 1];
        green_build_Bblock(&G, begin, len, G.B, G.Binv, G.tmp);
        udv_lmul_work(&left, G.B, &G.work);

        if (j + 1 < st.M && should_check_boundary(tau, Ltr)) {
            CHECK(green_from_stack(&G, &st, &left, &combined, j + 1) == 0);
            CHECK(green_from_scratch(&Ref, tau) == 0);
            check_close_mat(Lx, G.g, Ref.g, 1e-8);
            CHECK(G.det_sign == Ref.det_sign);
        }
    }

    CHECK(green_from_left_udv(&G, &left) == 0);
    CHECK(green_from_scratch(&Ref, 0) == 0);
    check_close_mat(Lx, G.g, Ref.g, 1e-8);
    CHECK(G.det_sign == Ref.det_sign);

    udv_free(&combined);
    udv_free(&left);
    green_stack_free(&st);
    green_free(&Ref);
    green_free(&G);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
}

int main(void)
{
    run_lowtemp_case(4, 200, 4, 12.0, 0.1, 1.0);
    run_lowtemp_case(4, 40, 8, 12.0, 0.1, 1.0);
    run_lowtemp_case(4, 40, 16, 12.0, 0.1, -1.0);
    run_lowtemp_case(8, 80, 8, 12.0, 0.1, 1.0);
    run_lowtemp_case(4, 800, 4, 12.0, 0.025, 1.0);
    run_lowtemp_case(8, 400, 4, 8.0, 0.05, -1.0);
    TEST_END();
}
