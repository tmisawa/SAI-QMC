#include "replica_run.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dqmc.h"
#include "field.h"
#include "measure.h"
#include "model.h"
#include "rng.h"
#include "structure_factor.h"

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
    const double beta = (double)Ltr * p->dtau;
    const double T = (beta > 0.0) ? 1.0 / beta : 0.0;

    profiler_phase_set(prof, PROF_PHASE_SETUP);
    Model m;
    PROF_BEGIN(prof, t_model_init);
    model_init(&m, L, p->U, p->dtau, 1, 0.0);
    PROF_END(prof, PROF_MODEL_INIT, t_model_init);

    Rng r;
    rng_seed(&r, seed);

    Field f;
    PROF_BEGIN(prof, t_field_init);
    field_init(&f, L->n, Ltr, p->U, p->dtau, &r);
    PROF_END(prof, PROF_FIELD_INIT, t_field_init);

    const DqmcSweepMode sweep_mode =
        (p->sweep_order[0] == 'a') ? DQMC_SWEEP_ALTERNATING
                                   : DQMC_SWEEP_FORWARD;
    Dqmc D;
    PROF_BEGIN(prof, t_dqmc_init);
    const int init_rc = dqmc_init_modes(
        &D, &m, &f, &r, p->stab_interval, prof, sweep_mode,
        green_rebuild_mode_from_params(p));
    PROF_END(prof, PROF_DQMC_INIT, t_dqmc_init);
    if (init_rc != 0 || D.status != 0) {
        fprintf(stderr,
                "ERROR: dqmc_init failed (replica=%d seed=%llu U=%g "
                "dtau=%g Ltr=%d stab=%d sweep_order=%s failure_reason=%s)\n",
                replica_id, seed, p->U, p->dtau, Ltr, p->stab_interval,
                p->sweep_order,
                linalg_failure_reason_string(dqmc_failure_reason(&D)));
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_set_current(old_prof);
        return 1;
    }
    if (!dqmc_state_is_finite(&D)) {
        fprintf(stderr,
                "ERROR: dqmc_init produced non-finite state "
                "(beta_index=%d beta=%.17g T=%.17g replica=%d seed=%llu "
                "U=%g dtau=%g Ltr=%d stab=%d sweep_order=%s)\n",
                beta_index, beta, T, replica_id, seed, p->U, p->dtau, Ltr,
                p->stab_interval, p->sweep_order);
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_set_current(old_prof);
        return 1;
    }
    if (p->stab_drift_file[0] != '\0' && dqmc_enable_stab_drift(&D, 1) != 0) {
        fprintf(stderr, "ERROR: failed to enable stabilization drift check\n");
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_set_current(old_prof);
        return 1;
    }
    if (p->udv_scale_file[0] != '\0' &&
        dqmc_enable_udv_scale_diag(&D, p->udv_scale_file, beta_index, Ltr,
                                   replica_id, seed, p->U, p->dtau) != 0) {
        fprintf(stderr, "ERROR: failed to enable UDV scale diagnostics\n");
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_set_current(old_prof);
        return 1;
    }
    if (p->udv_centered_file[0] != '\0' &&
        dqmc_enable_udv_centered_diag(&D, p->udv_centered_file, beta_index,
                                      Ltr, replica_id, seed) != 0) {
        fprintf(stderr, "ERROR: failed to enable centered UDV diagnostics\n");
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_set_current(old_prof);
        return 1;
    }

    profiler_phase_set(prof, PROF_PHASE_WARMUP);
    const int use_global = (strcmp(p->global_update, "site") == 0);
    unsigned long long global_sweep = 0ULL; /* counts warmup + measurement */
    const char *fail_env = getenv("AFQMC_TEST_GLOBAL_FAIL_AT"); /* test hook only */
    const char *fail_beta_env = getenv("AFQMC_TEST_GLOBAL_FAIL_BETA");
    const int fail_beta = (fail_beta_env != NULL) ? atoi(fail_beta_env) : 0;
    const unsigned long long fail_at =
        (fail_env != NULL && fail_beta == beta_index)
            ? strtoull(fail_env, NULL, 10) : 0ULL;
    for (int w = 0; w < p->nwarm; w++) {
        dqmc_sweep(&D);
        global_sweep++;
        if (use_global && D.status == 0 &&
            global_sweep % (unsigned long long)p->global_interval == 0ULL) {
            (void)dqmc_global_site_pass(&D);
        }
    }

    if (D.status != 0 || !dqmc_state_is_finite(&D)) {
        fprintf(stderr,
                "ERROR: dqmc warmup numerical breakdown "
                "(beta_index=%d beta=%.17g T=%.17g replica=%d seed=%llu "
                "U=%g dtau=%g Ltr=%d stab=%d sweep_count=%llu status=%d "
                "failure_reason=%s)\n",
                beta_index, beta, T, replica_id, seed, p->U, p->dtau, Ltr,
                p->stab_interval, D.sweep_count, D.status,
                linalg_failure_reason_string(dqmc_failure_reason(&D)));
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_set_current(old_prof);
        return 1;
    }

    if (replica_result_alloc(result, p->nbin) != 0) {
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_set_current(old_prof);
        return 1;
    }
    StructureFactorWorkspace szz_work = {0};
    StructureFactorWorkspace sperp_work = {0};
    double *szz_sample = NULL;
    double *sperp_sample = NULL;
    if (szz_plan->enabled) {
        if (replica_result_enable_szz(result, szz_plan->nq) != 0 ||
            szz_workspace_init(&szz_work, szz_plan) != 0) {
            fprintf(stderr,
                    "ERROR: failed to allocate Szz replica storage "
                    "(replica=%d seed=%llu nq=%d)\n",
                    replica_id, seed, szz_plan->nq);
            szz_workspace_free(&szz_work);
            replica_result_free(result);
            dqmc_free(&D);
            field_free(&f);
            model_free(&m);
            profiler_set_current(old_prof);
            return 1;
        }
        szz_sample = malloc((size_t)szz_plan->nq * sizeof(double));
        if (szz_sample == NULL) {
            fprintf(stderr,
                    "ERROR: failed to allocate Szz sample "
                    "(replica=%d seed=%llu nq=%d)\n",
                    replica_id, seed, szz_plan->nq);
            szz_workspace_free(&szz_work);
            replica_result_free(result);
            dqmc_free(&D);
            field_free(&f);
            model_free(&m);
            profiler_set_current(old_prof);
            return 1;
        }
    }
    if (sperp_plan->enabled) {
        if (replica_result_enable_sperp(result, sperp_plan->nq) != 0 ||
            structure_factor_workspace_init(&sperp_work, sperp_plan) != 0) {
            fprintf(stderr,
                    "ERROR: failed to allocate Sperp replica storage "
                    "(replica=%d seed=%llu nq=%d)\n",
                    replica_id, seed, sperp_plan->nq);
            free(szz_sample);
            szz_workspace_free(&szz_work);
            structure_factor_workspace_free(&sperp_work);
            replica_result_free(result);
            dqmc_free(&D);
            field_free(&f);
            model_free(&m);
            profiler_set_current(old_prof);
            return 1;
        }
        sperp_sample = malloc((size_t)sperp_plan->nq * sizeof(double));
        if (sperp_sample == NULL) {
            fprintf(stderr,
                    "ERROR: failed to allocate Sperp sample "
                    "(replica=%d seed=%llu nq=%d)\n",
                    replica_id, seed, sperp_plan->nq);
            free(szz_sample);
            szz_workspace_free(&szz_work);
            structure_factor_workspace_free(&sperp_work);
            replica_result_free(result);
            dqmc_free(&D);
            field_free(&f);
            model_free(&m);
            profiler_set_current(old_prof);
            return 1;
        }
    }
    result->replica_id = replica_id;
    result->seed = seed;
    result->status = 1;

    const int per = p->nmeas / p->nbin;
    profiler_phase_set(prof, PROF_PHASE_MEASUREMENT);
    for (int bi = 0; bi < p->nbin; bi++) {
        for (int k = 0; k < per; k++) {
            const unsigned long long attempts0 = D.accept_attempts;
            const unsigned long long accepted0 = D.accept_accepted;
            dqmc_sweep(&D);
            const unsigned long long gatt0 = D.global_attempts;
            const unsigned long long gacc0 = D.global_accepted;
            global_sweep++;
            if (use_global && D.status == 0 &&
                global_sweep % (unsigned long long)p->global_interval == 0ULL) {
                const int inject_failure = fail_at != 0ULL && global_sweep == fail_at;
                if (inject_failure) {
                    D.Gu.work.failed = 1;
                }
                const int global_rc = dqmc_global_site_pass(&D);
                if (inject_failure) {
                    fprintf(stderr,
                            "TEST_GLOBAL_FAIL beta_index=%d replica_id=%d sweep=%llu pass_rc=%d status=%d\n",
                            beta_index, replica_id, global_sweep, global_rc, D.status);
                }
            }
            if (D.status != 0) {
                fprintf(stderr,
                        "ERROR: dqmc measurement numerical breakdown "
                        "(beta_index=%d beta=%.17g T=%.17g replica=%d "
                        "seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                        "sweep_count=%llu bin=%d meas=%d status=%d "
                        "failure_reason=%s)\n",
                        beta_index, beta, T, replica_id, seed, p->U, p->dtau,
                        Ltr, p->stab_interval, D.sweep_count, bi, k,
                        D.status,
                        linalg_failure_reason_string(dqmc_failure_reason(&D)));
                free(szz_sample);
                free(sperp_sample);
                szz_workspace_free(&szz_work);
                structure_factor_workspace_free(&sperp_work);
                replica_result_free(result);
                dqmc_free(&D);
                field_free(&f);
                model_free(&m);
                profiler_set_current(old_prof);
                return 1;
            }
            if (!dqmc_state_is_finite(&D)) {
                fprintf(stderr,
                        "ERROR: non-finite Green/sign before measurement "
                        "(beta_index=%d beta=%.17g T=%.17g replica=%d "
                        "seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                        "sweep_count=%llu bin=%d meas=%d sign=%.17g)\n",
                        beta_index, beta, T, replica_id, seed, p->U, p->dtau,
                        Ltr, p->stab_interval, D.sweep_count, bi, k, D.sign);
                free(szz_sample);
                free(sperp_sample);
                szz_workspace_free(&szz_work);
                structure_factor_workspace_free(&sperp_work);
                replica_result_free(result);
                dqmc_free(&D);
                field_free(&f);
                model_free(&m);
                profiler_set_current(old_prof);
                return 1;
            }
            PROF_BEGIN(prof, t_measure_sample);
            MeasSample s = measure_sample(L->n, L->t, p->U, D.Gu.g, D.Gd.g);
            PROF_END(prof, PROF_MEASURE_SAMPLE, t_measure_sample);
            if (!measure_sample_is_finite(&s)) {
                fprintf(stderr,
                        "ERROR: non-finite measurement sample "
                        "(beta_index=%d beta=%.17g T=%.17g replica=%d "
                        "seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                        "sweep_count=%llu bin=%d meas=%d E=%.17g "
                        "ekin=%.17g eint=%.17g ntot=%.17g doublon=%.17g)\n",
                        beta_index, beta, T, replica_id, seed, p->U, p->dtau,
                        Ltr, p->stab_interval, D.sweep_count, bi, k, s.E,
                        s.ekin, s.eint, s.ntot, s.doublon);
                free(szz_sample);
                free(sperp_sample);
                szz_workspace_free(&szz_work);
                structure_factor_workspace_free(&sperp_work);
                replica_result_free(result);
                dqmc_free(&D);
                field_free(&f);
                model_free(&m);
                profiler_set_current(old_prof);
                return 1;
            }
            if (szz_plan->enabled && sperp_plan->enabled) {
                PROF_BEGIN(prof, t_measure_spin);
                const int spin_rc = measure_szz_sperp_sample(
                    szz_plan, &szz_work, szz_sample, sperp_plan, &sperp_work,
                    sperp_sample, D.Gu.g, D.Gd.g);
                PROF_END(prof, PROF_MEASURE_SPIN, t_measure_spin);
                if (spin_rc != 0) {
                    fprintf(stderr,
                            "ERROR: invalid joint Szz/Sperp sample "
                            "(beta_index=%d beta=%.17g replica=%d seed=%llu "
                            "bin=%d meas=%d szz_nq=%d sperp_nq=%d)\n",
                            beta_index, beta, replica_id, seed, bi, k,
                            szz_plan->nq, sperp_plan->nq);
                    free(szz_sample);
                    free(sperp_sample);
                    szz_workspace_free(&szz_work);
                    structure_factor_workspace_free(&sperp_work);
                    replica_result_free(result);
                    dqmc_free(&D);
                    field_free(&f);
                    model_free(&m);
                    profiler_set_current(old_prof);
                    return 1;
                }
            } else if (szz_plan->enabled) {
                PROF_BEGIN(prof, t_measure_szz);
                const int szz_rc = measure_szz_sample(
                    szz_plan, &szz_work, D.Gu.g, D.Gd.g, szz_sample);
                PROF_END(prof, PROF_MEASURE_SZZ, t_measure_szz);
                if (szz_rc != 0) {
                    fprintf(stderr,
                            "ERROR: invalid Szz sample "
                            "(beta_index=%d beta=%.17g replica=%d seed=%llu "
                            "bin=%d meas=%d nq=%d)\n",
                            beta_index, beta, replica_id, seed, bi, k,
                            szz_plan->nq);
                    free(szz_sample);
                    free(sperp_sample);
                    szz_workspace_free(&szz_work);
                    structure_factor_workspace_free(&sperp_work);
                    replica_result_free(result);
                    dqmc_free(&D);
                    field_free(&f);
                    model_free(&m);
                    profiler_set_current(old_prof);
                    return 1;
                }
            } else if (sperp_plan->enabled) {
                PROF_BEGIN(prof, t_measure_sperp);
                const int sperp_rc = measure_sperp_sample(
                    sperp_plan, &sperp_work, D.Gu.g, D.Gd.g, sperp_sample);
                PROF_END(prof, PROF_MEASURE_SPERP, t_measure_sperp);
                if (sperp_rc != 0) {
                    fprintf(stderr,
                            "ERROR: invalid Sperp sample "
                            "(beta_index=%d beta=%.17g replica=%d seed=%llu "
                            "bin=%d meas=%d nq=%d)\n",
                            beta_index, beta, replica_id, seed, bi, k,
                            sperp_plan->nq);
                    free(szz_sample);
                    free(sperp_sample);
                    szz_workspace_free(&szz_work);
                    structure_factor_workspace_free(&sperp_work);
                    replica_result_free(result);
                    dqmc_free(&D);
                    field_free(&f);
                    model_free(&m);
                    profiler_set_current(old_prof);
                    return 1;
                }
            }
            if (replica_result_add_sample(
                    result, bi, &s, mu, p->U, L->n, D.sign,
                    szz_plan->enabled ? szz_sample : NULL,
                    szz_plan->enabled ? szz_plan->nq : 0,
                    sperp_plan->enabled ? sperp_sample : NULL,
                    sperp_plan->enabled ? sperp_plan->nq : 0) != 0) {
                fprintf(stderr,
                        "ERROR: rejected non-finite bin sample "
                        "(beta_index=%d beta=%.17g T=%.17g replica=%d "
                        "seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                        "sweep_count=%llu bin=%d meas=%d sign=%.17g)\n",
                        beta_index, beta, T, replica_id, seed, p->U, p->dtau,
                        Ltr, p->stab_interval, D.sweep_count, bi, k, D.sign);
                free(szz_sample);
                free(sperp_sample);
                szz_workspace_free(&szz_work);
                structure_factor_workspace_free(&sperp_work);
                replica_result_free(result);
                dqmc_free(&D);
                field_free(&f);
                model_free(&m);
                profiler_set_current(old_prof);
                return 1;
            }
            replica_bin_add_global(&result->bins[bi],
                                   D.global_accepted - gacc0,
                                   D.global_attempts - gatt0);
            replica_bin_add_acceptance(&result->bins[bi],
                                       D.accept_accepted - accepted0,
                                       D.accept_attempts - attempts0);
        }
    }

    result->status = 0;
    if (append_stab_drift_row(p, beta_index, Ltr, replica_id, seed, &D) != 0) {
        free(szz_sample);
        free(sperp_sample);
        szz_workspace_free(&szz_work);
        structure_factor_workspace_free(&sperp_work);
        replica_result_free(result);
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_set_current(old_prof);
        return 1;
    }
    free(szz_sample);
    free(sperp_sample);
    szz_workspace_free(&szz_work);
    structure_factor_workspace_free(&sperp_work);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    profiler_set_current(old_prof);
    return 0;
}
