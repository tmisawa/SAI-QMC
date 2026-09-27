#include "replica_chain.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "measure.h"

static int matrix_is_finite(int n, const double *a)
{
    if (n <= 0 || a == NULL) {
        return 0;
    }
    for (int i = 0; i < n * n; i++) {
        if (!isfinite(a[i])) {
            return 0;
        }
    }
    return 1;
}

static int dqmc_state_is_finite(const Dqmc *D)
{
    if (D == NULL || !isfinite(D->sign)) {
        return 0;
    }
    return matrix_is_finite(D->n, D->Gu.g) &&
           matrix_is_finite(D->n, D->Gd.g);
}

static GreenRebuildMode green_rebuild_mode_from_params(const Params *p)
{
    if (strcmp(p->green_rebuild, "centered") == 0) {
        return GREEN_REBUILD_CENTERED;
    }
    return (strcmp(p->green_rebuild, "two_sided") == 0)
               ? GREEN_REBUILD_TWO_SIDED
               : GREEN_REBUILD_COMBINE;
}

static int dqmc_failure_reason(const Dqmc *D)
{
    if (D->Gu.work.failure_reason != LINALG_FAILURE_NONE) {
        return D->Gu.work.failure_reason;
    }
    if (D->Gd.work.failure_reason != LINALG_FAILURE_NONE) {
        return D->Gd.work.failure_reason;
    }
    return LINALG_FAILURE_NONE;
}

int replica_chain_state_is_finite(const ReplicaChain *c)
{
    return (c != NULL) && dqmc_state_is_finite(&c->D);
}

int replica_chain_failure_reason(const ReplicaChain *c)
{
    return dqmc_failure_reason(&c->D);
}

void replica_chain_free(ReplicaChain *c)
{
    if (c == NULL) {
        return;
    }
    free(c->szz_sample);
    free(c->sperp_sample);
    c->szz_sample = NULL;
    c->sperp_sample = NULL;
    szz_workspace_free(&c->szz_work);
    structure_factor_workspace_free(&c->sperp_work);
    if (c->have_dqmc) {
        dqmc_free(&c->D);
        c->have_dqmc = 0;
    }
    if (c->have_field) {
        field_free(&c->f);
        c->have_field = 0;
    }
    if (c->have_model) {
        model_free(&c->m);
        c->have_model = 0;
    }
}

int replica_chain_init(ReplicaChain *c, const Params *p, const Lattice *L,
                       int beta_index, int Ltr, double dtau, int replica_id,
                       unsigned long long seed, Profiler *prof)
{
    if (c == NULL) {
        return 1;
    }
    memset(c, 0, sizeof *c);
    if (p == NULL || L == NULL) {
        return 1;
    }
    c->p = p;
    c->lat = L;
    c->beta_index = beta_index;
    c->Ltr = Ltr;
    c->replica_id = replica_id;
    c->seed = seed;
    c->dtau = dtau;
    c->beta = (double)Ltr * dtau;
    c->T = (c->beta > 0.0) ? 1.0 / c->beta : 0.0;
    c->prof = prof;
    const double beta = c->beta;
    const double T = c->T;

    profiler_phase_set(prof, PROF_PHASE_SETUP);
    PROF_BEGIN(prof, t_model_init);
    model_init(&c->m, L, p->U, dtau, 1, 0.0);
    PROF_END(prof, PROF_MODEL_INIT, t_model_init);
    c->have_model = 1;

    rng_seed(&c->r, seed);

    PROF_BEGIN(prof, t_field_init);
    field_init(&c->f, L->n, Ltr, p->U, dtau, &c->r);
    PROF_END(prof, PROF_FIELD_INIT, t_field_init);
    c->have_field = 1;
    if (strcmp(p->field_init, "uniform") == 0) {
        field_set_uniform(&c->f, 1);
    }

    const DqmcSweepMode sweep_mode =
        (p->sweep_order[0] == 'a') ? DQMC_SWEEP_ALTERNATING
                                   : DQMC_SWEEP_FORWARD;
    PROF_BEGIN(prof, t_dqmc_init);
    const int init_rc = dqmc_init_modes(
        &c->D, &c->m, &c->f, &c->r, p->stab_interval, prof, sweep_mode,
        green_rebuild_mode_from_params(p));
    PROF_END(prof, PROF_DQMC_INIT, t_dqmc_init);
    c->have_dqmc = 1;
    if (init_rc != 0 || c->D.status != 0) {
        fprintf(stderr,
                "ERROR: dqmc_init failed (replica=%d seed=%llu U=%g "
                "dtau=%g Ltr=%d stab=%d sweep_order=%s failure_reason=%s)\n",
                replica_id, seed, p->U, dtau, Ltr, p->stab_interval,
                p->sweep_order,
                linalg_failure_reason_string(dqmc_failure_reason(&c->D)));
        replica_chain_free(c);
        return 1;
    }
    if (!dqmc_state_is_finite(&c->D)) {
        fprintf(stderr,
                "ERROR: dqmc_init produced non-finite state "
                "(beta_index=%d beta=%.17g T=%.17g replica=%d seed=%llu "
                "U=%g dtau=%g Ltr=%d stab=%d sweep_order=%s)\n",
                beta_index, beta, T, replica_id, seed, p->U, dtau, Ltr,
                p->stab_interval, p->sweep_order);
        replica_chain_free(c);
        return 1;
    }
    if (p->stab_drift_file[0] != '\0' &&
        dqmc_enable_stab_drift(&c->D, 1) != 0) {
        fprintf(stderr, "ERROR: failed to enable stabilization drift check\n");
        replica_chain_free(c);
        return 1;
    }
    if (p->udv_scale_file[0] != '\0' &&
        dqmc_enable_udv_scale_diag(&c->D, p->udv_scale_file, beta_index, Ltr,
                                   replica_id, seed, p->U, dtau) != 0) {
        fprintf(stderr, "ERROR: failed to enable UDV scale diagnostics\n");
        replica_chain_free(c);
        return 1;
    }
    if (p->udv_centered_file[0] != '\0' &&
        dqmc_enable_udv_centered_diag(&c->D, p->udv_centered_file, beta_index,
                                      Ltr, replica_id, seed) != 0) {
        fprintf(stderr, "ERROR: failed to enable centered UDV diagnostics\n");
        replica_chain_free(c);
        return 1;
    }

    profiler_phase_set(prof, PROF_PHASE_WARMUP);
    c->use_global = (strcmp(p->global_update, "site") == 0);
    if (c->use_global && strcmp(p->global_site_select, "polarized") == 0 &&
        dqmc_set_global_site_select(&c->D, 1, p->global_site_power) != 0) {
        fprintf(stderr,
                "ERROR: failed to enable polarized site selection "
                "(replica=%d seed=%llu alpha=%.17g)\n",
                replica_id, seed, p->global_site_power);
        replica_chain_free(c);
        return 1;
    }
    c->global_sweep = 0ULL; /* counts warmup + measurement */
#ifdef AFQMC_TEST_HOOKS
    const char *fail_env = getenv("AFQMC_TEST_GLOBAL_FAIL_AT"); /* test hook only */
    const char *fail_beta_env = getenv("AFQMC_TEST_GLOBAL_FAIL_BETA");
    const int fail_beta = (fail_beta_env != NULL) ? atoi(fail_beta_env) : 0;
    c->fail_at =
        (fail_env != NULL && fail_beta == beta_index)
            ? strtoull(fail_env, NULL, 10) : 0ULL;
#endif
    return 0;
}

int replica_chain_global_after_sweep(ReplicaChain *c)
{
    c->global_sweep++;
    if (c->use_global && c->D.status == 0 &&
        c->global_sweep % (unsigned long long)c->p->global_interval == 0ULL) {
#ifdef AFQMC_TEST_HOOKS
        /* the failure injection fires only in the measurement phase */
        const int inject_failure = c->measuring && c->fail_at != 0ULL &&
                                   c->global_sweep == c->fail_at;
        if (inject_failure) {
            c->D.Gu.work.failed = 1;
        }
        const int global_rc = dqmc_global_site_pass(&c->D);
        if (inject_failure) {
            fprintf(stderr,
                    "TEST_GLOBAL_FAIL beta_index=%d replica_id=%d sweep=%llu pass_rc=%d status=%d\n",
                    c->beta_index, c->replica_id, c->global_sweep, global_rc,
                    c->D.status);
        }
#else
        (void)dqmc_global_site_pass(&c->D);
#endif
    }
    return c->D.status;
}

int replica_chain_step(ReplicaChain *c)
{
    dqmc_sweep(&c->D);
    return replica_chain_global_after_sweep(c);
}

int replica_chain_measure_begin(ReplicaChain *c, const StructureFactorPlan *szz_plan,
                                const StructureFactorPlan *sperp_plan,
                                ReplicaResult *result)
{
    if (c == NULL || szz_plan == NULL || sperp_plan == NULL ||
        result == NULL) {
        return 1;
    }
    const Params *p = c->p;
    const int replica_id = c->replica_id;
    const unsigned long long seed = c->seed;
    c->szz_plan = szz_plan;
    c->sperp_plan = sperp_plan;

    const int use_site_diag =
        c->use_global && p->global_site_diag_file[0] != '\0';
    if (replica_result_alloc(result, p->nbin) != 0) {
        return 1;
    }
    if (use_site_diag &&
        (dqmc_enable_global_site_diag(&c->D, 1) != 0 ||
         replica_result_enable_site_diag(result) != 0)) {
        fprintf(stderr,
                "ERROR: failed to allocate site diagnostic storage "
                "(replica=%d seed=%llu)\n",
                replica_id, seed);
        replica_result_free(result);
        return 1;
    }
    if (szz_plan->enabled) {
        if (replica_result_enable_szz(result, szz_plan->nq) != 0 ||
            szz_workspace_init(&c->szz_work, szz_plan) != 0) {
            fprintf(stderr,
                    "ERROR: failed to allocate Szz replica storage "
                    "(replica=%d seed=%llu nq=%d)\n",
                    replica_id, seed, szz_plan->nq);
            replica_result_free(result);
            return 1;
        }
        c->szz_sample = malloc((size_t)szz_plan->nq * sizeof(double));
        if (c->szz_sample == NULL) {
            fprintf(stderr,
                    "ERROR: failed to allocate Szz sample "
                    "(replica=%d seed=%llu nq=%d)\n",
                    replica_id, seed, szz_plan->nq);
            replica_result_free(result);
            return 1;
        }
    }
    if (sperp_plan->enabled) {
        if (replica_result_enable_sperp(result, sperp_plan->nq) != 0 ||
            structure_factor_workspace_init(&c->sperp_work, sperp_plan) != 0) {
            fprintf(stderr,
                    "ERROR: failed to allocate Sperp replica storage "
                    "(replica=%d seed=%llu nq=%d)\n",
                    replica_id, seed, sperp_plan->nq);
            replica_result_free(result);
            return 1;
        }
        c->sperp_sample = malloc((size_t)sperp_plan->nq * sizeof(double));
        if (c->sperp_sample == NULL) {
            fprintf(stderr,
                    "ERROR: failed to allocate Sperp sample "
                    "(replica=%d seed=%llu nq=%d)\n",
                    replica_id, seed, sperp_plan->nq);
            replica_result_free(result);
            return 1;
        }
    }
    result->replica_id = replica_id;
    result->seed = seed;
    result->status = 1;
    c->measuring = 1;
    return 0;
}

int replica_chain_measure(ReplicaChain *c, ReplicaResult *result, int bi, int k,
                          double mu, unsigned long long attempts0,
                          unsigned long long accepted0,
                          unsigned long long gatt0, unsigned long long gacc0)
{
    const Params *p = c->p;
    const Lattice *L = c->lat;
    Profiler *prof = c->prof;
    Dqmc *D = &c->D;
    const StructureFactorPlan *szz_plan = c->szz_plan;
    const StructureFactorPlan *sperp_plan = c->sperp_plan;
    const int beta_index = c->beta_index;
    const int Ltr = c->Ltr;
    const int replica_id = c->replica_id;
    const unsigned long long seed = c->seed;
    const double beta = c->beta;
    const double T = c->T;

    if (D->status != 0) {
        fprintf(stderr,
                "ERROR: dqmc measurement numerical breakdown "
                "(beta_index=%d beta=%.17g T=%.17g replica=%d "
                "seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                "sweep_count=%llu bin=%d meas=%d status=%d "
                "failure_reason=%s)\n",
                beta_index, beta, T, replica_id, seed, p->U, c->dtau,
                Ltr, p->stab_interval, D->sweep_count, bi, k,
                D->status,
                linalg_failure_reason_string(dqmc_failure_reason(D)));
        return 1;
    }
    if (!dqmc_state_is_finite(D)) {
        fprintf(stderr,
                "ERROR: non-finite Green/sign before measurement "
                "(beta_index=%d beta=%.17g T=%.17g replica=%d "
                "seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                "sweep_count=%llu bin=%d meas=%d sign=%.17g)\n",
                beta_index, beta, T, replica_id, seed, p->U, c->dtau,
                Ltr, p->stab_interval, D->sweep_count, bi, k, D->sign);
        return 1;
    }
    PROF_BEGIN(prof, t_measure_sample);
    MeasSample s = measure_sample(L->n, L->t, p->U, D->Gu.g, D->Gd.g);
    PROF_END(prof, PROF_MEASURE_SAMPLE, t_measure_sample);
    if (!measure_sample_is_finite(&s)) {
        fprintf(stderr,
                "ERROR: non-finite measurement sample "
                "(beta_index=%d beta=%.17g T=%.17g replica=%d "
                "seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                "sweep_count=%llu bin=%d meas=%d E=%.17g "
                "ekin=%.17g eint=%.17g ntot=%.17g doublon=%.17g)\n",
                beta_index, beta, T, replica_id, seed, p->U, c->dtau,
                Ltr, p->stab_interval, D->sweep_count, bi, k, s.E,
                s.ekin, s.eint, s.ntot, s.doublon);
        return 1;
    }
    if (szz_plan->enabled && sperp_plan->enabled) {
        PROF_BEGIN(prof, t_measure_spin);
        const int spin_rc = measure_szz_sperp_sample(
            szz_plan, &c->szz_work, c->szz_sample, sperp_plan, &c->sperp_work,
            c->sperp_sample, D->Gu.g, D->Gd.g);
        PROF_END(prof, PROF_MEASURE_SPIN, t_measure_spin);
        if (spin_rc != 0) {
            fprintf(stderr,
                    "ERROR: invalid joint Szz/Sperp sample "
                    "(beta_index=%d beta=%.17g replica=%d seed=%llu "
                    "bin=%d meas=%d szz_nq=%d sperp_nq=%d)\n",
                    beta_index, beta, replica_id, seed, bi, k,
                    szz_plan->nq, sperp_plan->nq);
            return 1;
        }
    } else if (szz_plan->enabled) {
        PROF_BEGIN(prof, t_measure_szz);
        const int szz_rc = measure_szz_sample(
            szz_plan, &c->szz_work, D->Gu.g, D->Gd.g, c->szz_sample);
        PROF_END(prof, PROF_MEASURE_SZZ, t_measure_szz);
        if (szz_rc != 0) {
            fprintf(stderr,
                    "ERROR: invalid Szz sample "
                    "(beta_index=%d beta=%.17g replica=%d seed=%llu "
                    "bin=%d meas=%d nq=%d)\n",
                    beta_index, beta, replica_id, seed, bi, k,
                    szz_plan->nq);
            return 1;
        }
    } else if (sperp_plan->enabled) {
        PROF_BEGIN(prof, t_measure_sperp);
        const int sperp_rc = measure_sperp_sample(
            sperp_plan, &c->sperp_work, D->Gu.g, D->Gd.g, c->sperp_sample);
        PROF_END(prof, PROF_MEASURE_SPERP, t_measure_sperp);
        if (sperp_rc != 0) {
            fprintf(stderr,
                    "ERROR: invalid Sperp sample "
                    "(beta_index=%d beta=%.17g replica=%d seed=%llu "
                    "bin=%d meas=%d nq=%d)\n",
                    beta_index, beta, replica_id, seed, bi, k,
                    sperp_plan->nq);
            return 1;
        }
    }
    if (replica_result_add_sample(
            result, bi, &s, mu, p->U, L->n, D->sign,
            szz_plan->enabled ? c->szz_sample : NULL,
            szz_plan->enabled ? szz_plan->nq : 0,
            sperp_plan->enabled ? c->sperp_sample : NULL,
            sperp_plan->enabled ? sperp_plan->nq : 0) != 0) {
        fprintf(stderr,
                "ERROR: rejected non-finite bin sample "
                "(beta_index=%d beta=%.17g T=%.17g replica=%d "
                "seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                "sweep_count=%llu bin=%d meas=%d sign=%.17g)\n",
                beta_index, beta, T, replica_id, seed, p->U, c->dtau,
                Ltr, p->stab_interval, D->sweep_count, bi, k, D->sign);
        return 1;
    }
    replica_bin_add_global(&result->bins[bi],
                           D->global_accepted - gacc0,
                           D->global_attempts - gatt0);
    replica_bin_add_acceptance(&result->bins[bi],
                               D->accept_accepted - accepted0,
                               D->accept_attempts - attempts0);
    return 0;
}
