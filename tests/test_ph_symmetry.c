#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "model.h"
#include "structure_factor.h"

#include <stdlib.h>

static void check_close_mat(int n, const double *a, const double *b,
                            double tol)
{
    for (int i = 0; i < n * n; i++) {
        CHECK_CLOSE(a[i], b[i], tol);
    }
}

static void check_spin_structure_equal(const Lattice *L, const double *gu,
                                       const double *gd_direct,
                                       const double *gd_mapped)
{
    SzzPlan plan;
    CHECK(szz_plan_init(&plan, L, "all") == 0);
    SzzWorkspace work;
    CHECK(szz_workspace_init(&work, &plan) == 0);
    double *direct = malloc((size_t)plan.nq * sizeof(double));
    double *mapped = malloc((size_t)plan.nq * sizeof(double));
    double *sperp_direct = malloc((size_t)plan.nq * sizeof(double));
    double *sperp_mapped = malloc((size_t)plan.nq * sizeof(double));
    CHECK(direct != NULL && mapped != NULL && sperp_direct != NULL &&
          sperp_mapped != NULL);
    if (direct != NULL && mapped != NULL && sperp_direct != NULL &&
        sperp_mapped != NULL) {
        CHECK(measure_szz_sample(&plan, &work, gu, gd_direct, direct) == 0);
        CHECK(measure_szz_sample(&plan, &work, gu, gd_mapped, mapped) == 0);
        CHECK(measure_sperp_sample(&plan, &work, gu, gd_direct,
                                   sperp_direct) == 0);
        CHECK(measure_sperp_sample(&plan, &work, gu, gd_mapped,
                                   sperp_mapped) == 0);
        for (int q = 0; q < plan.nq; q++) {
            CHECK_CLOSE(direct[q], mapped[q], 1e-9);
            CHECK_CLOSE(sperp_direct[q], sperp_mapped[q], 1e-9);
        }
    }
    free(direct);
    free(mapped);
    free(sperp_direct);
    free(sperp_mapped);
    szz_workspace_free(&work);
    szz_plan_free(&plan);
}

static void run_case_chain(int Lx, int Ltr, double U, double dtau,
                           int l0, unsigned seed)
{
    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    CHECK(L.is_bipartite == 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, seed);
    Field f;
    field_init(&f, Lx, Ltr, U, dtau, &r);

    Green Gu;
    Green Gd;
    green_alloc(&Gu, &m, &f, 1.0);
    green_alloc(&Gd, &m, &f, -1.0);
    CHECK(green_from_scratch(&Gu, l0) == 0);
    CHECK(green_from_scratch(&Gd, l0) == 0);

    double *mapped = Gu.tmp;
    green_build_ph_down(&Gu, L.bipart, mapped);
    check_close_mat(Lx, mapped, Gd.g, 1e-9);
    check_spin_structure_equal(&L, Gu.g, Gd.g, mapped);

    green_free(&Gd);
    green_free(&Gu);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
}

static void run_case_square(int Lx, int Ly, int Ltr, double U, double dtau,
                            int l0, unsigned seed)
{
    Lattice L;
    lattice_square(&L, Lx, Ly, -1.0, 1);
    CHECK(L.is_bipartite == 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, seed);
    Field f;
    field_init(&f, L.n, Ltr, U, dtau, &r);

    Green Gu;
    Green Gd;
    green_alloc(&Gu, &m, &f, 1.0);
    green_alloc(&Gd, &m, &f, -1.0);
    CHECK(green_from_scratch(&Gu, l0) == 0);
    CHECK(green_from_scratch(&Gd, l0) == 0);

    double *mapped = Gu.tmp;
    green_build_ph_down(&Gu, L.bipart, mapped);
    check_close_mat(L.n, mapped, Gd.g, 1e-9);
    check_spin_structure_equal(&L, Gu.g, Gd.g, mapped);

    green_free(&Gd);
    green_free(&Gu);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
}

int main(void)
{
    run_case_chain(4, 12, 4.0, 0.1, 0, 111);
    run_case_chain(6, 18, 8.0, 0.05, 5, 222);
    run_case_square(4, 4, 16, 4.0, 0.1, 0, 333);
    run_case_square(2, 4, 20, 6.0, 0.05, 7, 444);
    TEST_END();
}
