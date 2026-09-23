/* Slow end-to-end regression for the high-U sign collapse (invalid bin).
 *
 * Reproduces the diagnostic condition (kugui i2cpu job 840456) that, before the
 * stabilized-sign fix, produced `sum_sign == 0` -> "invalid bin" abort:
 *   chain Lx=4, U=12, dtau=0.1, beta=4 (Ltr=40), stab=8,
 *   nwarm=2000, nmeas=8000, nbin=40, seed=246813579.
 *
 * Pre-fix this is RED (some bin has sum_sign==0 so replica_bin_values != 0).
 * Post-fix the running sign is reset from the stabilized determinant each
 * sweep, so at half-filling every bin has sum_sign != 0 and sign ~ +1.
 *
 * Excluded from `make test`; run via `make test_slow`.
 */
#include "test_util.h"
#include "dqmc.h"
#include "io.h"
#include "lattice.h"
#include "profiler.h"
#include "replica.h"
#include "replica_run.h"
#include "rng.h"
#include "structure_factor.h"

#include <string.h>

int main(void)
{
    Params p = {0};
    p.Lx = 4;
    p.Ly = 1;
    p.pbc = 1;
    p.thop = -1.0;
    p.U = 12.0;
    p.dtau = 0.1;
    p.nwarm = 2000;
    p.nmeas = 8000;
    p.nbin = 40;
    p.stab_interval = 8;
    p.profile = 0;
    strcpy(p.sweep_order, "forward");
    strcpy(p.parallel, "serial");
    p.nrep = 1;
    p.seed = 246813579ULL;

    const double beta = 4.0;
    const int Ltr = (int)(beta / p.dtau + 0.5);
    const double mu = p.U / 2.0;

    Lattice L;
    lattice_chain(&L, p.Lx, p.thop, p.pbc);

    Profiler prof;
    profiler_init_memory(&prof, 0);

    ReplicaResult result;
    SzzPlan szz_plan = {0};
    StructureFactorPlan sperp_plan = {0};
    const int rc = dqmc_run_replica(&p, &L, 0, Ltr, mu, 0, p.seed,
                                    &szz_plan, &sperp_plan, &prof, &result);
    CHECK(rc == 0);

    if (rc == 0) {
        int valid_bins = 0;
        double sign_sum = 0.0;
        for (int bi = 0; bi < result.nbin; bi++) {
            double Ehub, Egc, Eph, N, D, sgn;
            const int ok = replica_bin_values(&result.bins[bi], &Ehub, &Egc,
                                              &Eph, &N, &D, &sgn);
            CHECK(ok == 0); /* pre-fix: some bin has sum_sign==0 -> ok!=0 */
            if (ok == 0) {
                valid_bins++;
                sign_sum += sgn;
                /* loose sanity: E_hub finite and of sensible magnitude */
                CHECK(Ehub > -50.0 && Ehub < 50.0);
            }
        }
        CHECK(valid_bins == result.nbin);
        if (valid_bins > 0) {
            const double sign_avg = sign_sum / (double)valid_bins;
            /* half-filling is sign-free: average sign must be ~ +1 */
            CHECK(sign_avg > 0.99);
        }
        replica_result_free(&result);
    }

    profiler_close(&prof);
    lattice_free(&L);

    TEST_END();
}
