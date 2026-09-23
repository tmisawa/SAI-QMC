#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "green.h"
#include "io.h"
#include "lattice.h"
#include "model.h"
#include "profiler.h"
#include "replica.h"
#include "replica_run.h"
#include "structure_factor.h"

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
        if (fabs(d) > max_abs) {
            max_abs = fabs(d);
        }
        num += d * d;
        den += b[i] * b[i];
    }
    const double rel = sqrt(num) / (sqrt(den) + 1e-30);
    CHECK(max_abs < tol || rel < tol);
}

static void check_exact_ph_state(Dqmc *D, double tol)
{
    Green ref;
    green_alloc(&ref, D->m, D->f, 1.0);
    CHECK(green_from_scratch(&ref, 0) == 0);
    CHECK(D->Gu.cur_l == 0);
    CHECK(D->Gd.cur_l == 0);
    check_close_mat(D->n, D->Gu.g, ref.g, tol);
    CHECK(D->Gu.det_sign == ref.det_sign);
    CHECK_CLOSE(D->sign, 1.0, 1e-12);

    double *gd = malloc(sizeof(double) * (size_t)D->n * (size_t)D->n);
    green_build_ph_down(&D->Gu, D->m->bipart, gd);
    check_close_mat(D->n, D->Gd.g, gd, tol);
    free(gd);
    green_free(&ref);
}

static void run_alternating_case_mode(Lattice *L, int Ltr, int stab,
                                      int sweeps, unsigned long long seed,
                                      GreenRebuildMode mode)
{
    const double U = 4.0;
    const double dtau = 0.1;
    Model m;
    model_init(&m, L, U, dtau, 1, 0.0);
    CHECK(m.ph_symmetric == 1);

    Rng r;
    rng_seed(&r, seed);
    Field f;
    field_init(&f, L->n, Ltr, U, dtau, &r);

    Dqmc D;
    CHECK(dqmc_init_modes(&D, &m, &f, &r, stab, NULL,
                          DQMC_SWEEP_ALTERNATING, mode) == 0);
    CHECK(D.status == 0);
    CHECK(D.sweep_mode == DQMC_SWEEP_ALTERNATING);
    CHECK(D.sweep_dir == DQMC_DIR_FORWARD);
    CHECK(D.green_rebuild_mode == mode);
    CHECK(D.Gu.rebuild_mode == mode);
    CHECK(D.Gd.rebuild_mode == mode);
    CHECK(dqmc_enable_stab_drift(&D, 1) == 0);
    CHECK(D.stab_drift.Gu_ref.rebuild_mode == mode);

    for (int sweep = 0; sweep < sweeps; sweep++) {
        if (sweep == 1) {
            D.carried_prefix_valid = 0;
        }
        if (sweep == 2) {
            D.carried_suffix_valid = 0;
        }
        dqmc_sweep(&D);
        CHECK(D.status == 0);
        check_exact_ph_state(&D, 1e-9);
        if ((D.sweep_count % 2ULL) == 1ULL) {
            CHECK(D.sweep_dir == DQMC_DIR_BACKWARD);
            CHECK(D.carried_prefix_valid == 1);
            CHECK(D.carried_suffix_valid == 0);
        } else {
            CHECK(D.sweep_dir == DQMC_DIR_FORWARD);
            CHECK(D.carried_prefix_valid == 0);
            CHECK(D.carried_suffix_valid == 1);
        }
    }

    const int samples_per_sweep = (Ltr - 1) / stab + 1;
    CHECK(D.stab_drift.failed == 0);
    CHECK(D.stab_drift.samples ==
          (unsigned long long)(samples_per_sweep * sweeps));
    CHECK(D.stab_drift.max_inf < 1e-7);

    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
}

static void run_alternating_case(Lattice *L, int Ltr, int stab, int sweeps,
                                 unsigned long long seed)
{
    run_alternating_case_mode(L, Ltr, stab, sweeps, seed,
                              GREEN_REBUILD_COMBINE);
}

static void check_non_ph_rejects_alternating(void)
{
    const double U = 4.0;
    const double dtau = 0.1;
    Lattice L;
    lattice_chain(&L, 3, -1.0, 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);
    CHECK(m.ph_symmetric == 0);

    Rng r;
    rng_seed(&r, 2026);
    Field f;
    field_init(&f, L.n, 5, U, dtau, &r);

    Dqmc D;
    CHECK(dqmc_init_mode(&D, &m, &f, &r, 2, NULL,
                         DQMC_SWEEP_ALTERNATING) != 0);
    CHECK(D.status != 0);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
}

static void check_replica_run_alternating(void)
{
    Params p = {0};
    strcpy(p.sweep_order, "alternating");
    strcpy(p.parallel, "serial");
    p.Lx = 4;
    p.Ly = 1;
    p.pbc = 1;
    p.thop = -1.0;
    p.U = 4.0;
    p.dtau = 0.1;
    p.nwarm = 1;
    p.nmeas = 4;
    p.nbin = 2;
    p.stab_interval = 2;
    p.profile = 0;
    p.nrep = 1;
    p.seed = 123456ULL;

    Lattice L;
    lattice_chain(&L, p.Lx, p.thop, p.pbc);

    Profiler prof;
    profiler_init_memory(&prof, 0);
    ReplicaResult result;
    SzzPlan szz_plan = {0};
    StructureFactorPlan sperp_plan = {0};
    const int Ltr = 4;
    const double mu = p.U / 2.0;
    CHECK(dqmc_run_replica(&p, &L, 0, Ltr, mu, 0, p.seed, &szz_plan,
                           &sperp_plan, &prof, &result) == 0);
    CHECK(result.status == 0);
    replica_result_free(&result);
    profiler_close(&prof);
    lattice_free(&L);
}

int main(void)
{
    Lattice chain;
    lattice_chain(&chain, 4, -1.0, 1);
    run_alternating_case(&chain, 10, 4, 5, 9001);
    run_alternating_case(&chain, 5, 8, 4, 9002);
    run_alternating_case(&chain, 6, 1, 4, 9003);
    run_alternating_case(&chain, 4, 4, 4, 9004);
    run_alternating_case_mode(&chain, 10, 4, 3, 9005,
                              GREEN_REBUILD_TWO_SIDED);
    run_alternating_case_mode(&chain, 10, 4, 3, 9006,
                              GREEN_REBUILD_CENTERED);
    lattice_free(&chain);

    Lattice square;
    lattice_square(&square, 2, 4, -1.0, 1);
    run_alternating_case(&square, 8, 4, 4, 9011);
    lattice_free(&square);

    check_non_ph_rejects_alternating();
    check_replica_run_alternating();
    TEST_END();
}
