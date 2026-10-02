#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "lattice.h"
#include "measure.h"
#include "model.h"
#include "structure_factor.h"

#include <math.h>
#include <stdlib.h>

/* U=0, mu=0 references built from momenta, independent of the lattice code.
   Single-particle momenta are k = 2 pi (m + s) / L with s = 1/2 along an
   antiperiodic direction and s = 0 along a periodic one; the spin momentum
   q = 2 pi mq / L is integer either way. A periodic direction of length 2
   carries the doubled bond, whose eigenvalues +-2t equal 2t cos k at k = 0, pi. */
static double band(int Lx, int Ly, double sx, double sy, int kx, int ky,
                   double thop)
{
    const double two_pi = 2.0 * acos(-1.0);
    double e = 2.0 * thop * cos(two_pi * ((double)kx + sx) / Lx);
    if (Ly > 1) {
        e += 2.0 * thop * cos(two_pi * ((double)ky + sy) / Ly);
    }
    return e;
}

static double fermi(double beta, double e)
{
    return 1.0 / (1.0 + exp(beta * e));
}

static double free_energy(int Lx, int Ly, double sx, double sy, double beta,
                          double thop)
{
    double sum = 0.0;
    for (int ky = 0; ky < Ly; ky++) {
        for (int kx = 0; kx < Lx; kx++) {
            const double e = band(Lx, Ly, sx, sy, kx, ky, thop);
            sum += e * fermi(beta, e);
        }
    }
    return 2.0 * sum;
}

static double free_szz(int Lx, int Ly, double sx, double sy, int mx, int my,
                       double beta, double thop)
{
    double sum = 0.0;
    for (int ky = 0; ky < Ly; ky++) {
        for (int kx = 0; kx < Lx; kx++) {
            const double f = fermi(beta, band(Lx, Ly, sx, sy, kx, ky, thop));
            const double fq = fermi(
                beta, band(Lx, Ly, sx, sy, (kx + mx) % Lx, (ky + my) % Ly, thop));
            sum += f * (1.0 - fq);
        }
    }
    return sum / (2.0 * (double)(Lx * Ly));
}

/* Measures E, Szz(q), Sperp(q) on the U=0 DQMC Green function of L and
   compares them with the momentum formulas for shifts (sx, sy). The periodic
   formulas (sx = sy = 0) must miss by more than 1e-3 somewhere. */
static void check_case(Lattice *L, double sx, double sy, unsigned long long seed)
{
    const double beta = 2.0;
    const double dtau = 0.1;
    const int Ltr = 20;
    const int Lx = L->Lx;
    const int Ly = L->Ly;
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

    const MeasSample s = measure_sample(L->n, L->t, 0.0, D.Gu.g, D.Gd.g);
    CHECK_CLOSE(s.E, free_energy(Lx, Ly, sx, sy, beta, -1.0), 1e-8);
    CHECK(fabs(s.E - free_energy(Lx, Ly, 0.0, 0.0, beta, -1.0)) > 1e-3);

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
        double miss = 0.0;
        for (int q = 0; q < plan.nq; q++) {
            const double exact =
                free_szz(Lx, Ly, sx, sy, plan.mx[q], plan.my[q], beta, -1.0);
            CHECK_CLOSE(szz[q], exact, 1e-8);
            CHECK_CLOSE(sperp[q], 2.0 * exact, 1e-8);
            const double periodic =
                free_szz(Lx, Ly, 0.0, 0.0, plan.mx[q], plan.my[q], beta, -1.0);
            if (fabs(szz[q] - periodic) > miss) {
                miss = fabs(szz[q] - periodic);
            }
        }
        CHECK(miss > 1e-3);
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
    Lattice L;
    CHECK(lattice_chain_bc(&L, 4, -1.0, LAT_BC_ANTIPERIODIC) == 0);
    check_case(&L, 0.5, 0.0, 11);
    lattice_free(&L);

    CHECK(lattice_square_bc(&L, 4, 4, -1.0, LAT_BC_ANTIPERIODIC,
                            LAT_BC_PERIODIC) == 0);
    check_case(&L, 0.5, 0.0, 12);
    lattice_free(&L);

    CHECK(lattice_square_bc(&L, 4, 2, -1.0, LAT_BC_ANTIPERIODIC,
                            LAT_BC_PERIODIC) == 0);
    check_case(&L, 0.5, 0.0, 13);
    lattice_free(&L);

    CHECK(lattice_square_bc(&L, 4, 4, -1.0, LAT_BC_ANTIPERIODIC,
                            LAT_BC_ANTIPERIODIC) == 0);
    check_case(&L, 0.5, 0.5, 14);
    lattice_free(&L);
    TEST_END();
}
