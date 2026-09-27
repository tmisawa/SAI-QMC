#include "replica_run.h"

#include <stdio.h>

#include "dqmc.h"
#include "replica_chain.h"

static int append_stab_drift_row(const Params *p, int beta_index, int Ltr,
                                 int replica_id, unsigned long long seed,
                                 const Dqmc *D)
{
    if (p->stab_drift_file[0] == '\0') {
        return 0;
    }

    FILE *fp = fopen(p->stab_drift_file, "a");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: failed to open stab_drift_file %s\n",
                p->stab_drift_file);
        return 1;
    }

    const DqmcStabDrift *drift = &D->stab_drift;
    const double mean =
        (drift->samples > 0) ? drift->sum_inf / (double)drift->samples : 0.0;
    fprintf(fp,
            "%d %d %d %llu %.17g %.17g %d %d %llu %.17g %.17g %d %llu %d\n",
            beta_index, Ltr, replica_id, seed, p->U, p->dtau,
            p->stab_interval, D->use_ph, drift->samples, drift->max_inf, mean,
            drift->max_tau, drift->max_sweep, drift->failed);
    if (fclose(fp) != 0) {
        fprintf(stderr, "ERROR: failed to close stab_drift_file %s\n",
                p->stab_drift_file);
        return 1;
    }
    return 0;
}

int dqmc_run_replica(const Params *p, const Lattice *L, int beta_index,
                     int Ltr, double mu, int replica_id,
                     unsigned long long seed,
                     const StructureFactorPlan *szz_plan,
                     const StructureFactorPlan *sperp_plan, Profiler *prof,
                     ReplicaResult *result)
{
    if (p == NULL || L == NULL || szz_plan == NULL || sperp_plan == NULL ||
        result == NULL) {
        return 1;
    }
    Profiler *old_prof = profiler_current();
    profiler_set_current(prof);
    ReplicaChain c;
    if (replica_chain_init(&c, p, L, beta_index, Ltr, p->dtau, replica_id,
                           seed, prof) != 0) {
        profiler_set_current(old_prof);
        return 1;
    }
    for (int w = 0; w < p->nwarm; w++) {
        (void)replica_chain_step(&c);
    }

    if (c.D.status != 0 || !replica_chain_state_is_finite(&c)) {
        fprintf(stderr,
                "ERROR: dqmc warmup numerical breakdown "
                "(beta_index=%d beta=%.17g T=%.17g replica=%d seed=%llu "
                "U=%g dtau=%g Ltr=%d stab=%d sweep_count=%llu status=%d "
                "failure_reason=%s)\n",
                beta_index, c.beta, c.T, replica_id, seed, p->U, c.dtau, Ltr,
                p->stab_interval, c.D.sweep_count, c.D.status,
                linalg_failure_reason_string(replica_chain_failure_reason(&c)));
        replica_chain_free(&c);
        profiler_set_current(old_prof);
        return 1;
    }

    if (replica_chain_measure_begin(&c, szz_plan, sperp_plan, result) != 0) {
        replica_chain_free(&c);
        profiler_set_current(old_prof);
        return 1;
    }
    const int use_site_diag =
        c.use_global && p->global_site_diag_file[0] != '\0';

    const int per = p->nmeas / p->nbin;
    profiler_phase_set(prof, PROF_PHASE_MEASUREMENT);
    for (int bi = 0; bi < p->nbin; bi++) {
        for (int k = 0; k < per; k++) {
            const unsigned long long attempts0 = c.D.accept_attempts;
            const unsigned long long accepted0 = c.D.accept_accepted;
            dqmc_sweep(&c.D);
            const unsigned long long gatt0 = c.D.global_attempts;
            const unsigned long long gacc0 = c.D.global_accepted;
            (void)replica_chain_global_after_sweep(&c);
            if (replica_chain_measure(&c, result, bi, k, mu, attempts0,
                                      accepted0, gatt0, gacc0) != 0) {
                replica_result_free(result);
                replica_chain_free(&c);
                profiler_set_current(old_prof);
                return 1;
            }
        }
    }

    if (use_site_diag) {
        *result->site_diag = *c.D.site_diag;
    }
    result->status = 0;
    if (append_stab_drift_row(p, beta_index, Ltr, replica_id, seed, &c.D) != 0) {
        replica_result_free(result);
        replica_chain_free(&c);
        profiler_set_current(old_prof);
        return 1;
    }
    replica_chain_free(&c);
    profiler_set_current(old_prof);
    return 0;
}
