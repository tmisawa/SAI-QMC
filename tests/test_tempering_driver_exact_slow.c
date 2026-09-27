/* Slow regression: the integrated ladder driver dqmc_run_ladder, run with the
 * option sets of production tempering runs, reproduces exact finite-dtau
 * averages.
 *
 * An open 2-site chain at U=4 with three slots, beta = 0.4, 0.8, 1.2, sharing
 * Ltr=4 time slices (dtau_k = beta_k/4) has 2^8 = 256 Hubbard-Stratonovich
 * configurations per slot, so each slot's exact expectation of the measured
 * energy E and double occupancy D follows from full enumeration with
 * dqmc_log_weight (checked against direct determinants in
 * test_dqmc_tempering_weight). Three driver option sets are compared:
 *   (a) forward sweeps, no global update, an exchange round every sweep;
 *   (b) alternating sweeps with global_update=site every 3 sweeps and
 *       stab=2 < Ltr, so carried stacks are reused between exchanges;
 *   (c) alternating sweeps with the polarized global update every 2 sweeps
 *       and tempering_interval=3.
 * Each case runs NLADDER independent ladders from one fixed seed. The mean of
 * the per-ladder averages is compared with the exact value in units of the
 * ladder-to-ladder standard error (NLADDER-1 degrees of freedom). The pass
 * threshold is |z| < 4: a correct implementation exceeds it with probability
 * about 6.3e-5 per comparison (normal tail; the Student-t correction is
 * negligible at this NLADDER), so across the 18 comparisons (3 cases x
 * 3 slots x {E, D}) the Bonferroni bound on a false alarm is about 1.2e-3.
 * Each case also checks the exchange schedule (measurement-phase attempts per
 * pair), that exchanges are accepted, and that the global update ran.
 *
 * Excluded from `make test`; run via `make test_slow`.
 */
#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "green.h"
#include "io.h"
#include "lattice.h"
#include "measure.h"
#include "model.h"
#include "replica.h"
#include "rng.h"
#include "structure_factor.h"
#include "tempering_run.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { NS = 2, LT = 4, NC = 256, K = 3, NLADDER = 800 };
static const double BETA[K] = {0.4, 0.8, 1.2};
static const double U = 4.0;
static const double ZMAX = 4.0;
static const unsigned long long SEED = 20260928ULL;

/* Exact finite-dtau averages of the measured E and D of one slot. */
static void exact_slot(const Lattice *L, double dtau, double *Eex, double *Dex)
{
    Model m;
    model_init(&m, L, U, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 1);
    Field f;
    field_init(&f, NS, LT, U, dtau, &r);
    Dqmc D;
    CHECK(dqmc_init_modes(&D, &m, &f, &r, 2, NULL, DQMC_SWEEP_FORWARD,
                          GREEN_REBUILD_COMBINE) == 0);
    double lw[NC], E[NC], Dd[NC], wmax = -1e300;
    double gd[NS * NS];
    for (int c = 0; c < NC; c++) {
        for (int j = 0; j < NS * LT; j++) {
            f.s[j] = (c >> j & 1) ? 1 : -1;
        }
        int sg = 0;
        CHECK(dqmc_log_weight(&D, &lw[c], &sg) == 0);
        CHECK(green_from_scratch(&D.Gu, 0) == 0);
        green_build_ph_down(&D.Gu, m.bipart, gd);
        const MeasSample s = measure_sample(NS, L->t, U, D.Gu.g, gd);
        E[c] = s.E;
        Dd[c] = s.doublon;
        wmax = lw[c] > wmax ? lw[c] : wmax;
    }
    double Z = 0.0, sE = 0.0, sD = 0.0;
    for (int c = 0; c < NC; c++) {
        const double w = exp(lw[c] - wmax);
        Z += w;
        sE += w * E[c];
        sD += w * Dd[c];
    }
    *Eex = sE / Z;
    *Dex = sD / Z;
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
}

/* Case (a); (b) and (c) change only the fields they name. */
static void base_params(Params *p)
{
    memset(p, 0, sizeof *p);
    strcpy(p->lattice, "chain");
    p->Lx = NS;
    p->Ly = 1;
    p->pbc = 0;
    p->thop = -1.0;
    p->U = U;
    for (int k = 0; k < K; k++) {
        p->beta_list[k] = BETA[k];
    }
    p->nbeta = K;
    p->nwarm = 200;
    p->nmeas = 3000;
    p->nbin = 3;
    p->stab_interval = 2;
    strcpy(p->sweep_order, "forward");
    strcpy(p->green_rebuild, "combine");
    strcpy(p->parallel, "serial");
    strcpy(p->global_update, "none");
    p->global_interval = 100;
    strcpy(p->global_site_select, "fixed");
    p->global_site_power = 2.0;
    strcpy(p->tempering, "dtau_ladder");
    p->tempering_ltr = LT;
    p->tempering_interval = 1;
    strcpy(p->field_init, "random");
    p->nrep = NLADDER;
    p->seed = SEED;
}

/* Measurement-phase attempts of pair k in one ladder, from the documented
   schedule: a round follows every ladder sweep s (warmup included) with
   s % tempering_interval == 0, and round r tries the pairs of parity r & 1. */
static unsigned long long expected_attempts(const Params *p, int k)
{
    unsigned long long round = 0ULL, n = 0ULL;
    for (int s = 1; s <= p->nwarm + p->nmeas; s++) {
        if (s % p->tempering_interval != 0) {
            continue;
        }
        if (s > p->nwarm && (int)(round & 1ULL) == (k & 1)) {
            n++;
        }
        round++;
    }
    return n;
}

static void mean_se(const double *x, double *mean, double *se)
{
    double a = 0.0, v = 0.0;
    for (int l = 0; l < NLADDER; l++) {
        a += x[l];
    }
    a /= NLADDER;
    for (int l = 0; l < NLADDER; l++) {
        v += (x[l] - a) * (x[l] - a);
    }
    *mean = a;
    *se = sqrt(v / (NLADDER - 1) / NLADDER);
}

/* Returns the largest |z| of the case, or a negative value on a failure. */
static double run_case(const char *name, const Params *p, const Lattice *L,
                       const double *Eex, const double *Dex, int want_global)
{
    StructureFactorPlan off;
    memset(&off, 0, sizeof off);          /* no spin measurements */
    double *mE = calloc((size_t)K * NLADDER, sizeof *mE);
    double *mD = calloc((size_t)K * NLADDER, sizeof *mD);
    if (mE == NULL || mD == NULL) {
        free(mE);
        free(mD);
        return -1.0;
    }
    unsigned long long want[K - 1];
    for (int k = 0; k < K - 1; k++) {
        want[k] = expected_attempts(p, k);
    }
    unsigned long long acc = 0ULL, att = 0ULL, gatt = 0ULL;
    int schedule_ok = 1;
    const clock_t t0 = clock();
    for (int l = 0; l < NLADDER; l++) {
        ReplicaResult res[K];
        memset(res, 0, sizeof res);
        TemperingLadderOut out;
        if (tempering_ladder_out_alloc(&out, K, p->nbin) != 0) {
            free(mE);
            free(mD);
            return -1.0;
        }
        if (dqmc_run_ladder(p, L, U / 2.0, l, &off, &off, res, &out) != 0) {
            printf("%s: ladder %d failed\n", name, l);
            tempering_ladder_out_free(&out);
            free(mE);
            free(mD);
            return -1.0;
        }
        for (int k = 0; k < K; k++) {
            double ss = 0.0, sE = 0.0, sD = 0.0;
            for (int b = 0; b < p->nbin; b++) {
                ss += res[k].bins[b].sum_sign;
                sE += res[k].bins[b].sum_sign_Ehub;
                sD += res[k].bins[b].sum_sign_D;
                gatt += res[k].bins[b].global_attempts;
            }
            mE[k * NLADDER + l] = sE / ss;
            mD[k * NLADDER + l] = sD / ss;
            replica_result_free(&res[k]);
        }
        for (int k = 0; k < K - 1; k++) {
            unsigned long long a = 0ULL;
            for (int b = 0; b < p->nbin; b++) {
                a += out.bin_attempts[b * (K - 1) + k];
                acc += out.bin_accepted[b * (K - 1) + k];
            }
            att += a;
            schedule_ok &= a == want[k];
        }
        tempering_ladder_out_free(&out);
    }
    const double seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%s: %d ladders, %.1f s, pair acceptance %.3f, global attempts %llu\n",
           name, NLADDER, seconds, att ? (double)acc / (double)att : 0.0, gatt);
    double zmax = 0.0;
    for (int k = 0; k < K; k++) {
        double aE, seE, aD, seD;
        mean_se(&mE[k * NLADDER], &aE, &seE);
        mean_se(&mD[k * NLADDER], &aD, &seD);
        const double zE = (aE - Eex[k]) / seE;
        const double zD = (aD - Dex[k]) / seD;
        printf("  slot %d beta=%.1f  E=%.6f(%.6f) exact %.6f z=%+.2f   "
               "D=%.6f(%.6f) exact %.6f z=%+.2f\n",
               k, BETA[k], aE, seE, Eex[k], zE, aD, seD, Dex[k], zD);
        /* written so that a NaN z fails */
        if (!(fabs(zE) < ZMAX) || !(fabs(zD) < ZMAX)) {
            printf("FAIL %s: slot %d |z| >= %.1f\n", name, k, ZMAX);
            zmax = -1.0;
        } else if (zmax >= 0.0) {
            zmax = fmax(zmax, fmax(fabs(zE), fabs(zD)));
        }
    }
    if (!schedule_ok) {
        printf("FAIL %s: measurement attempts per pair differ from %llu,%llu\n",
               name, want[0], want[1]);
        zmax = -1.0;
    }
    if (acc == 0ULL || acc == att) {
        printf("FAIL %s: exchanges never or always accepted\n", name);
        zmax = -1.0;
    }
    if ((gatt > 0ULL) != (want_global != 0)) {
        printf("FAIL %s: global update %s\n", name, want_global ? "never ran" : "ran");
        zmax = -1.0;
    }
    free(mE);
    free(mD);
    return zmax;
}

int main(void)
{
    Lattice L;
    lattice_chain(&L, NS, -1.0, 0);
    double Eex[K], Dex[K];
    for (int k = 0; k < K; k++) {
        exact_slot(&L, BETA[k] / LT, &Eex[k], &Dex[k]);
    }
    Params p;
    double z;

    base_params(&p);
    z = run_case("(a) forward, no global update, interval 1", &p, &L, Eex, Dex, 0);
    CHECK(z >= 0.0);

    base_params(&p);
    strcpy(p.sweep_order, "alternating");
    strcpy(p.global_update, "site");
    p.global_interval = 3;
    z = run_case("(b) alternating, global site every 3, stab 2 < Ltr", &p, &L, Eex, Dex, 1);
    CHECK(z >= 0.0);

    base_params(&p);
    strcpy(p.sweep_order, "alternating");
    strcpy(p.global_update, "site");
    p.global_interval = 2;
    strcpy(p.global_site_select, "polarized");
    p.tempering_interval = 3;
    z = run_case("(c) alternating, polarized global every 2, interval 3", &p, &L, Eex, Dex, 1);
    CHECK(z >= 0.0);

    lattice_free(&L);
    TEST_END();
}
