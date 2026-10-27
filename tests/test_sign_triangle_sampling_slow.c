/* Stochastic check of the negative-sign triangle (plan section 6.2 item 6):
   the production replica path samples |W| and the pooled estimator recovers
   the exact finite-dtau sign, E_hub, ntot and doublon of
   tests/fixtures/sign_triangle/oracle.txt.

   Fixed in advance: 8 replicas x 40000 measurements (nbin 20), nwarm 500,
   seeds below, forward sweeps without and with polarized global updates;
   each estimate must lie within 4 jackknife errors of the exact value and
   negative signs must be measured. The MCMC negative fraction (1 - sign)/2
   is compared with (1 - exact sign)/2, not with the 48/4096 count ratio. */
#include "test_util.h"
#include "io.h"
#include "lattice.h"
#include "profiler.h"
#include "replica.h"
#include "replica_run.h"
#include "sign_ratio.h"
#include "structure_factor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIX "tests/fixtures/sign_triangle/"
enum { NREP = 8 };

static double oracle_value(const char *name)
{
    FILE *fp = fopen(FIX "oracle.txt", "r");
    char key[64];
    char line[4096];
    double v = NAN;
    while (fp != NULL && fgets(line, sizeof line, fp) != NULL) {
        double x = 0.0;
        if (sscanf(line, "%63s %lf", key, &x) == 2 && strcmp(key, name) == 0) {
            v = x;
        }
    }
    if (fp != NULL) {
        fclose(fp);
    }
    return v;
}

static void run_case(const char *extra, unsigned long long seed)
{
    const char *path = "tests/tmp_sign_triangle_slow.in";
    FILE *fp = fopen(path, "w");
    fprintf(fp, "lattice=file\nlatfile=" FIX "triangle.txt\nU=4\nmu=2\n"
                "dtau=0.5\nbeta_list=2\nnwarm=500\nnmeas=40000\nnbin=20\n"
                "stab=3\nnrep=%d\nseed=%llu\n%s", NREP, seed, extra);
    fclose(fp);
    Params p;
    CHECK(params_read(&p, path) == 0);
    remove(path);
    Lattice L;
    CHECK(lattice_from_file(&L, p.latfile) == 0);
    StructureFactorPlan off = {0};
    ReplicaResult res[NREP];
    ReplicaBin bins[NREP * 20];
    for (int r = 0; r < NREP; r++) {
        Profiler prof;
        profiler_init_memory(&prof, 0);
        CHECK(dqmc_run_replica(&p, &L, 0, 4, p.mu, r, replica_seed(p.seed, 0, r),
                               &off, &off, &prof, &res[r]) == 0);
        memcpy(&bins[r * 20], res[r].bins, 20 * sizeof bins[0]);
    }
    double S[NREP * 20], A[NREP * 20];
    int count[NREP * 20];
    for (int b = 0; b < NREP * 20; b++) {
        S[b] = bins[b].sum_sign;
        count[b] = bins[b].count;
    }
    SignRatioBlocks blk;
    CHECK(sign_ratio_blocks_init(&blk, S, count, NREP, 20, 2000) == 0);
    CHECK(blk.status == SIGN_RATIO_OK);
    CHECK(blk.negative_count > 0);
    const double sign = oracle_value("average_sign");
    CHECK(fabs(blk.sign - sign) <= 4.0 * blk.dsign);
    CHECK(fabs(blk.f_neg - 0.5 * (1.0 - sign)) <= 4.0 * blk.df_neg);
    const char *names[3] = {"E_hub", "ntot", "doublon"};
    double est[3], err[3];
    for (int k = 0; k < 3; k++) {
        for (int b = 0; b < NREP * 20; b++) {
            A[b] = k == 0 ? bins[b].sum_sign_Ehub
                 : k == 1 ? bins[b].sum_sign_N : bins[b].sum_sign_D;
        }
        CHECK(sign_ratio_estimate(&blk, A, 1, &est[k], &err[k]) == 0);
        const double exact = oracle_value(names[k]);
        CHECK(fabs(est[k] - exact) <= 4.0 * err[k]);
        printf("  [%s] %s = %.6f +- %.6f (exact %.6f, %.1f sigma)\n",
               extra[0] ? "global" : "local", names[k], est[k], err[k], exact,
               (est[k] - exact) / err[k]);
    }
    printf("  [%s] sign = %.6f +- %.6f (exact %.6f), negative measurements %lld\n",
           extra[0] ? "global" : "local", blk.sign, blk.dsign, sign,
           blk.negative_count);
    for (int r = 0; r < NREP; r++) {
        replica_result_free(&res[r]);
    }
    lattice_free(&L);
}

int main(void)
{
    run_case("", 2026102701ULL);
    run_case("global_update=site\nglobal_interval=3\n"
             "global_site_select=polarized\nglobal_site_power=2\n",
             2026102702ULL);
    TEST_END();
}
