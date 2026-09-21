#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "linalg.h"
#include "model.h"

#include <stdlib.h>
#include <string.h>

/* Dense log|det(1 + B_{L-1}...B_0)| for one spin. */
static void dense_logdet(Green *G, int *sign, double *logabs)
{
    const int n = G->n;
    double *A = calloc((size_t)n * n, sizeof(double));
    double *Bl = calloc((size_t)n * n, sizeof(double));
    double *tmp = calloc((size_t)n * n, sizeof(double));
    la_eye(n, A);
    for (int l = 0; l < G->L; l++) {
        green_build_B(G, l, Bl);
        la_matmul(n, Bl, A, tmp);
        memcpy(A, tmp, sizeof(double) * (size_t)n * n);
    }
    for (int i = 0; i < n; i++) {
        A[i + i * n] += 1.0;
    }
    la_logdet(n, A, sign, logabs);
    free(A);
    free(Bl);
    free(tmp);
}

static void run_case(double U, int Ltr, int stab, GreenRebuildMode mode,
                     uint64_t seed)
{
    Lattice L;
    lattice_chain(&L, 4, -1.0, 1);
    Model m;
    model_init(&m, &L, U, 0.1, 1, 0.0);
    Rng r;
    rng_seed(&r, seed);
    Field f;
    field_init(&f, L.n, Ltr, U, 0.1, &r);
    Green Gu, Gd;
    green_alloc(&Gu, &m, &f, 1.0);
    green_alloc(&Gd, &m, &f, -1.0);
    green_set_rebuild_mode(&Gu, mode);
    green_set_rebuild_mode(&Gd, mode);

    CHECK(green_from_scratch(&Gu, 0) == 0);
    const int n = L.n;
    double *g0 = malloc(sizeof(double) * (size_t)n * n);
    memcpy(g0, Gu.g, sizeof(double) * (size_t)n * n);
    const int cur0 = Gu.cur_l;
    const int sign0 = Gu.det_sign;

    for (int flip = 0; flip < 2; flip++) {
        int su = 0, sd = 0, ru = 0, rd = 0;
        double lu = 0.0, ld = 0.0, du = 0.0, dd = 0.0;
        CHECK(green_logdet_full(&Gu, stab, &su, &lu) == 0);
        CHECK(green_logdet_full(&Gd, stab, &sd, &ld) == 0);
        dense_logdet(&Gu, &ru, &du);
        dense_logdet(&Gd, &rd, &dd);
        CHECK(isfinite(lu) && isfinite(ld) && isfinite(du) && isfinite(dd));
        CHECK(su == ru);
        CHECK(sd == rd);
        CHECK_CLOSE(lu, du, 1e-10);
        CHECK_CLOSE(ld, dd, 1e-10);
        /* PH identity (spec 3.2): log|W| = 2 log|det(1+A_up)| - lambda sum s */
        long total = 0;
        for (int k = 0; k < f.L * f.n; k++) {
            total += f.s[k];
        }
        CHECK_CLOSE(lu + ld, 2.0 * lu - f.lambda * (double)total, 1e-10);
        field_flip_site_worldline(&f, 2);
    }
    /* state untouched */
    CHECK(Gu.cur_l == cur0);
    CHECK(Gu.det_sign == sign0);
    for (int k = 0; k < n * n; k++) {
        CHECK_CLOSE(Gu.g[k], g0[k], 0.0);
    }
    free(g0);
    green_free(&Gu);
    green_free(&Gd);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
}

int main(void)
{
    run_case(4.0, 8, 4, GREEN_REBUILD_COMBINE, 101);
    run_case(4.0, 10, 4, GREEN_REBUILD_COMBINE, 102);   /* stab does not divide L */
    run_case(4.0, 8, 3, GREEN_REBUILD_TWO_SIDED, 103);
    run_case(4.0, 8, 4, GREEN_REBUILD_CENTERED, 104);
    run_case(0.0, 8, 4, GREEN_REBUILD_COMBINE, 105);    /* U = 0 */
    run_case(4.0, 1, 4, GREEN_REBUILD_COMBINE, 106);    /* L = 1 */
    TEST_END();
}
