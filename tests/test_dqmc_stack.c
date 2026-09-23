#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "model.h"

#include <math.h>

static void reference_sweep_from_scratch(Dqmc *D)
{
    if (D->status) {
        return;
    }
    const int n = D->n;
    const int L = D->L;

    for (int l = 0; l < L; l++) {
        for (int i = 0; i < n; i++) {
            const double Nu = green_flipN(&D->Gu, i);
            const double Nd = green_flipN(&D->Gd, i);
            const double Ru = green_ratio_N(&D->Gu, i, Nu);
            const double Rd = green_ratio_N(&D->Gd, i, Nd);
            const double R = Ru * Rd;

            if (rng_double(D->rng) < fabs(R)) {
                green_update(&D->Gu, i, Nu);
                green_update(&D->Gd, i, Nd);
                D->f->s[l * n + i] *= -1;
                if (R < 0.0) {
                    D->sign = -D->sign;
                }
            }
        }

        green_wrap(&D->Gu);
        green_wrap(&D->Gd);
        if (((l + 1) % D->stab_interval) == 0 && (l + 1) < L) {
            const int tau = l + 1;
            const int ru = green_from_scratch(&D->Gu, tau);
            const int rd = green_from_scratch(&D->Gd, tau);
            if (ru != 0 || rd != 0 || D->Gu.det_sign == 0 ||
                D->Gd.det_sign == 0) {
                D->status = 1;
                return;
            }
            D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);
        }
    }

    const int ru = green_from_scratch(&D->Gu, 0);
    const int rd = green_from_scratch(&D->Gd, 0);
    if (ru != 0 || rd != 0 || D->Gu.det_sign == 0 || D->Gd.det_sign == 0) {
        D->status = 1;
        return;
    }
    D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);
}

static void check_close_mat(int n, const double *a, const double *b,
                            double tol)
{
    for (int i = 0; i < n * n; i++) {
        CHECK_CLOSE(a[i], b[i], tol);
    }
}

static void run_case_chain_mode(int Ltr, int stab, double U, int sweeps,
                                unsigned seed, GreenRebuildMode mode)
{
    const int Lx = 4;
    const double dtau = 0.1;
    Lattice L_new;
    Lattice L_ref;
    lattice_chain(&L_new, Lx, -1.0, 1);
    lattice_chain(&L_ref, Lx, -1.0, 1);
    Model m_new;
    Model m_ref;
    model_init(&m_new, &L_new, U, dtau, 1, 0.0);
    model_init(&m_ref, &L_ref, U, dtau, 1, 0.0);
    m_ref.ph_symmetric = 0;

    Rng r_new;
    Rng r_ref;
    rng_seed(&r_new, seed);
    rng_seed(&r_ref, seed);
    Field f_new;
    Field f_ref;
    field_init(&f_new, Lx, Ltr, U, dtau, &r_new);
    field_init(&f_ref, Lx, Ltr, U, dtau, &r_ref);

    Dqmc D_new;
    Dqmc D_ref;
    CHECK(dqmc_init_modes(&D_new, &m_new, &f_new, &r_new, stab, NULL,
                          DQMC_SWEEP_FORWARD, mode) == 0);
    dqmc_init(&D_ref, &m_ref, &f_ref, &r_ref, stab, NULL);
    CHECK(D_new.status == 0);
    CHECK(D_ref.status == 0);
    CHECK(D_new.use_ph == 1);
    CHECK(D_ref.use_ph == 0);
    CHECK(D_new.green_rebuild_mode == mode);
    CHECK(D_new.Gu.rebuild_mode == mode);
    CHECK(D_new.Gd.rebuild_mode == mode);

    for (int sweep = 0; sweep < sweeps; sweep++) {
        dqmc_sweep(&D_new);
        reference_sweep_from_scratch(&D_ref);
        CHECK(D_new.status == D_ref.status);
        CHECK_CLOSE(D_new.sign, D_ref.sign, 0.0);
        CHECK(D_new.Gu.det_sign == D_ref.Gu.det_sign);
        CHECK(D_new.Gd.det_sign == D_ref.Gd.det_sign);
        CHECK(D_new.Gu.cur_l == D_ref.Gu.cur_l);
        CHECK(D_new.Gd.cur_l == D_ref.Gd.cur_l);
        for (int i = 0; i < Lx * Ltr; i++) {
            CHECK(f_new.s[i] == f_ref.s[i]);
        }
        check_close_mat(Lx, D_new.Gu.g, D_ref.Gu.g, 1e-9);
        check_close_mat(Lx, D_new.Gd.g, D_ref.Gd.g, 1e-9);
    }

    dqmc_free(&D_ref);
    dqmc_free(&D_new);
    field_free(&f_ref);
    field_free(&f_new);
    model_free(&m_ref);
    model_free(&m_new);
    lattice_free(&L_ref);
    lattice_free(&L_new);
}

static void run_case_chain(int Ltr, int stab, double U, int sweeps,
                           unsigned seed)
{
    run_case_chain_mode(Ltr, stab, U, sweeps, seed, GREEN_REBUILD_COMBINE);
}

static void run_case_square(int Ltr, int stab, double U, int sweeps,
                            unsigned seed)
{
    const int Lx = 2;
    const int Ly = 4;
    const double dtau = 0.1;
    Lattice L_new;
    Lattice L_ref;
    lattice_square(&L_new, Lx, Ly, -1.0, 1);
    lattice_square(&L_ref, Lx, Ly, -1.0, 1);
    const int n = L_new.n;
    Model m_new;
    Model m_ref;
    model_init(&m_new, &L_new, U, dtau, 1, 0.0);
    model_init(&m_ref, &L_ref, U, dtau, 1, 0.0);
    m_ref.ph_symmetric = 0;

    Rng r_new;
    Rng r_ref;
    rng_seed(&r_new, seed);
    rng_seed(&r_ref, seed);
    Field f_new;
    Field f_ref;
    field_init(&f_new, n, Ltr, U, dtau, &r_new);
    field_init(&f_ref, n, Ltr, U, dtau, &r_ref);

    Dqmc D_new;
    Dqmc D_ref;
    dqmc_init(&D_new, &m_new, &f_new, &r_new, stab, NULL);
    dqmc_init(&D_ref, &m_ref, &f_ref, &r_ref, stab, NULL);
    CHECK(D_new.status == 0);
    CHECK(D_ref.status == 0);
    CHECK(D_new.use_ph == 1);
    CHECK(D_ref.use_ph == 0);

    for (int sweep = 0; sweep < sweeps; sweep++) {
        dqmc_sweep(&D_new);
        reference_sweep_from_scratch(&D_ref);
        CHECK(D_new.status == D_ref.status);
        CHECK_CLOSE(D_new.sign, D_ref.sign, 0.0);
        CHECK(D_new.Gu.det_sign == D_ref.Gu.det_sign);
        CHECK(D_new.Gd.det_sign == D_ref.Gd.det_sign);
        CHECK(D_new.Gu.cur_l == D_ref.Gu.cur_l);
        CHECK(D_new.Gd.cur_l == D_ref.Gd.cur_l);
        for (int i = 0; i < n * Ltr; i++) {
            CHECK(f_new.s[i] == f_ref.s[i]);
        }
        check_close_mat(n, D_new.Gu.g, D_ref.Gu.g, 1e-9);
        check_close_mat(n, D_new.Gd.g, D_ref.Gd.g, 1e-9);
    }

    dqmc_free(&D_ref);
    dqmc_free(&D_new);
    field_free(&f_ref);
    field_free(&f_new);
    model_free(&m_ref);
    model_free(&m_new);
    lattice_free(&L_ref);
    lattice_free(&L_new);
}

int main(void)
{
    run_case_chain(10, 4, 4.0, 4, 101);
    run_case_chain(5, 8, 4.0, 4, 202);
    run_case_chain(8, 8, 4.0, 4, 303);
    run_case_chain(8, 4, 8.0, 3, 404);
    run_case_chain(6, 1, 4.0, 3, 505);
    run_case_chain_mode(10, 4, 4.0, 3, 707, GREEN_REBUILD_TWO_SIDED);
    run_case_chain_mode(10, 4, 4.0, 3, 708, GREEN_REBUILD_CENTERED);
    run_case_square(8, 4, 4.0, 3, 606);
    TEST_END();
}
