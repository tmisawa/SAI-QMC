#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "lattice.h"
#include "measure.h"
#include "model.h"
#include "structure_factor.h"

#include <math.h>
#include <stdlib.h>

extern void dsyev_(const char *, const char *, const int *, double *,
                   const int *, double *, double *, const int *, int *);

static double free_szz(const Lattice *L, int mx, int my, double beta,
                       double thop)
{
    const double two_pi = 2.0 * acos(-1.0);
    double sum = 0.0;
    for (int ky = 0; ky < L->Ly; ky++) {
        for (int kx = 0; kx < L->Lx; kx++) {
            const int kqx = (kx + mx) % L->Lx;
            const int kqy = (ky + my) % L->Ly;
            double eps = 2.0 * thop * cos(two_pi * (double)kx / L->Lx);
            double eps_q =
                2.0 * thop * cos(two_pi * (double)kqx / L->Lx);
            if (L->type == LAT_SQUARE && L->Ly > 1) {
                eps += 2.0 * thop * cos(two_pi * (double)ky / L->Ly);
                eps_q +=
                    2.0 * thop * cos(two_pi * (double)kqy / L->Ly);
            }
            const double f = 1.0 / (1.0 + exp(beta * eps));
            const double f_q = 1.0 / (1.0 + exp(beta * eps_q));
            sum += f * (1.0 - f_q);
        }
    }
    return sum / (2.0 * (double)L->n);
}

static void check_free_szz(Lattice *L, double beta, double dtau,
                           unsigned long long seed)
{
    const int Ltr = (int)lround(beta / dtau);
    Model m;
    model_init(&m, L, 0.0, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, seed);
    Field f;
    field_init(&f, L->n, Ltr, 0.0, dtau, &r);
    Dqmc D;
    dqmc_init(&D, &m, &f, &r, 4, NULL);
    for (int w = 0; w < 3; w++) {
        dqmc_sweep(&D);
    }
    CHECK(green_from_scratch(&D.Gu, 0) == 0);
    CHECK(green_from_scratch(&D.Gd, 0) == 0);

    SzzPlan plan;
    CHECK(szz_plan_init(&plan, L, "all") == 0);
    SzzWorkspace work;
    StructureFactorWorkspace sperp_work;
    CHECK(szz_workspace_init(&work, &plan) == 0);
    CHECK(structure_factor_workspace_init(&sperp_work, &plan) == 0);
    double *szz = malloc((size_t)plan.nq * sizeof(double));
    double *sperp = malloc((size_t)plan.nq * sizeof(double));
    CHECK(szz != NULL && sperp != NULL);
    if (szz != NULL && sperp != NULL) {
        CHECK(measure_szz_sample(&plan, &work, D.Gu.g, D.Gd.g, szz) == 0);
        CHECK(measure_sperp_sample(&plan, &sperp_work, D.Gu.g, D.Gd.g,
                                   sperp) == 0);
        for (int q = 0; q < plan.nq; q++) {
            const double exact =
                free_szz(L, plan.mx[q], plan.my[q], beta, -1.0);
            CHECK_CLOSE(szz[q], exact, 1e-8);
            CHECK_CLOSE(sperp[q], 2.0 * exact, 2e-8);
        }
    }
    free(szz);
    free(sperp);
    szz_workspace_free(&work);
    structure_factor_workspace_free(&sperp_work);
    szz_plan_free(&plan);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
}

int main(void)
{
    const int Lx = 4;
    const double dtau = 0.1;
    const double beta = 2.0;
    const int Ltr = 20;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, 0.0, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 42);
    Field f;
    field_init(&f, Lx, Ltr, 0.0, dtau, &r);
    Dqmc D;
    dqmc_init(&D, &m, &f, &r, 8, NULL);

    for (int w = 0; w < 20; w++) {
        dqmc_sweep(&D);
    }
    green_from_scratch(&D.Gu, 0);
    green_from_scratch(&D.Gd, 0);
    MeasSample s = measure_sample(Lx, L.t, 0.0, D.Gu.g, D.Gd.g);
    CHECK_CLOSE(D.sign, 1.0, 1e-12);

    double Kc[16];
    for (int i = 0; i < 16; i++) {
        Kc[i] = m.K[i];
    }
    double eig[4];
    double work[256];
    int n = 4;
    int lwork = 256;
    int info = 0;
    dsyev_("N", "U", &n, Kc, &n, eig, work, &lwork, &info);
    CHECK(info == 0);

    double ek = 0.0;
    for (int i = 0; i < 4; i++) {
        const double fe = 1.0 / (1.0 + exp(beta * eig[i]));
        ek += eig[i] * fe;
    }
    ek *= 2.0;
    CHECK_CLOSE(s.E, ek, 1e-7);

    SzzPlan chain_plan;
    CHECK(szz_plan_init(&chain_plan, &L, "all") == 0);
    SzzWorkspace chain_work;
    StructureFactorWorkspace chain_sperp_work;
    CHECK(szz_workspace_init(&chain_work, &chain_plan) == 0);
    CHECK(structure_factor_workspace_init(&chain_sperp_work, &chain_plan) ==
          0);
    double chain_szz[4];
    double chain_sperp[4];
    CHECK(measure_szz_sample(&chain_plan, &chain_work, D.Gu.g, D.Gd.g,
                             chain_szz) == 0);
    CHECK(measure_sperp_sample(&chain_plan, &chain_sperp_work, D.Gu.g,
                               D.Gd.g, chain_sperp) == 0);
    for (int q = 0; q < chain_plan.nq; q++) {
        CHECK_CLOSE(chain_szz[q],
                    free_szz(&L, chain_plan.mx[q], chain_plan.my[q], beta,
                             -1.0),
                    1e-8);
        CHECK_CLOSE(chain_sperp[q], 2.0 * chain_szz[q], 1e-8);
    }
    szz_workspace_free(&chain_work);
    structure_factor_workspace_free(&chain_sperp_work);
    szz_plan_free(&chain_plan);

    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    Lattice square;
    lattice_square(&square, 2, 2, -1.0, 1);
    check_free_szz(&square, beta, dtau, 7);
    check_free_szz(&square, beta, dtau, 991);
    lattice_free(&square);
    TEST_END();
}
