#include "dqmc.h"
#include "field.h"
#include "io.h"
#include "lattice.h"
#include "measure.h"
#include "model.h"
#include "profiler.h"
#include "replica.h"
#include "replica_mpi.h"
#include "replica_run.h"
#include "scalar_output.h"
#include "replica_bin_out.h"
#include "structure_factor.h"

#include <limits.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AFQMC_USE_OPENMP
#include <omp.h>
#endif

#ifdef AFQMC_USE_MPI
#include <mpi.h>
#endif

typedef struct {
    int enabled;
    int rank;
    int nranks;
} MpiEnv;

typedef struct {
    const char *name;
    const char *path;
} ActiveOutputPath;

/* No files are opened here. Returns the conflicting output name, or NULL. */
static const char *replica_bin_conflict(const Params *p)
{
    if (p->replica_bin_file[0] == '\0') {
        return NULL;
    }
    const int write_replica_log = strcmp(p->replica_log, "none") != 0 &&
                                  (p->replica_log[0] != '\0' || p->nrep > 1);
    const ActiveOutputPath outputs[] = {
        {"hopping_used.txt", "hopping_used.txt"},
        {"output_file", strcmp(p->output_file, "none") != 0 ? p->output_file : NULL},
        {"stab_drift_file", p->stab_drift_file},
        {"udv_scale_file", p->udv_scale_file},
        {"udv_centered_file", p->udv_centered_file},
        {"replica_log", write_replica_log
                            ? (p->replica_log[0] != '\0' ? p->replica_log : "replicas.dat")
                            : NULL},
        {"profile_file", p->profile
                             ? (p->profile_file[0] != '\0' ? p->profile_file : "profile.dat")
                             : NULL},
        {"szz_file", strcmp(p->szz_q, "none") != 0 ? p->szz_file : NULL},
        {"sperp_file", strcmp(p->sperp_q, "none") != 0 ? p->sperp_file : NULL},
        {"spin_consistency_file", strcmp(p->spin_consistency_file, "none") != 0
                                      ? p->spin_consistency_file : NULL}
    };
    for (size_t k = 0; k < sizeof outputs / sizeof outputs[0]; k++) {
        if (outputs[k].path != NULL && outputs[k].path[0] != '\0' &&
            strcmp(outputs[k].path, p->replica_bin_file) == 0) {
            return outputs[k].name;
        }
    }
    return NULL;
}


static int find_output_path_collision(const ActiveOutputPath *outputs,
                                      int noutputs, int *first, int *second)
{
    for (int i = 0; i < noutputs; i++) {
        for (int j = i + 1; j < noutputs; j++) {
            if (strcmp(outputs[i].path, outputs[j].path) == 0) {
                if (first != NULL) {
                    *first = i;
                }
                if (second != NULL) {
                    *second = j;
                }
                return 1;
            }
        }
    }
    return 0;
}

/* Check the new scalar destination before any output can overwrite an input
 * or another enabled output. Other legacy output pairs keep their own checks. */
static int validate_scalar_output_path(const Params *p, const char *input_path)
{
    if (strcmp(p->output_file, "none") == 0) {
        return 0;
    }
    const ActiveOutputPath occupied[] = {
        {"input", input_path},
        {"latfile", strcmp(p->lattice, "file") == 0 ? p->latfile : NULL},
        {"hopping matrix", "hopping_used.txt"},
        {"profile_file", p->profile ?
            (p->profile_file[0] ? p->profile_file : "profile.dat") : NULL},
        {"replica_log", strcmp(p->replica_log, "none") != 0 &&
            (p->replica_log[0] || p->nrep > 1) ?
            (p->replica_log[0] ? p->replica_log : "replicas.dat") : NULL},
        {"szz_file", strcmp(p->szz_q, "none") != 0 ? p->szz_file : NULL},
        {"sperp_file", strcmp(p->sperp_q, "none") != 0 ? p->sperp_file : NULL},
        {"spin_consistency_file", strcmp(p->spin_consistency_file, "none") != 0
            ? p->spin_consistency_file : NULL},
        {"stab_drift_file", p->stab_drift_file[0] ? p->stab_drift_file : NULL},
        {"udv_scale_file", p->udv_scale_file[0] ? p->udv_scale_file : NULL},
        {"udv_centered_file", p->udv_centered_file[0] ? p->udv_centered_file : NULL},
        {"replica_bin_file", p->replica_bin_file[0] ? p->replica_bin_file : NULL}
    };
    for (size_t i = 0; i < sizeof occupied / sizeof occupied[0]; i++) {
        if (occupied[i].path != NULL && occupied[i].path[0] != '\0' &&
            output_paths_equal(p->output_file, occupied[i].path)) {
            fprintf(stderr, "ERROR: output_file %s collides with %s %s\n",
                    p->output_file, occupied[i].name, occupied[i].path);
            return 1;
        }
    }
    return 0;
}

static int mpi_is_root(const MpiEnv *env)
{
    return env == NULL || !env->enabled || env->rank == 0;
}

static int mpi_any_failed(const MpiEnv *env, int local_failed)
{
#ifdef AFQMC_USE_MPI
    if (env != NULL && env->enabled) {
        int global_failed = 0;
        MPI_Allreduce(&local_failed, &global_failed, 1, MPI_INT, MPI_MAX,
                      MPI_COMM_WORLD);
        return global_failed;
    }
#else
    (void)env;
#endif
    return local_failed;
}

static void mpi_finalize_if_enabled(const MpiEnv *env)
{
#ifdef AFQMC_USE_MPI
    if (env != NULL && env->enabled) {
        MPI_Finalize();
    }
#else
    (void)env;
#endif
}

static int parallel_uses_mpi(const char *parallel)
{
    return strcmp(parallel, "mpi") == 0 || strcmp(parallel, "hybrid") == 0;
}

#ifndef AFQMC_USE_OPENMP
static int parallel_uses_openmp(const char *parallel)
{
    return strcmp(parallel, "omp") == 0 || strcmp(parallel, "hybrid") == 0;
}
#endif

static void free_replica_arrays(unsigned long long *seeds,
                                ReplicaResult *results,
                                Profiler *replica_profs,
                                int *replica_failed, int nrep)
{
    if (results != NULL) {
        for (int q = 0; q < nrep; q++) {
            replica_result_free(&results[q]);
        }
    }
    free(seeds);
    free(results);
    free(replica_profs);
    free(replica_failed);
}

static int alloc_replica_arrays(int nrep, unsigned long long **seeds,
                                ReplicaResult **results,
                                Profiler **replica_profs,
                                int **replica_failed)
{
    if (results == NULL || replica_profs == NULL || replica_failed == NULL) {
        return 1;
    }
    if (seeds != NULL) {
        *seeds = NULL;
    }
    *results = NULL;
    *replica_profs = NULL;
    *replica_failed = NULL;
    if (nrep < 0) {
        return 1;
    }
    if (nrep == 0) {
        return 0;
    }

    if (seeds != NULL) {
        *seeds = calloc((size_t)nrep, sizeof(unsigned long long));
    }
    *results = calloc((size_t)nrep, sizeof(ReplicaResult));
    *replica_profs = calloc((size_t)nrep, sizeof(Profiler));
    *replica_failed = calloc((size_t)nrep, sizeof(int));
    if ((seeds != NULL && *seeds == NULL) || *results == NULL ||
        *replica_profs == NULL || *replica_failed == NULL) {
        free_replica_arrays(seeds != NULL ? *seeds : NULL, *results,
                            *replica_profs, *replica_failed, nrep);
        if (seeds != NULL) {
            *seeds = NULL;
        }
        *results = NULL;
        *replica_profs = NULL;
        *replica_failed = NULL;
        return 1;
    }
    return 0;
}

static void free_measurement_arrays(double *Ehub, double *Egc, double *Eph,
                                    double *Nbin, double *Dbin, double *Sbin,
                                    double *Accbin)
{
    free(Ehub);
    free(Egc);
    free(Eph);
    free(Nbin);
    free(Dbin);
    free(Sbin);
    free(Accbin);
}

static int array_is_finite(const double *x, int n)
{
    if (x == NULL || n <= 0) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (!isfinite(x[i])) {
            return 0;
        }
    }
    return 1;
}

static int jackknife_and_print(FILE *scalar_fp, Profiler *prof,
                               int total_bins, double T,
                               double *Ehub, double *Egc, double *Eph,
                               double *Nbin, double *Dbin, double *Sbin,
                               double *Accbin, int global_enabled,
                               double global_acceptance,
                               unsigned long long global_attempts)
{
    if (!array_is_finite(Ehub, total_bins) ||
        !array_is_finite(Egc, total_bins) ||
        !array_is_finite(Eph, total_bins) ||
        !array_is_finite(Nbin, total_bins) ||
        !array_is_finite(Dbin, total_bins) ||
        !array_is_finite(Sbin, total_bins) ||
        !array_is_finite(Accbin, total_bins)) {
        fprintf(stderr,
                "ERROR: non-finite or empty jackknife input at T=%.17g "
                "total_bins=%d\n",
                T, total_bins);
        return 1;
    }

    profiler_phase_set(prof, PROF_PHASE_FINALIZE);
    PROF_BEGIN(prof, t_jackknife);
    double Eh, Ehe, Eg, Ege, Ep, Epe, Nm, Ne, Dm, De, Sm, Se, Am, Ae;
    jackknife(Ehub, total_bins, &Eh, &Ehe);
    jackknife(Egc, total_bins, &Eg, &Ege);
    jackknife(Eph, total_bins, &Ep, &Epe);
    jackknife(Nbin, total_bins, &Nm, &Ne);
    jackknife(Dbin, total_bins, &Dm, &De);
    jackknife(Sbin, total_bins, &Sm, &Se);
    jackknife(Accbin, total_bins, &Am, &Ae);
    PROF_END(prof, PROF_JACKKNIFE, t_jackknife);
    if (!isfinite(Eh) || !isfinite(Ehe) || !isfinite(Eg) ||
        !isfinite(Ege) || !isfinite(Ep) || !isfinite(Epe) ||
        !isfinite(Nm) || !isfinite(Ne) || !isfinite(Dm) ||
        !isfinite(De) || !isfinite(Sm) || !isfinite(Se) ||
        !isfinite(Am) || !isfinite(Ae)) {
        fprintf(stderr, "ERROR: non-finite jackknife output at T=%.17g\n", T);
        return 1;
    }
    int failed = scalar_output_printf(scalar_fp,
           "%.6g %.8g %.3g %.8g %.3g %.8g %.3g %.6g %.2g %.8g %.2g %.6g %.8g %.3g",
           T, Eh, Ehe, Eg, Ege, Ep, Epe, Nm, Ne, Dm, De, Sm, Am, Ae);
    if (!global_enabled) {
        failed |= scalar_output_printf(scalar_fp, "\n");
    } else if (global_attempts == 0ULL) {
        failed |= scalar_output_printf(scalar_fp, " nan 0\n");
    } else {
        failed |= scalar_output_printf(scalar_fp, " %.8g %llu\n", global_acceptance, global_attempts);
    }
    failed |= scalar_output_flush(scalar_fp);
    return failed;
}

static int write_q_beta(FILE *fp, Profiler *prof,
                        const StructureFactorPlan *plan,
                        double beta_requested, double beta, double T,
                        const double *q_major_bins, int total_bins)
{
    if (fp == NULL || plan == NULL || !plan->enabled ||
        q_major_bins == NULL || total_bins <= 0) {
        return 1;
    }
    profiler_phase_set(prof, PROF_PHASE_FINALIZE);
    for (int q = 0; q < plan->nq; q++) {
        const double *bins = &q_major_bins[q * total_bins];
        if (!array_is_finite(bins, total_bins)) {
            return 1;
        }
        double mean = 0.0;
        double err = 0.0;
        PROF_BEGIN(prof, t_q_jackknife);
        jackknife(bins, total_bins, &mean, &err);
        PROF_END(prof, PROF_JACKKNIFE, t_q_jackknife);
        if (!isfinite(mean) || !isfinite(err)) {
            return 1;
        }
        const int folded_mx =
            plan->Lx > 1 && plan->mx[q] > plan->Lx / 2
                ? plan->mx[q] - plan->Lx
                : plan->mx[q];
        const int folded_my =
            plan->Ly > 1 && plan->my[q] > plan->Ly / 2
                ? plan->my[q] - plan->Ly
                : plan->my[q];
        const double qx = plan->Lx > 1
                              ? 2.0 * (double)plan->mx[q] / plan->Lx
                              : 0.0;
        const double qy = plan->Ly > 1
                              ? 2.0 * (double)plan->my[q] / plan->Ly
                              : 0.0;
        const double qx_folded =
            plan->Lx > 1 ? 2.0 * (double)folded_mx / plan->Lx : 0.0;
        const double qy_folded =
            plan->Ly > 1 ? 2.0 * (double)folded_my / plan->Ly : 0.0;
        fprintf(fp,
                "%.17g %.17g %.17g %d %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n",
                beta_requested, beta, T, q, plan->mx[q], plan->my[q], qx,
                qy, qx_folded, qy_folded, mean, err);
    }
    if (fflush(fp) != 0 || ferror(fp)) {
        return 1;
    }
    return 0;
}

static int write_spin_consistency_beta(
    FILE *fp, Profiler *prof, const StructureFactorPlan *plan,
    double beta_requested, double beta, double T, const double *szz_bins,
    const double *sperp_bins, int total_bins)
{
    if (fp == NULL || plan == NULL || !plan->enabled || szz_bins == NULL ||
        sperp_bins == NULL || total_bins <= 0) {
        return 1;
    }
    double *delta_bins = malloc((size_t)total_bins * sizeof(double));
    if (delta_bins == NULL) {
        return 1;
    }
    profiler_phase_set(prof, PROF_PHASE_FINALIZE);
    for (int q = 0; q < plan->nq; q++) {
        for (int bi = 0; bi < total_bins; bi++) {
            delta_bins[bi] =
                0.5 * sperp_bins[q * total_bins + bi] -
                szz_bins[q * total_bins + bi];
        }
        if (!array_is_finite(delta_bins, total_bins)) {
            free(delta_bins);
            return 1;
        }
        double mean = 0.0;
        double err = 0.0;
        PROF_BEGIN(prof, t_consistency_jackknife);
        jackknife(delta_bins, total_bins, &mean, &err);
        PROF_END(prof, PROF_JACKKNIFE, t_consistency_jackknife);
        if (!isfinite(mean) || !isfinite(err)) {
            free(delta_bins);
            return 1;
        }
        const int folded_mx =
            plan->Lx > 1 && plan->mx[q] > plan->Lx / 2
                ? plan->mx[q] - plan->Lx
                : plan->mx[q];
        const int folded_my =
            plan->Ly > 1 && plan->my[q] > plan->Ly / 2
                ? plan->my[q] - plan->Ly
                : plan->my[q];
        const double qx = plan->Lx > 1
                              ? 2.0 * (double)plan->mx[q] / plan->Lx
                              : 0.0;
        const double qy = plan->Ly > 1
                              ? 2.0 * (double)plan->my[q] / plan->Ly
                              : 0.0;
        const double qx_folded =
            plan->Lx > 1 ? 2.0 * (double)folded_mx / plan->Lx : 0.0;
        const double qy_folded =
            plan->Ly > 1 ? 2.0 * (double)folded_my / plan->Ly : 0.0;
        fprintf(fp,
                "%.17g %.17g %.17g %d %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n",
                beta_requested, beta, T, q, plan->mx[q], plan->my[q], qx,
                qy, qx_folded, qy_folded, mean, err);
    }
    free(delta_bins);
    return (fflush(fp) != 0 || ferror(fp)) ? 1 : 0;
}

static int plans_have_same_q(const StructureFactorPlan *a,
                             const StructureFactorPlan *b)
{
    if (a == NULL || b == NULL || !a->enabled || !b->enabled ||
        a->nq != b->nq || a->Lx != b->Lx || a->Ly != b->Ly) {
        return 0;
    }
    for (int q = 0; q < a->nq; q++) {
        if (a->mx[q] != b->mx[q] || a->my[q] != b->my[q]) {
            return 0;
        }
    }
    return 1;
}

static void free_structure_plans(StructureFactorPlan *szz_plan,
                                 StructureFactorPlan *sperp_storage,
                                 int plans_shared)
{
    structure_factor_plan_free(szz_plan);
    if (!plans_shared) {
        structure_factor_plan_free(sperp_storage);
    }
}

/* Index of momentum (mx,my) in a plan, or -1 when the plan is disabled or the
   momentum was not selected. */
static int plan_q_index(const StructureFactorPlan *plan, int mx, int my)
{
    if (plan == NULL || !plan->enabled) {
        return -1;
    }
    for (int q = 0; q < plan->nq; q++) {
        if (plan->mx[q] == mx && plan->my[q] == my) {
            return q;
        }
    }
    return -1;
}

static void fill_bin_meta(ReplicaBinMeta *meta, const Params *p,
                          const Lattice *L, int beta_index, int Ltr,
                          const StructureFactorPlan *szz_plan,
                          const StructureFactorPlan *sperp_plan)
{
    const int is_square = (strcmp(p->lattice, "square") == 0);
    const int Qx = p->Lx / 2;
    const int Qy = is_square ? p->Ly / 2 : 0;
    const int is_file = strcmp(p->lattice, "file") == 0;
    const int has_Q = !is_file && (p->Lx % 2 == 0) && (!is_square || p->Ly % 2 == 0);
    meta->beta_index = beta_index;
    meta->Ltr = Ltr;
    meta->nsite = L->n;
    meta->beta_requested = p->beta_list[beta_index];
    meta->beta_effective = (double)Ltr * p->dtau;
    meta->U = p->U;
    meta->dtau = p->dtau;
    meta->nwarm = p->nwarm;
    meta->nmeas = p->nmeas;
    meta->nbin = p->nbin;
    meta->lattice = p->lattice;
    meta->Lx = L->Lx;
    meta->Ly = L->Ly;
    meta->pbc = p->pbc;
    meta->global_update = p->global_update;
    meta->global_interval = p->global_interval;
    meta->szz_Q_index = has_Q ? plan_q_index(szz_plan, Qx, Qy) : -1;
    meta->szz_0_index = is_file ? -1 : plan_q_index(szz_plan, 0, 0);
    meta->sperp_Q_index = has_Q ? plan_q_index(sperp_plan, Qx, Qy) : -1;
}


static int write_bin_view(FILE *fp, const ReplicaBinView *view,
                          const ReplicaBinMeta *meta, int write_header)
{
    if (fp == NULL) {
        return 0;
    }
    int write_failed = replica_bin_write(fp, view, meta, write_header) != 0;
#ifdef AFQMC_TEST_HOOKS
    write_failed |= getenv("AFQMC_TEST_BIN_WRITE_FAIL") != NULL;
#endif
    if (write_failed) {
        fprintf(stderr, "ERROR: failed to write replica_bin_file at beta_index=%d\n",
                meta->beta_index);
        return 1;
    }
    return 0;
}

static int write_serial_bins(FILE *fp, const ReplicaResult *results,
                              const Params *p, const Lattice *L, int beta_index,
                              int Ltr, const StructureFactorPlan *szz_plan,
                              const StructureFactorPlan *sperp_plan)
{
    if (fp == NULL) {
        return 0;
    }
    const size_t count = (size_t)p->nrep * (size_t)p->nbin;
    const int nz = szz_plan->enabled ? szz_plan->nq : 0;
    const int np = sperp_plan->enabled ? sperp_plan->nq : 0;
    if (count > SIZE_MAX / sizeof(ReplicaBin) ||
        (nz > 0 && count > SIZE_MAX / sizeof(double) / (size_t)nz) ||
        (np > 0 && count > SIZE_MAX / sizeof(double) / (size_t)np)) {
        return 1;
    }
    ReplicaBin *bins = malloc(count * sizeof *bins);
    double *szz = nz > 0 ? malloc(count * (size_t)nz * sizeof *szz) : NULL;
    double *sperp = np > 0 ? malloc(count * (size_t)np * sizeof *sperp) : NULL;
    int *ids = malloc((size_t)p->nrep * sizeof *ids);
    unsigned long long *seeds = malloc((size_t)p->nrep * sizeof *seeds);
    int failed = bins == NULL || ids == NULL || seeds == NULL ||
                 (nz > 0 && szz == NULL) || (np > 0 && sperp == NULL);
    if (!failed) {
        for (int r = 0; r < p->nrep; r++) {
            const size_t offset = (size_t)r * (size_t)p->nbin;
            memcpy(bins + offset, results[r].bins, (size_t)p->nbin * sizeof *bins);
            if (nz > 0) {
                memcpy(szz + offset * (size_t)nz, results[r].szz.sum_sign_values,
                       (size_t)p->nbin * (size_t)nz * sizeof *szz);
            }
            if (np > 0) {
                memcpy(sperp + offset * (size_t)np, results[r].sperp.sum_sign_values,
                       (size_t)p->nbin * (size_t)np * sizeof *sperp);
            }
            ids[r] = results[r].replica_id;
            seeds[r] = results[r].seed;
        }
        const ReplicaBinView view = {p->nrep, p->nbin, nz, np, bins, szz,
                                     sperp, ids, seeds};
        ReplicaBinMeta meta;
        fill_bin_meta(&meta, p, L, beta_index, Ltr, szz_plan, sperp_plan);
        failed = write_bin_view(fp, &view, &meta, beta_index == 0);
    }
    free(bins);
    free(szz);
    free(sperp);
    free(ids);
    free(seeds);
    return failed;
}

#ifdef AFQMC_USE_MPI
static int write_gathered_bins(FILE *fp, const ReplicaBin *bins,
                                const double *szz, const double *sperp,
                                const unsigned long long *seeds,
                                const Params *p, const Lattice *L,
                                int beta_index, int Ltr,
                                const StructureFactorPlan *szz_plan,
                                const StructureFactorPlan *sperp_plan)
{
    if (fp == NULL) {
        return 0;
    }
    int *ids = malloc((size_t)p->nrep * sizeof *ids);
    if (ids == NULL) {
        return 1;
    }
    for (int r = 0; r < p->nrep; r++) {
        ids[r] = r;
    }
    const ReplicaBinView view = {p->nrep, p->nbin, szz_plan->nq,
                                 sperp_plan->nq, bins, szz, sperp, ids, seeds};
    ReplicaBinMeta meta;
    fill_bin_meta(&meta, p, L, beta_index, Ltr, szz_plan, sperp_plan);
    const int failed = write_bin_view(fp, &view, &meta, beta_index == 0);
    free(ids);
    return failed;
}
#endif

static int close_outputs(const MpiEnv *env, FILE **scalar_fp,
                         FILE **replica_fp, FILE **szz_fp,
                         FILE **sperp_fp, FILE **consistency_fp,
                         FILE **bin_fp, Profiler *prof)
{
    int failed = 0;
    profiler_set_current(NULL);
    if (mpi_is_root(env)) {
        if (scalar_fp != NULL && *scalar_fp != NULL) {
            if (fclose(*scalar_fp) != 0) {
                fprintf(stderr, "ERROR: failed to close output_file\n");
                failed = 1;
            }
            *scalar_fp = NULL;
        }
        if (replica_fp != NULL && *replica_fp != NULL) {
            if (fclose(*replica_fp) != 0) {
                failed = 1;
            }
            *replica_fp = NULL;
        }
        if (szz_fp != NULL && *szz_fp != NULL) {
            if (fclose(*szz_fp) != 0) {
                failed = 1;
            }
            *szz_fp = NULL;
        }
        if (sperp_fp != NULL && *sperp_fp != NULL) {
            if (fclose(*sperp_fp) != 0) {
                failed = 1;
            }
            *sperp_fp = NULL;
        }
        if (consistency_fp != NULL && *consistency_fp != NULL) {
            if (fclose(*consistency_fp) != 0) {
                failed = 1;
            }
            *consistency_fp = NULL;
        }
        if (bin_fp != NULL && *bin_fp != NULL) {
            int close_rc = fclose(*bin_fp);
            *bin_fp = NULL;
#ifdef AFQMC_TEST_HOOKS
            close_rc |= getenv("AFQMC_TEST_BIN_CLOSE_FAIL") != NULL;
#endif
            if (close_rc != 0) {
                fprintf(stderr, "ERROR: failed to close replica_bin_file\n");
                failed = 1;
            }
        }
        profiler_close(prof);
        if (profiler_error(prof)) {
            failed = 1;
        }
    } else {
        profiler_close(prof);
    }
    return failed;
}

static int init_stab_drift_file(const Params *p)
{
    if (p->stab_drift_file[0] == '\0') {
        return 0;
    }
    FILE *fp = fopen(p->stab_drift_file, "w");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: failed to open stab_drift_file %s\n",
                p->stab_drift_file);
        return 1;
    }
    fprintf(fp,
            "# beta_index Ltr replica_id seed U dtau stab use_ph samples max_inf mean_inf max_tau max_sweep failed\n");
    if (fclose(fp) != 0) {
        fprintf(stderr, "ERROR: failed to close stab_drift_file %s\n",
                p->stab_drift_file);
        return 1;
    }
    return 0;
}

static int init_udv_scale_file(const Params *p)
{
    if (p->udv_scale_file[0] == '\0') {
        return 0;
    }
    FILE *fp = fopen(p->udv_scale_file, "w");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: failed to open udv_scale_file %s\n",
                p->udv_scale_file);
        return 1;
    }
    fprintf(fp,
            "# beta_index Ltr replica_id seed U dtau stab_interval sweep_count tau boundary spin direction left_min_logD left_max_logD left_spread_logD left_finite_count left_zero_count left_nonfinite_count right_min_logD right_max_logD right_spread_logD right_finite_count right_zero_count right_nonfinite_count cross_max_logD\n");
    if (fclose(fp) != 0) {
        fprintf(stderr, "ERROR: failed to close udv_scale_file %s\n",
                p->udv_scale_file);
        return 1;
    }
    return 0;
}

static int init_udv_centered_file(const Params *p)
{
    if (p->udv_centered_file[0] == '\0') {
        return 0;
    }
    FILE *fp = fopen(p->udv_centered_file, "w");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: failed to open udv_centered_file %s\n",
                p->udv_centered_file);
        return 1;
    }
    fprintf(fp,
            "# beta_index Ltr replica_id seed sweep_count tau boundary spin direction stored_min_logD stored_max_logD stored_radius log_offset effective_min_logD effective_max_logD remaining_margin warning status\n");
    if (fclose(fp) != 0) {
        fprintf(stderr, "ERROR: failed to close udv_centered_file %s\n",
                p->udv_centered_file);
        return 1;
    }
    return 0;
}

#ifdef AFQMC_USE_MPI
static int run_replica_range(const Params *p, const Lattice *L, int beta_index,
                             int Ltr, double mu, double beta, double T,
                             int first_replica, int local_nrep,
                             const unsigned long long *global_seeds,
                             const char *parallel_label, int use_openmp,
                             const StructureFactorPlan *szz_plan,
                             const StructureFactorPlan *sperp_plan,
                             ReplicaResult *results,
                             Profiler *replica_profs,
                             int *replica_failed)
{
#ifndef AFQMC_USE_OPENMP
    (void)use_openmp;
#endif
#ifdef AFQMC_USE_OPENMP
#pragma omp parallel for schedule(static) if(use_openmp)
#endif
    for (int local = 0; local < local_nrep; local++) {
        const int gid = first_replica + local;
        profiler_init_memory(&replica_profs[local], p->profile);
        profiler_set_metadata(&replica_profs[local], 1, parallel_label);
        profiler_beta_begin(&replica_profs[local], beta, T, p->dtau, Ltr);
        if (dqmc_run_replica(p, L, beta_index, Ltr, mu, gid,
                             global_seeds[gid], szz_plan, sperp_plan,
                             &replica_profs[local], &results[local]) != 0) {
            replica_failed[local] = 1;
        }
    }
    return 0;
}

static int mpi_reduce_profiler_stats(const MpiEnv *env, const Profiler *local,
                                     Profiler *root)
{
    const int nstat = PROF_PHASE_COUNT * PROF_REGION_COUNT;
    unsigned long long *local_calls =
        calloc((size_t)nstat, sizeof(unsigned long long));
    unsigned long long *root_calls =
        mpi_is_root(env) ? calloc((size_t)nstat, sizeof(unsigned long long))
                         : NULL;
    double *local_sec = calloc((size_t)nstat, sizeof(double));
    double *root_sec =
        mpi_is_root(env) ? calloc((size_t)nstat, sizeof(double)) : NULL;
    int failed = local == NULL || local_calls == NULL || local_sec == NULL ||
                 (mpi_is_root(env) && (root_calls == NULL || root_sec == NULL));
    if (mpi_any_failed(env, failed)) {
        free(local_calls);
        free(root_calls);
        free(local_sec);
        free(root_sec);
        return 1;
    }

    int idx = 0;
    for (int ph = 0; ph < PROF_PHASE_COUNT; ph++) {
        for (int rg = 0; rg < PROF_REGION_COUNT; rg++) {
            local_calls[idx] = local->stat[ph][rg].calls;
            local_sec[idx] = local->stat[ph][rg].total_sec;
            idx++;
        }
    }

    MPI_Reduce(local_calls, root_calls, nstat, MPI_UNSIGNED_LONG_LONG, MPI_SUM,
               0, MPI_COMM_WORLD);
    MPI_Reduce(local_sec, root_sec, nstat, MPI_DOUBLE, MPI_SUM, 0,
               MPI_COMM_WORLD);

    if (mpi_is_root(env) && root != NULL) {
        idx = 0;
        for (int ph = 0; ph < PROF_PHASE_COUNT; ph++) {
            for (int rg = 0; rg < PROF_REGION_COUNT; rg++) {
                root->stat[ph][rg].calls = root_calls[idx];
                root->stat[ph][rg].total_sec = root_sec[idx];
                idx++;
            }
        }
    }

    free(local_calls);
    free(root_calls);
    free(local_sec);
    free(root_sec);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    MpiEnv mpi_env;
    mpi_env.enabled = 0;
    mpi_env.rank = 0;
    mpi_env.nranks = 1;
#ifdef AFQMC_USE_MPI
    MPI_Init(&argc, &argv);
    mpi_env.enabled = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_env.rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_env.nranks);
#endif

    if (argc < 2) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr, "usage: %s input.txt\n", argv[0]);
        }
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }

    Params p;
    if (params_read(&p, argv[1]) != 0) {
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    int scalar_path_failed = mpi_is_root(&mpi_env) &&
        validate_scalar_output_path(&p, argv[1]);
    if (mpi_any_failed(&mpi_env, scalar_path_failed)) {
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    if (p.replica_bin_file[0] != '\0') {
        const char *conflict = replica_bin_conflict(&p);
        if (mpi_any_failed(&mpi_env, conflict != NULL)) {
            if (mpi_is_root(&mpi_env)) {
                fprintf(stderr, "ERROR: output path collision: replica_bin_file and %s both use %s\n",
                        conflict != NULL ? conflict : "an output on another rank",
                        p.replica_bin_file);
            }
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }
    }
    const int global_enabled = strcmp(p.global_update, "site") == 0;
    Lattice L;
    if (strcmp(p.lattice, "file") == 0) {
        if (p.latfile[0] == '\0') {
            if (mpi_is_root(&mpi_env)) {
                fprintf(stderr, "ERROR: lattice=file requires latfile\n");
            }
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }
        if (lattice_from_file(&L, p.latfile) != 0) {
            if (mpi_is_root(&mpi_env)) {
                fprintf(stderr, "ERROR: failed to read latfile %s\n",
                        p.latfile);
            }
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }
    } else if (strcmp(p.lattice, "square") == 0) {
        lattice_square(&L, p.Lx, p.Ly, p.thop, p.pbc);
    } else if (strcmp(p.lattice, "chain") == 0) {
        lattice_chain(&L, p.Lx, p.thop, p.pbc);
    } else {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr, "ERROR: unknown lattice %s\n", p.lattice);
        }
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    if (mpi_is_root(&mpi_env)) {
        (void)lattice_dump(&L, "hopping_used.txt");
    }

    if (!L.is_bipartite) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: non-bipartite lattice is outside v1 scope; sign "
                    "column would be only relative without determinant-sign "
                    "initialization\n");
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }

    const double mu = p.U / 2.0;

#ifndef AFQMC_USE_MPI
    if (parallel_uses_mpi(p.parallel)) {
        fprintf(stderr, "ERROR: parallel=%s requires MPI-enabled build\n",
                p.parallel);
        lattice_free(&L);
        return 1;
    }
#endif
#ifndef AFQMC_USE_OPENMP
    if (parallel_uses_openmp(p.parallel)) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: parallel=%s requires OpenMP-enabled build\n",
                    p.parallel);
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
#endif
#ifdef AFQMC_USE_MPI
    if (mpi_env.nranks > 1 && strcmp(p.parallel, "serial") == 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: parallel=serial under MPI requires mpirun -np 1\n");
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
#ifdef AFQMC_USE_OPENMP
    if (mpi_env.nranks > 1 && strcmp(p.parallel, "omp") == 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: parallel=omp under MPI requires mpirun -np 1\n");
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
#endif
#endif

    if (p.stab_drift_file[0] != '\0' && strcmp(p.parallel, "serial") != 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: stab_drift_file currently supports parallel=serial only\n");
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    if (p.udv_scale_file[0] != '\0' && strcmp(p.parallel, "serial") != 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: udv_scale_file currently supports parallel=serial only\n");
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    if (p.udv_centered_file[0] != '\0' &&
        strcmp(p.parallel, "serial") != 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: udv_centered_file currently supports parallel=serial only\n");
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    if (p.udv_centered_file[0] != '\0' &&
        strcmp(p.green_rebuild, "centered") != 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: udv_centered_file requires green_rebuild=centered\n");
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    if (mpi_is_root(&mpi_env) && init_stab_drift_file(&p) != 0) {
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    if (mpi_is_root(&mpi_env) && init_udv_scale_file(&p) != 0) {
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    if (mpi_is_root(&mpi_env) && init_udv_centered_file(&p) != 0) {
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }

    const int bin_shape_failed = p.nbin > 0 && p.nrep > INT_MAX / p.nbin;
    if (mpi_any_failed(&mpi_env, bin_shape_failed)) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr, "ERROR: nrep*nbin exceeds INT_MAX\n");
        }
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }

    StructureFactorPlan szz_plan = {0};
    StructureFactorPlan sperp_plan_storage = {0};
    const StructureFactorPlan *sperp_plan = &sperp_plan_storage;
    int plans_shared = 0;
    int spin_plan_failed =
        structure_factor_plan_init(&szz_plan, &L, p.szz_q) != 0;
    if (!spin_plan_failed && strcmp(p.szz_q, p.sperp_q) == 0) {
        sperp_plan = &szz_plan;
        plans_shared = 1;
    } else if (!spin_plan_failed &&
               structure_factor_plan_init(&sperp_plan_storage, &L,
                                          p.sperp_q) != 0) {
        spin_plan_failed = 1;
    }
    if (!spin_plan_failed &&
        ((szz_plan.enabled && strcmp(p.szz_file, "none") == 0) ||
         (sperp_plan->enabled && strcmp(p.sperp_file, "none") == 0))) {
        spin_plan_failed = 1;
    }
    const int consistency_enabled =
        strcmp(p.spin_consistency_file, "none") != 0;
    if (!spin_plan_failed && consistency_enabled &&
        !plans_have_same_q(&szz_plan, sperp_plan)) {
        spin_plan_failed = 1;
    }
    if (mpi_any_failed(&mpi_env, spin_plan_failed)) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: invalid spin structure-factor setup "
                    "szz_selector=%s szz_file=%s sperp_selector=%s "
                    "sperp_file=%s consistency_file=%s lattice=%s "
                    "Lx=%d Ly=%d\n",
                    p.szz_q, p.szz_file, p.sperp_q, p.sperp_file,
                    p.spin_consistency_file, p.lattice, L.Lx, L.Ly);
        }
        free_structure_plans(&szz_plan, &sperp_plan_storage, plans_shared);
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }

    const int write_replica_log =
        strcmp(p.replica_log, "none") != 0 &&
        (p.replica_log[0] != '\0' || p.nrep > 1);
    const char *replica_log_path =
        (p.replica_log[0] != '\0') ? p.replica_log : "replicas.dat";
    const char *profile_path =
        (p.profile_file[0] != '\0') ? p.profile_file : "profile.dat";
    ActiveOutputPath active_outputs[5];
    int noutputs = 0;
    if (p.profile) {
        active_outputs[noutputs++] =
            (ActiveOutputPath){"profile_file", profile_path};
    }
    if (write_replica_log) {
        active_outputs[noutputs++] =
            (ActiveOutputPath){"replica_log", replica_log_path};
    }
    if (szz_plan.enabled) {
        active_outputs[noutputs++] =
            (ActiveOutputPath){"szz_file", p.szz_file};
    }
    if (sperp_plan->enabled) {
        active_outputs[noutputs++] =
            (ActiveOutputPath){"sperp_file", p.sperp_file};
    }
    if (consistency_enabled) {
        active_outputs[noutputs++] = (ActiveOutputPath){
            "spin_consistency_file", p.spin_consistency_file};
    }
    int collision_first = -1;
    int collision_second = -1;
    const int output_path_failed = find_output_path_collision(
        active_outputs, noutputs, &collision_first, &collision_second);
    if (mpi_any_failed(&mpi_env, output_path_failed)) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: output path collision: %s and %s both use %s\n",
                    active_outputs[collision_first].name,
                    active_outputs[collision_second].name,
                    active_outputs[collision_first].path);
        }
        free_structure_plans(&szz_plan, &sperp_plan_storage, plans_shared);
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }

    FILE *scalar_fp = NULL;
    Profiler prof;
    int setup_failed = 0;
    if (mpi_is_root(&mpi_env)) {
        setup_failed |= scalar_output_open(&scalar_fp, p.output_file);
        profiler_init(&prof, p.profile, p.profile_file);
        if (profiler_error(&prof)) {
            fprintf(stderr, "ERROR: failed to initialize profiler output\n");
            setup_failed = 1;
        }
    } else {
        profiler_init_memory(&prof, p.profile);
    }
    profiler_set_current(&prof);

    if (mpi_is_root(&mpi_env)) {
        setup_failed |= scalar_output_printf(scalar_fp, "# lattice=%s n=%d U=%g mu=%g dtau=%g bipartite=%d green_rebuild=%s parallel=%s nrep=%d bins=%d",
               p.lattice, L.n, p.U, mu, p.dtau, L.is_bipartite,
               p.green_rebuild, p.parallel, p.nrep, p.nrep * p.nbin);
        if (parallel_uses_mpi(p.parallel)) {
            setup_failed |= scalar_output_printf(scalar_fp, " nranks=%d", mpi_env.nranks);
        }
        if (szz_plan.enabled) {
            setup_failed |= scalar_output_printf(scalar_fp, " szz_file=%s szz_nq=%d", p.szz_file, szz_plan.nq);
        }
        if (sperp_plan->enabled) {
            setup_failed |= scalar_output_printf(scalar_fp, " sperp_file=%s sperp_nq=%d", p.sperp_file,
                   sperp_plan->nq);
        }
        if (consistency_enabled) {
            setup_failed |= scalar_output_printf(scalar_fp, " spin_consistency_file=%s", p.spin_consistency_file);
        }
        if (strcmp(p.global_update, "site") == 0) {
            setup_failed |= scalar_output_printf(scalar_fp, " global_update=site global_interval=%d", p.global_interval);
        }
        setup_failed |= scalar_output_printf(scalar_fp, "\n");
        if (strcmp(p.global_update, "site") == 0) {
            setup_failed |= scalar_output_printf(scalar_fp, "# T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign  acceptance dAcceptance  global_acceptance global_attempts\n");
        } else {
            setup_failed |= scalar_output_printf(scalar_fp, "# T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign  acceptance dAcceptance\n");
        }
        setup_failed |= scalar_output_flush(scalar_fp);
    }

    FILE *replica_fp = NULL;
    FILE *szz_fp = NULL;
    FILE *sperp_fp = NULL;
    FILE *consistency_fp = NULL;
    FILE *bin_fp = NULL;
    if (p.replica_bin_file[0] != '\0' && mpi_is_root(&mpi_env)) {
        bin_fp = fopen(p.replica_bin_file, "w");
        if (bin_fp == NULL) {
            fprintf(stderr, "ERROR: cannot open replica_bin_file %s\n",
                    p.replica_bin_file);
            setup_failed = 1;
        }
    }
    if (write_replica_log && mpi_is_root(&mpi_env)) {
        replica_fp = fopen(replica_log_path, "w");
        if (replica_fp == NULL) {
            fprintf(stderr, "ERROR: failed to open replica_log %s\n",
                    replica_log_path);
            setup_failed = 1;
        } else if (parallel_uses_mpi(p.parallel)) {
            fprintf(replica_fp,
                    "# beta T replica_id seed nwarm nmeas nbin status rank\n");
        } else {
            fprintf(replica_fp,
                    "# beta T replica_id seed nwarm nmeas nbin status\n");
        }
        if (replica_fp != NULL && ferror(replica_fp)) {
            fprintf(stderr, "ERROR: failed to write replica_log\n");
            setup_failed = 1;
        }
    }

    if (szz_plan.enabled && mpi_is_root(&mpi_env)) {
        szz_fp = fopen(p.szz_file, "w");
        if (szz_fp == NULL) {
            fprintf(stderr, "ERROR: failed to open szz_file %s\n",
                    p.szz_file);
            setup_failed = 1;
        } else {
            fprintf(szz_fp,
                    "# definition=Szz(q)=N^-1 sum_ij exp[-iq.(ri-rj)] <Szi Szj>\n");
            fprintf(szz_fp,
                    "# spin_operator=Szi=(n_up-n_down)/2 factor3_applied=0\n");
            fprintf(szz_fp,
                    "# lattice=%s Lx=%d Ly=%d n=%d pbc=%d t=%.17g U=%.17g mu=%.17g dtau=%.17g\n",
                    p.lattice, L.Lx, L.Ly, L.n, p.pbc, p.thop, p.U, mu,
                    p.dtau);
            fprintf(szz_fp,
                    "# szz_q=%s seed=%llu parallel=%s nrep=%d bins=%d nbeta=%d\n",
                    p.szz_q, p.seed, p.parallel, p.nrep, p.nrep * p.nbin,
                    p.nbeta);
            fprintf(szz_fp,
                    "# beta_requested beta T q_index mx my qx_over_pi qy_over_pi qx_folded_over_pi qy_folded_over_pi Szz dSzz\n");
            if (ferror(szz_fp)) {
                fprintf(stderr, "ERROR: failed to write szz_file header\n");
                setup_failed = 1;
            }
        }
    }

    if (sperp_plan->enabled && mpi_is_root(&mpi_env)) {
        sperp_fp = fopen(p.sperp_file, "w");
        if (sperp_fp == NULL) {
            fprintf(stderr, "ERROR: failed to open sperp_file %s\n",
                    p.sperp_file);
            setup_failed = 1;
        } else {
            fprintf(sperp_fp,
                    "# definition=Sperp(q)=N^-1 sum_ij exp[-iq.(ri-rj)] <Sxi Sxj + Syi Syj>\n");
            fprintf(sperp_fp,
                    "# ladder_definition=Sperp(q)=0.5*(S+-(q)+S-+(q)); Splus=Sx+iSy\n");
            fprintf(sperp_fp,
                    "# su2_relation=ensemble Sperp(q)=2*Szz(q) at any dtau for the current SU(2)-invariant model\n");
            fprintf(sperp_fp,
                    "# hs_caveat=individual spin-channel-HS samples select the z axis; equality is restored by ensemble averaging\n");
            fprintf(sperp_fp,
                    "# lattice=%s Lx=%d Ly=%d n=%d pbc=%d t=%.17g U=%.17g mu=%.17g dtau=%.17g\n",
                    p.lattice, L.Lx, L.Ly, L.n, p.pbc, p.thop, p.U, mu,
                    p.dtau);
            fprintf(sperp_fp,
                    "# sperp_q=%s seed=%llu parallel=%s nrep=%d bins=%d nbeta=%d\n",
                    p.sperp_q, p.seed, p.parallel, p.nrep, p.nrep * p.nbin,
                    p.nbeta);
            fprintf(sperp_fp,
                    "# beta_requested beta T q_index mx my qx_over_pi qy_over_pi qx_folded_over_pi qy_folded_over_pi Sperp dSperp\n");
            if (ferror(sperp_fp)) {
                fprintf(stderr, "ERROR: failed to write sperp_file header\n");
                setup_failed = 1;
            }
        }
    }

    if (consistency_enabled && mpi_is_root(&mpi_env)) {
        consistency_fp = fopen(p.spin_consistency_file, "w");
        if (consistency_fp == NULL) {
            fprintf(stderr,
                    "ERROR: failed to open spin_consistency_file %s\n",
                    p.spin_consistency_file);
            setup_failed = 1;
        } else {
            fprintf(consistency_fp,
                    "# definition=DeltaSU2(q)=0.5*Sperp(q)-Szz(q)\n");
            fprintf(consistency_fp,
                    "# error=paired jackknife over the same sign-reweighted replica/bin samples\n");
            fprintf(consistency_fp,
                    "# lattice=%s Lx=%d Ly=%d n=%d pbc=%d t=%.17g U=%.17g mu=%.17g dtau=%.17g\n",
                    p.lattice, L.Lx, L.Ly, L.n, p.pbc, p.thop, p.U, mu,
                    p.dtau);
            fprintf(consistency_fp,
                    "# szz_q=%s sperp_q=%s seed=%llu parallel=%s nrep=%d bins=%d nbeta=%d\n",
                    p.szz_q, p.sperp_q, p.seed, p.parallel, p.nrep,
                    p.nrep * p.nbin, p.nbeta);
            fprintf(consistency_fp,
                    "# beta_requested beta T q_index mx my qx_over_pi qy_over_pi qx_folded_over_pi qy_folded_over_pi DeltaSU2 dDeltaSU2\n");
            if (ferror(consistency_fp)) {
                fprintf(stderr,
                        "ERROR: failed to write spin_consistency_file header\n");
                setup_failed = 1;
            }
        }
    }

    if (mpi_any_failed(&mpi_env, setup_failed)) {
        (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                            &consistency_fp, &bin_fp, &prof);
        free_structure_plans(&szz_plan, &sperp_plan_storage, plans_shared);
        lattice_free(&L);
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }

    for (int b = 0; b < p.nbeta; b++) {
        const double beta = p.beta_list[b];
        const int Ltr = (int)lround(beta / p.dtau);
        if (fabs(beta / p.dtau - (double)Ltr) > 1e-9) {
            if (mpi_is_root(&mpi_env)) {
                fprintf(stderr,
                        "ERROR: beta=%g not a multiple of dtau=%g "
                        "(beta/dtau=%g)\n",
                        beta, p.dtau, beta / p.dtau);
            }
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }
        const double beta_eff = Ltr * p.dtau;
        const double T = 1.0 / beta_eff;
        profiler_beta_begin(&prof, beta, T, p.dtau, Ltr);
        if (parallel_uses_mpi(p.parallel)) {
            profiler_set_mpi_metadata(&prof, p.nrep, p.parallel,
                                      mpi_env.nranks);
        } else {
            profiler_set_metadata(&prof, p.nrep, p.parallel);
        }

        const int total_bins = p.nrep * p.nbin;
        unsigned long long *seeds = NULL;
        ReplicaResult *results = NULL;
        Profiler *replica_profs = NULL;
        int *replica_failed = NULL;
        const int replica_alloc_failed =
            alloc_replica_arrays(p.nrep, &seeds, &results, &replica_profs,
                                 &replica_failed);
        if (mpi_any_failed(&mpi_env, replica_alloc_failed)) {
            if (mpi_is_root(&mpi_env)) {
                fprintf(stderr, "ERROR: failed to allocate replica arrays\n");
            }
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }

        for (int r = 0; r < p.nrep; r++) {
            seeds[r] = replica_seed(p.seed, b, r);
        }
        const int seed_failed = replica_check_seed_unique(seeds, p.nrep) != 0;
        if (mpi_any_failed(&mpi_env, seed_failed)) {
            if (mpi_is_root(&mpi_env)) {
                fprintf(stderr, "ERROR: duplicate replica seed at beta=%g\n",
                        beta);
            }
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }

#ifdef AFQMC_USE_MPI
        if (parallel_uses_mpi(p.parallel)) {
            int mpi_beta_failed = 0;
            int local_setup_failed = 0;
            int first_replica = 0;
            int local_nrep = 0;
            int local_bins = 0;
            int any_failed = 0;
            int status_alloc_failed = 0;
            int gather_alloc_failed = 0;
            int gather_layout_failed = 0;
            int local_pack_failed = 0;
            int root_post_failed = 0;

            ReplicaResult *local_results = NULL;
            Profiler *local_replica_profs = NULL;
            int *local_replica_failed = NULL;
            int *local_failed = NULL;
            int *all_failed = NULL;
            int *status_counts = NULL;
            int *status_displs = NULL;
            double *local_values = NULL;
            int *local_counts = NULL;
            double *all_values = NULL;
            int *all_counts = NULL;
            int *value_counts = NULL;
            int *value_displs = NULL;
            int *count_counts = NULL;
            int *count_displs = NULL;
            double *local_szz = NULL;
            double *all_szz = NULL;
            double *szz_bins = NULL;
            double *szz_values = NULL;
            int *szz_counts = NULL;
            int *szz_displs = NULL;
            double *local_sperp = NULL;
            double *all_sperp = NULL;
            double *sperp_bins = NULL;
            double *sperp_values = NULL;
            int *sperp_counts = NULL;
            int *sperp_displs = NULL;
            ReplicaBin *global_bins = NULL;
            double *Ehub = NULL;
            double *Egc = NULL;
            double *Eph = NULL;
            double *Nbin = NULL;
            double *Dbin = NULL;
            double *Sbin = NULL;
            double *Accbin = NULL;
            Profiler rank_prof;
            profiler_init_memory(&rank_prof, p.profile);
            profiler_set_mpi_metadata(&rank_prof, p.nrep, p.parallel,
                                      mpi_env.nranks);
            profiler_beta_begin(&rank_prof, beta, T, p.dtau, Ltr);

            replica_mpi_rank_range(p.nrep, mpi_env.nranks, mpi_env.rank,
                                   &first_replica, &local_nrep);
            local_bins = local_nrep * p.nbin;

            if (alloc_replica_arrays(local_nrep, NULL, &local_results,
                                     &local_replica_profs,
                                     &local_replica_failed) != 0) {
                local_setup_failed = 1;
            }
            if (mpi_any_failed(&mpi_env, local_setup_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }

            run_replica_range(&p, &L, b, Ltr, mu, beta, T, first_replica,
                              local_nrep, seeds, p.parallel,
                              strcmp(p.parallel, "hybrid") == 0,
                              &szz_plan, sperp_plan,
                              local_results, local_replica_profs,
                              local_replica_failed);
            for (int local = 0; local < local_nrep; local++) {
                profiler_merge(&rank_prof, &local_replica_profs[local]);
            }

            local_failed = calloc((size_t)local_nrep, sizeof(int));
            status_alloc_failed = (local_failed == NULL && local_nrep > 0);
            if (mpi_env.rank == 0) {
                all_failed = calloc((size_t)p.nrep, sizeof(int));
            }
            status_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
            status_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
            if ((mpi_env.rank == 0 && all_failed == NULL) ||
                status_counts == NULL || status_displs == NULL) {
                status_alloc_failed = 1;
            }
            if (mpi_any_failed(&mpi_env, status_alloc_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }

            gather_layout_failed = replica_mpi_gatherv_layout(
                p.nrep, 1, mpi_env.nranks, 1, status_counts, status_displs);
            if (mpi_any_failed(&mpi_env, gather_layout_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }
            for (int local = 0; local < local_nrep; local++) {
                local_failed[local] = replica_mpi_local_failed(
                    local_replica_failed[local], &local_results[local]);
            }
            MPI_Gatherv(local_failed, local_nrep, MPI_INT, all_failed,
                        status_counts, status_displs, MPI_INT, 0,
                        MPI_COMM_WORLD);

            any_failed = 0;
            if (mpi_env.rank == 0) {
                for (int r = 0; r < p.nrep; r++) {
                    const int failed = all_failed[r] != 0;
                    const char *status = failed ? "error" : "ok";
                    const int owner =
                        replica_mpi_rank_for_replica(p.nrep, mpi_env.nranks,
                                                     r);
                    if (replica_fp != NULL) {
                        fprintf(replica_fp,
                                "%.17g %.17g %d %llu %d %d %d %s %d\n",
                                beta, T, r, seeds[r], p.nwarm, p.nmeas,
                                p.nbin, status, owner);
                    }
                    if (failed) {
                        fprintf(stderr, "ERROR: replica %d failed\n", r);
                        any_failed = 1;
                    }
                }
                if (replica_fp != NULL && ferror(replica_fp)) {
                    fprintf(stderr, "ERROR: failed to write replica_log\n");
                    any_failed = 1;
                }
            }
            if (mpi_any_failed(&mpi_env, any_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }

            if (local_bins > 0) {
                local_values =
                    calloc((size_t)local_bins * REPLICA_MPI_BIN_DOUBLES,
                           sizeof(double));
                local_counts = calloc((size_t)local_bins, sizeof(int));
            }
            value_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
            value_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
            count_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
            count_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
            if (szz_plan.enabled) {
                szz_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
                szz_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
            }
            if (sperp_plan->enabled) {
                sperp_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
                sperp_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
            }
            gather_alloc_failed =
                (local_bins > 0 &&
                 (local_values == NULL || local_counts == NULL)) ||
                value_counts == NULL || value_displs == NULL ||
                count_counts == NULL || count_displs == NULL ||
                (szz_plan.enabled &&
                 (szz_counts == NULL || szz_displs == NULL)) ||
                (sperp_plan->enabled &&
                 (sperp_counts == NULL || sperp_displs == NULL));
            if (szz_plan.enabled && local_bins > 0) {
                if ((size_t)local_bins > SIZE_MAX / (size_t)szz_plan.nq ||
                    (size_t)local_bins * (size_t)szz_plan.nq > INT_MAX ||
                    (size_t)local_bins * (size_t)szz_plan.nq >
                        SIZE_MAX / sizeof(double)) {
                    gather_alloc_failed = 1;
                } else {
                    local_szz = malloc((size_t)local_bins *
                                       (size_t)szz_plan.nq * sizeof(double));
                    if (local_szz == NULL) {
                        gather_alloc_failed = 1;
                    }
                }
            }
            if (sperp_plan->enabled && local_bins > 0) {
                if ((size_t)local_bins > SIZE_MAX / (size_t)sperp_plan->nq ||
                    (size_t)local_bins * (size_t)sperp_plan->nq > INT_MAX ||
                    (size_t)local_bins * (size_t)sperp_plan->nq >
                        SIZE_MAX / sizeof(double)) {
                    gather_alloc_failed = 1;
                } else {
                    local_sperp =
                        malloc((size_t)local_bins *
                               (size_t)sperp_plan->nq * sizeof(double));
                    if (local_sperp == NULL) {
                        gather_alloc_failed = 1;
                    }
                }
            }
            if (mpi_env.rank == 0) {
                all_values =
                    calloc((size_t)total_bins * REPLICA_MPI_BIN_DOUBLES,
                           sizeof(double));
                all_counts = calloc((size_t)total_bins, sizeof(int));
                if (all_values == NULL || all_counts == NULL) {
                    gather_alloc_failed = 1;
                }
                if (szz_plan.enabled) {
                    if ((size_t)total_bins >
                            SIZE_MAX / (size_t)szz_plan.nq ||
                        (size_t)total_bins * (size_t)szz_plan.nq > INT_MAX ||
                        (size_t)total_bins * (size_t)szz_plan.nq >
                            SIZE_MAX / sizeof(double)) {
                        gather_alloc_failed = 1;
                    } else {
                        const size_t szz_count =
                            (size_t)total_bins * (size_t)szz_plan.nq;
                        all_szz = malloc(szz_count * sizeof(double));
                        szz_bins = malloc(szz_count * sizeof(double));
                        szz_values =
                            malloc((size_t)szz_plan.nq * sizeof(double));
                        if (all_szz == NULL || szz_bins == NULL ||
                            szz_values == NULL) {
                            gather_alloc_failed = 1;
                        } else {
                            for (size_t i = 0; i < szz_count; i++) {
                                all_szz[i] = NAN;
                            }
                        }
                    }
                }
                if (sperp_plan->enabled) {
                    if ((size_t)total_bins >
                            SIZE_MAX / (size_t)sperp_plan->nq ||
                        (size_t)total_bins * (size_t)sperp_plan->nq >
                            INT_MAX ||
                        (size_t)total_bins * (size_t)sperp_plan->nq >
                            SIZE_MAX / sizeof(double)) {
                        gather_alloc_failed = 1;
                    } else {
                        const size_t sperp_count =
                            (size_t)total_bins * (size_t)sperp_plan->nq;
                        all_sperp = malloc(sperp_count * sizeof(double));
                        sperp_bins = malloc(sperp_count * sizeof(double));
                        sperp_values =
                            malloc((size_t)sperp_plan->nq * sizeof(double));
                        if (all_sperp == NULL || sperp_bins == NULL ||
                            sperp_values == NULL) {
                            gather_alloc_failed = 1;
                        } else {
                            for (size_t i = 0; i < sperp_count; i++) {
                                all_sperp[i] = NAN;
                            }
                        }
                    }
                }
            }
            if (mpi_any_failed(&mpi_env, gather_alloc_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }

            replica_mpi_pack_bins(local_results, local_nrep, p.nbin,
                                  local_values, local_counts);
            if (szz_plan.enabled) {
                local_pack_failed = replica_mpi_pack_szz(
                    local_results, local_nrep, p.nbin, szz_plan.nq,
                    local_szz);
            }
            if (sperp_plan->enabled) {
                local_pack_failed =
                    local_pack_failed ||
                    replica_mpi_pack_sperp(local_results, local_nrep, p.nbin,
                                           sperp_plan->nq, local_sperp);
            }
            if (mpi_any_failed(&mpi_env, local_pack_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }
            gather_layout_failed =
                replica_mpi_gatherv_layout(
                    p.nrep, p.nbin, mpi_env.nranks,
                    REPLICA_MPI_BIN_DOUBLES, value_counts, value_displs) ||
                replica_mpi_gatherv_layout(p.nrep, p.nbin, mpi_env.nranks, 1,
                                           count_counts, count_displs);
            if (szz_plan.enabled) {
                gather_layout_failed =
                    gather_layout_failed ||
                    replica_mpi_gatherv_layout(
                        p.nrep, p.nbin, mpi_env.nranks, szz_plan.nq,
                        szz_counts, szz_displs);
            }
            if (sperp_plan->enabled) {
                gather_layout_failed =
                    gather_layout_failed ||
                    replica_mpi_gatherv_layout(
                        p.nrep, p.nbin, mpi_env.nranks, sperp_plan->nq,
                        sperp_counts, sperp_displs);
            }
            if (mpi_any_failed(&mpi_env, gather_layout_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }
            MPI_Gatherv(local_values,
                        local_bins * REPLICA_MPI_BIN_DOUBLES, MPI_DOUBLE,
                        all_values, value_counts, value_displs, MPI_DOUBLE, 0,
                        MPI_COMM_WORLD);
            MPI_Gatherv(local_counts, local_bins, MPI_INT, all_counts,
                        count_counts, count_displs, MPI_INT, 0,
                        MPI_COMM_WORLD);
            if (szz_plan.enabled) {
                MPI_Gatherv(local_szz, local_bins * szz_plan.nq, MPI_DOUBLE,
                            all_szz, szz_counts, szz_displs, MPI_DOUBLE, 0,
                            MPI_COMM_WORLD);
            }
            if (sperp_plan->enabled) {
                MPI_Gatherv(local_sperp, local_bins * sperp_plan->nq,
                            MPI_DOUBLE, all_sperp, sperp_counts, sperp_displs,
                            MPI_DOUBLE, 0, MPI_COMM_WORLD);
            }

            root_post_failed = 0;
            if (mpi_env.rank == 0) {
                global_bins = calloc((size_t)total_bins, sizeof(ReplicaBin));
                Ehub = calloc((size_t)total_bins, sizeof(double));
                Egc = calloc((size_t)total_bins, sizeof(double));
                Eph = calloc((size_t)total_bins, sizeof(double));
                Nbin = calloc((size_t)total_bins, sizeof(double));
                Dbin = calloc((size_t)total_bins, sizeof(double));
                Sbin = calloc((size_t)total_bins, sizeof(double));
                Accbin = calloc((size_t)total_bins, sizeof(double));
                root_post_failed =
                    global_bins == NULL || Ehub == NULL || Egc == NULL ||
                    Eph == NULL || Nbin == NULL || Dbin == NULL ||
                    Sbin == NULL || Accbin == NULL;
                if (szz_plan.enabled &&
                    (all_szz == NULL || szz_bins == NULL ||
                     szz_values == NULL)) {
                    root_post_failed = 1;
                }
                if (sperp_plan->enabled &&
                    (all_sperp == NULL || sperp_bins == NULL ||
                     sperp_values == NULL)) {
                    root_post_failed = 1;
                }
                if (!root_post_failed) {
                    replica_mpi_unpack_bins(all_values, all_counts,
                                            total_bins, global_bins);
                    for (int out = 0; out < total_bins; out++) {
                        if (replica_bin_values(&global_bins[out], &Ehub[out],
                                               &Egc[out], &Eph[out],
                                               &Nbin[out], &Dbin[out],
                                               &Sbin[out]) != 0) {
                            const int replica_id = out / p.nbin;
                            const int bin_id = out % p.nbin;
                            const int owner =
                                replica_mpi_rank_for_replica(
                                    p.nrep, mpi_env.nranks, replica_id);
                            const ReplicaBin *bad = &global_bins[out];
                            fprintf(stderr,
                                    "ERROR: invalid bin at beta=%.17g flat_bin=%d replica=%d bin=%d owner_rank=%d count=%d sum_sign=%.17g sum_sign_Ehub=%.17g sum_sign_Egc=%.17g sum_sign_Eph=%.17g sum_sign_N=%.17g sum_sign_D=%.17g\n",
                                    beta, out, replica_id, bin_id, owner,
                                    bad->count, bad->sum_sign,
                                    bad->sum_sign_Ehub, bad->sum_sign_Egc,
                                    bad->sum_sign_Eph, bad->sum_sign_N,
                                    bad->sum_sign_D);
                            root_post_failed = 1;
                            break;
                        }
                        Accbin[out] =
                            replica_bin_acceptance(&global_bins[out]);
                        if (szz_plan.enabled) {
                            const double *numerator =
                                &all_szz[out * szz_plan.nq];
                            for (int q = 0; q < szz_plan.nq; q++) {
                                if (!isfinite(numerator[q])) {
                                    fprintf(stderr,
                                            "ERROR: incomplete MPI Szz "
                                            "receive at beta=%.17g "
                                            "flat_bin=%d q=%d\n",
                                            beta, out, q);
                                    root_post_failed = 1;
                                    break;
                                }
                            }
                            if (root_post_failed) {
                                break;
                            }
                            if (szz_bin_ratio(numerator, szz_plan.nq,
                                              global_bins[out].sum_sign,
                                              szz_values) != 0) {
                                fprintf(stderr,
                                        "ERROR: invalid gathered Szz ratio "
                                        "at beta=%.17g flat_bin=%d "
                                        "sum_sign=%.17g nq=%d\n",
                                        beta, out,
                                        global_bins[out].sum_sign,
                                        szz_plan.nq);
                                root_post_failed = 1;
                                break;
                            }
                            SzzSumRuleDiagnostics sum_rule;
                            if (szz_sum_rule_check(
                                    &szz_plan, szz_values, Nbin[out],
                                    Dbin[out], &sum_rule) != 0) {
                                fprintf(stderr,
                                        "ERROR: gathered Szz sum rule failed "
                                        "at beta=%.17g flat_bin=%d "
                                        "lhs=%.17g rhs=%.17g diff=%.17g "
                                        "tol=%.17g sum_sign=%.17g\n",
                                        beta, out, sum_rule.lhs,
                                        sum_rule.rhs, sum_rule.difference,
                                        sum_rule.tolerance,
                                        global_bins[out].sum_sign);
                                root_post_failed = 1;
                                break;
                            }
                            for (int q = 0; q < szz_plan.nq; q++) {
                                szz_bins[q * total_bins + out] =
                                    szz_values[q];
                            }
                        }
                        if (sperp_plan->enabled) {
                            const double *numerator =
                                &all_sperp[out * sperp_plan->nq];
                            for (int q = 0; q < sperp_plan->nq; q++) {
                                if (!isfinite(numerator[q])) {
                                    fprintf(stderr,
                                            "ERROR: incomplete MPI Sperp "
                                            "receive at beta=%.17g "
                                            "flat_bin=%d q=%d\n",
                                            beta, out, q);
                                    root_post_failed = 1;
                                    break;
                                }
                            }
                            if (root_post_failed) {
                                break;
                            }
                            if (sperp_bin_ratio(
                                    numerator, sperp_plan->nq,
                                    global_bins[out].sum_sign,
                                    sperp_values) != 0) {
                                fprintf(stderr,
                                        "ERROR: invalid gathered Sperp ratio "
                                        "at beta=%.17g flat_bin=%d "
                                        "sum_sign=%.17g nq=%d\n",
                                        beta, out,
                                        global_bins[out].sum_sign,
                                        sperp_plan->nq);
                                root_post_failed = 1;
                                break;
                            }
                            StructureFactorSumRuleDiagnostics sum_rule;
                            if (sperp_sum_rule_check(
                                    sperp_plan, sperp_values, Nbin[out],
                                    Dbin[out], &sum_rule) != 0) {
                                fprintf(stderr,
                                        "ERROR: gathered Sperp sum rule "
                                        "failed at beta=%.17g flat_bin=%d "
                                        "lhs=%.17g rhs=%.17g diff=%.17g "
                                        "tol=%.17g sum_sign=%.17g\n",
                                        beta, out, sum_rule.lhs,
                                        sum_rule.rhs, sum_rule.difference,
                                        sum_rule.tolerance,
                                        global_bins[out].sum_sign);
                                root_post_failed = 1;
                                break;
                            }
                            for (int q = 0; q < sperp_plan->nq; q++) {
                                sperp_bins[q * total_bins + out] =
                                    sperp_values[q];
                            }
                        }
                    }
                }
            }
            if (mpi_any_failed(&mpi_env, root_post_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }

            if (p.profile) {
                if (mpi_reduce_profiler_stats(&mpi_env, &rank_prof, &prof) !=
                    0) {
                    mpi_beta_failed = 1;
                    goto mpi_beta_cleanup;
                }
            }

            if (mpi_env.rank == 0) {
                unsigned long long global_attempts = 0ULL;
                const double global_acceptance = replica_bins_global_acceptance(
                    global_bins, total_bins, &global_attempts);
                root_post_failed = jackknife_and_print(
                    scalar_fp, &prof, total_bins, T, Ehub, Egc, Eph, Nbin, Dbin, Sbin,
                    Accbin, global_enabled, global_acceptance, global_attempts);
                if (!root_post_failed && szz_plan.enabled) {
                    root_post_failed = write_q_beta(
                        szz_fp, &prof, &szz_plan, beta, beta_eff, T, szz_bins,
                        total_bins);
                }
                if (!root_post_failed && sperp_plan->enabled) {
                    root_post_failed = write_q_beta(
                        sperp_fp, &prof, sperp_plan, beta, beta_eff, T,
                        sperp_bins, total_bins);
                }
                if (!root_post_failed && consistency_enabled) {
                    root_post_failed = write_spin_consistency_beta(
                        consistency_fp, &prof, &szz_plan, beta, beta_eff, T,
                        szz_bins, sperp_bins, total_bins);
                }
                if (!root_post_failed && bin_fp != NULL) {
                    root_post_failed = write_gathered_bins(
                        bin_fp, global_bins, all_szz, all_sperp, seeds,
                        &p, &L, b, Ltr, &szz_plan, sperp_plan);
                }
                if (p.profile) {
                    profiler_set_mpi_metadata(&prof, p.nrep, p.parallel,
                                              mpi_env.nranks);
                    profiler_beta_end(&prof);
                }
            }
            if (mpi_any_failed(&mpi_env, root_post_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }

        mpi_beta_cleanup:
            free(local_failed);
            free(all_failed);
            free(status_counts);
            free(status_displs);
            free(local_values);
            free(local_counts);
            free(all_values);
            free(all_counts);
            free(value_counts);
            free(value_displs);
            free(count_counts);
            free(count_displs);
            free(local_szz);
            free(all_szz);
            free(szz_bins);
            free(szz_values);
            free(szz_counts);
            free(szz_displs);
            free(local_sperp);
            free(all_sperp);
            free(sperp_bins);
            free(sperp_values);
            free(sperp_counts);
            free(sperp_displs);
            free(global_bins);
            free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin,
                                    Accbin);
            free_replica_arrays(NULL, local_results, local_replica_profs,
                                local_replica_failed, local_nrep);
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            if (mpi_any_failed(&mpi_env, mpi_beta_failed)) {
                (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                    &sperp_fp, &consistency_fp, &bin_fp, &prof);
                free_structure_plans(&szz_plan, &sperp_plan_storage,
                                     plans_shared);
                lattice_free(&L);
                mpi_finalize_if_enabled(&mpi_env);
                return 1;
            }
            continue;
        }
#endif

#ifdef AFQMC_USE_OPENMP
        if (strcmp(p.parallel, "omp") == 0) {
#pragma omp parallel for schedule(static)
            for (int r = 0; r < p.nrep; r++) {
                profiler_init_memory(&replica_profs[r], p.profile);
                profiler_set_metadata(&replica_profs[r], 1, "omp");
                profiler_beta_begin(&replica_profs[r], beta, T, p.dtau, Ltr);
                if (dqmc_run_replica(&p, &L, b, Ltr, mu, r, seeds[r],
                                     &szz_plan, sperp_plan, &replica_profs[r],
                                     &results[r]) != 0) {
                    replica_failed[r] = 1;
                }
            }
        } else
#endif
        {
            for (int r = 0; r < p.nrep; r++) {
                profiler_init_memory(&replica_profs[r], p.profile);
                profiler_set_metadata(&replica_profs[r], 1, "serial");
                profiler_beta_begin(&replica_profs[r], beta, T, p.dtau, Ltr);
                if (dqmc_run_replica(&p, &L, b, Ltr, mu, r, seeds[r],
                                     &szz_plan, sperp_plan, &replica_profs[r],
                                     &results[r]) != 0) {
                    replica_failed[r] = 1;
                }
            }
        }

        for (int r = 0; r < p.nrep; r++) {
            profiler_merge(&prof, &replica_profs[r]);
        }

        int any_failed = 0;
        for (int r = 0; r < p.nrep; r++) {
            if (replica_failed[r]) {
                fprintf(stderr, "ERROR: replica %d failed\n", r);
                any_failed = 1;
            }
        }

        if (replica_fp != NULL) {
            for (int r = 0; r < p.nrep; r++) {
                const int failed = replica_failed[r] || results[r].status != 0;
                const char *status = failed ? "error" : "ok";
                fprintf(replica_fp, "%.17g %.17g %d %llu %d %d %d %s\n",
                        beta, T, r, seeds[r], p.nwarm, p.nmeas, p.nbin,
                        status);
            }
            if (ferror(replica_fp)) {
                fprintf(stderr, "ERROR: failed to write replica_log\n");
                any_failed = 1;
            }
        }

        if (any_failed) {
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }

        double *Ehub = calloc((size_t)total_bins, sizeof(double));
        double *Egc = calloc((size_t)total_bins, sizeof(double));
        double *Eph = calloc((size_t)total_bins, sizeof(double));
        double *Nbin = calloc((size_t)total_bins, sizeof(double));
        double *Dbin = calloc((size_t)total_bins, sizeof(double));
        double *Sbin = calloc((size_t)total_bins, sizeof(double));
        double *Accbin = calloc((size_t)total_bins, sizeof(double));
        double *szz_bins = NULL;
        double *szz_values = NULL;
        double *sperp_bins = NULL;
        double *sperp_values = NULL;
        if (Ehub == NULL || Egc == NULL || Eph == NULL || Nbin == NULL ||
            Dbin == NULL || Sbin == NULL || Accbin == NULL) {
            fprintf(stderr, "ERROR: failed to allocate measurement bins\n");
            free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin,
                                    Accbin);
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }
        if (szz_plan.enabled) {
            if (total_bins <= 0 ||
                (size_t)szz_plan.nq > SIZE_MAX / (size_t)total_bins ||
                (size_t)szz_plan.nq * (size_t)total_bins >
                    SIZE_MAX / sizeof(double)) {
                fprintf(stderr, "ERROR: Szz bin dimensions overflow\n");
                free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin,
                                        Accbin);
                free_replica_arrays(seeds, results, replica_profs,
                                    replica_failed, p.nrep);
                (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                    &sperp_fp, &consistency_fp, &bin_fp, &prof);
                free_structure_plans(&szz_plan, &sperp_plan_storage,
                                     plans_shared);
                lattice_free(&L);
                mpi_finalize_if_enabled(&mpi_env);
                return 1;
            }
            szz_bins = malloc((size_t)szz_plan.nq * (size_t)total_bins *
                              sizeof(double));
            szz_values = malloc((size_t)szz_plan.nq * sizeof(double));
            if (szz_bins == NULL || szz_values == NULL) {
                fprintf(stderr, "ERROR: failed to allocate Szz finalization bins\n");
                free(szz_bins);
                free(szz_values);
                free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin,
                                        Accbin);
                free_replica_arrays(seeds, results, replica_profs,
                                    replica_failed, p.nrep);
                (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                    &sperp_fp, &consistency_fp, &bin_fp, &prof);
                free_structure_plans(&szz_plan, &sperp_plan_storage,
                                     plans_shared);
                lattice_free(&L);
                mpi_finalize_if_enabled(&mpi_env);
                return 1;
            }
            for (size_t i = 0;
                 i < (size_t)szz_plan.nq * (size_t)total_bins; i++) {
                szz_bins[i] = NAN;
            }
        }
        if (sperp_plan->enabled) {
            if (total_bins <= 0 ||
                (size_t)sperp_plan->nq > SIZE_MAX / (size_t)total_bins ||
                (size_t)sperp_plan->nq * (size_t)total_bins >
                    SIZE_MAX / sizeof(double)) {
                fprintf(stderr, "ERROR: Sperp bin dimensions overflow\n");
                free(szz_bins);
                free(szz_values);
                free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin,
                                        Accbin);
                free_replica_arrays(seeds, results, replica_profs,
                                    replica_failed, p.nrep);
                (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                    &sperp_fp, &consistency_fp, &bin_fp, &prof);
                free_structure_plans(&szz_plan, &sperp_plan_storage,
                                     plans_shared);
                lattice_free(&L);
                mpi_finalize_if_enabled(&mpi_env);
                return 1;
            }
            sperp_bins =
                malloc((size_t)sperp_plan->nq * (size_t)total_bins *
                       sizeof(double));
            sperp_values = malloc((size_t)sperp_plan->nq * sizeof(double));
            if (sperp_bins == NULL || sperp_values == NULL) {
                fprintf(stderr,
                        "ERROR: failed to allocate Sperp finalization bins\n");
                free(szz_bins);
                free(szz_values);
                free(sperp_bins);
                free(sperp_values);
                free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin,
                                        Accbin);
                free_replica_arrays(seeds, results, replica_profs,
                                    replica_failed, p.nrep);
                (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                    &sperp_fp, &consistency_fp, &bin_fp, &prof);
                free_structure_plans(&szz_plan, &sperp_plan_storage,
                                     plans_shared);
                lattice_free(&L);
                mpi_finalize_if_enabled(&mpi_env);
                return 1;
            }
            for (size_t i = 0;
                 i < (size_t)sperp_plan->nq * (size_t)total_bins; i++) {
                sperp_bins[i] = NAN;
            }
        }

        for (int r = 0; r < p.nrep; r++) {
            for (int bi = 0; bi < p.nbin; bi++) {
                const int out = r * p.nbin + bi;
                if (replica_bin_values(&results[r].bins[bi], &Ehub[out],
                                       &Egc[out], &Eph[out], &Nbin[out],
                                       &Dbin[out], &Sbin[out]) != 0) {
                    const ReplicaBin *bad = &results[r].bins[bi];
                    fprintf(stderr,
                            "ERROR: invalid bin at beta=%.17g replica=%d bin=%d count=%d sum_sign=%.17g sum_sign_Ehub=%.17g sum_sign_Egc=%.17g sum_sign_Eph=%.17g sum_sign_N=%.17g sum_sign_D=%.17g\n",
                            beta, r, bi, bad->count, bad->sum_sign,
                            bad->sum_sign_Ehub, bad->sum_sign_Egc,
                            bad->sum_sign_Eph, bad->sum_sign_N,
                            bad->sum_sign_D);
                    free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin,
                                            Accbin);
                    free(szz_bins);
                    free(szz_values);
                    free(sperp_bins);
                    free(sperp_values);
                    free_replica_arrays(seeds, results, replica_profs,
                                        replica_failed, p.nrep);
                    (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                        &sperp_fp, &consistency_fp, &bin_fp, &prof);
                    free_structure_plans(&szz_plan, &sperp_plan_storage,
                                         plans_shared);
                    lattice_free(&L);
                    mpi_finalize_if_enabled(&mpi_env);
                    return 1;
                }
                Accbin[out] = replica_bin_acceptance(&results[r].bins[bi]);
                if (szz_plan.enabled) {
                    if (replica_result_szz_bin_values(
                            &results[r], bi, szz_values, szz_plan.nq) != 0) {
                        fprintf(stderr,
                                "ERROR: invalid Szz ratio at beta=%.17g "
                                "replica=%d bin=%d sum_sign=%.17g "
                                "requested_nq=%d result_nq=%d\n",
                                beta, r, bi, results[r].bins[bi].sum_sign,
                                szz_plan.nq, results[r].szz.nq);
                        free(szz_bins);
                        free(szz_values);
                        free(sperp_bins);
                        free(sperp_values);
                        free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin,
                                                Sbin, Accbin);
                        free_replica_arrays(seeds, results, replica_profs,
                                            replica_failed, p.nrep);
                        (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                            &sperp_fp, &consistency_fp, &bin_fp, &prof);
                        free_structure_plans(&szz_plan, &sperp_plan_storage,
                                             plans_shared);
                        lattice_free(&L);
                        mpi_finalize_if_enabled(&mpi_env);
                        return 1;
                    }
                    SzzSumRuleDiagnostics sum_rule;
                    if (szz_sum_rule_check(&szz_plan, szz_values, Nbin[out],
                                           Dbin[out], &sum_rule) != 0) {
                        fprintf(stderr,
                                "ERROR: Szz sum rule failed at beta=%.17g "
                                "replica=%d bin=%d lhs=%.17g rhs=%.17g "
                                "diff=%.17g tol=%.17g sum_sign=%.17g\n",
                                beta, r, bi, sum_rule.lhs, sum_rule.rhs,
                                sum_rule.difference, sum_rule.tolerance,
                                results[r].bins[bi].sum_sign);
                        free(szz_bins);
                        free(szz_values);
                        free(sperp_bins);
                        free(sperp_values);
                        free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin,
                                                Sbin, Accbin);
                        free_replica_arrays(seeds, results, replica_profs,
                                            replica_failed, p.nrep);
                        (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                            &sperp_fp, &consistency_fp, &bin_fp, &prof);
                        free_structure_plans(&szz_plan, &sperp_plan_storage,
                                             plans_shared);
                        lattice_free(&L);
                        mpi_finalize_if_enabled(&mpi_env);
                        return 1;
                    }
                    for (int q = 0; q < szz_plan.nq; q++) {
                        szz_bins[q * total_bins + out] = szz_values[q];
                    }
                }
                if (sperp_plan->enabled) {
                    if (replica_result_sperp_bin_values(
                            &results[r], bi, sperp_values,
                            sperp_plan->nq) != 0) {
                        fprintf(stderr,
                                "ERROR: invalid Sperp ratio at beta=%.17g "
                                "replica=%d bin=%d sum_sign=%.17g "
                                "requested_nq=%d result_nq=%d\n",
                                beta, r, bi, results[r].bins[bi].sum_sign,
                                sperp_plan->nq, results[r].sperp.nq);
                        free(szz_bins);
                        free(szz_values);
                        free(sperp_bins);
                        free(sperp_values);
                        free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin,
                                                Sbin, Accbin);
                        free_replica_arrays(seeds, results, replica_profs,
                                            replica_failed, p.nrep);
                        (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                            &sperp_fp, &consistency_fp, &bin_fp, &prof);
                        free_structure_plans(&szz_plan, &sperp_plan_storage,
                                             plans_shared);
                        lattice_free(&L);
                        mpi_finalize_if_enabled(&mpi_env);
                        return 1;
                    }
                    StructureFactorSumRuleDiagnostics sum_rule;
                    if (sperp_sum_rule_check(sperp_plan, sperp_values,
                                             Nbin[out], Dbin[out],
                                             &sum_rule) != 0) {
                        fprintf(stderr,
                                "ERROR: Sperp sum rule failed at beta=%.17g "
                                "replica=%d bin=%d lhs=%.17g rhs=%.17g "
                                "diff=%.17g tol=%.17g sum_sign=%.17g\n",
                                beta, r, bi, sum_rule.lhs, sum_rule.rhs,
                                sum_rule.difference, sum_rule.tolerance,
                                results[r].bins[bi].sum_sign);
                        free(szz_bins);
                        free(szz_values);
                        free(sperp_bins);
                        free(sperp_values);
                        free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin,
                                                Sbin, Accbin);
                        free_replica_arrays(seeds, results, replica_profs,
                                            replica_failed, p.nrep);
                        (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp,
                                            &sperp_fp, &consistency_fp, &bin_fp, &prof);
                        free_structure_plans(&szz_plan, &sperp_plan_storage,
                                             plans_shared);
                        lattice_free(&L);
                        mpi_finalize_if_enabled(&mpi_env);
                        return 1;
                    }
                    for (int q = 0; q < sperp_plan->nq; q++) {
                        sperp_bins[q * total_bins + out] = sperp_values[q];
                    }
                }
            }
        }

        unsigned long long global_attempts = 0ULL;
        unsigned long long global_accepted = 0ULL;
        for (int r = 0; r < p.nrep; r++) {
            for (int bi = 0; bi < p.nbin; bi++) {
                global_attempts += results[r].bins[bi].global_attempts;
                global_accepted += results[r].bins[bi].global_accepted;
            }
        }
        const double global_acceptance = global_attempts == 0ULL ? NAN :
            (double)global_accepted / (double)global_attempts;
        if (jackknife_and_print(scalar_fp, &prof, total_bins, T, Ehub, Egc, Eph, Nbin,
                                 Dbin, Sbin, Accbin, global_enabled,
                                 global_acceptance, global_attempts) != 0) {
            free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin, Accbin);
            free(szz_bins);
            free(szz_values);
            free(sperp_bins);
            free(sperp_values);
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }

        if (szz_plan.enabled &&
            write_q_beta(szz_fp, &prof, &szz_plan, beta, beta_eff, T,
                         szz_bins, total_bins) != 0) {
            fprintf(stderr, "ERROR: failed to finalize Szz at beta=%.17g\n",
                    beta);
            free(szz_bins);
            free(szz_values);
            free(sperp_bins);
            free(sperp_values);
            free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin, Accbin);
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }

        if (sperp_plan->enabled &&
            write_q_beta(sperp_fp, &prof, sperp_plan, beta, beta_eff, T,
                         sperp_bins, total_bins) != 0) {
            fprintf(stderr,
                    "ERROR: failed to finalize Sperp at beta=%.17g\n", beta);
            free(szz_bins);
            free(szz_values);
            free(sperp_bins);
            free(sperp_values);
            free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin, Accbin);
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }

        if (consistency_enabled &&
            write_spin_consistency_beta(
                consistency_fp, &prof, &szz_plan, beta, beta_eff, T,
                szz_bins, sperp_bins, total_bins) != 0) {
            fprintf(stderr,
                    "ERROR: failed to finalize spin consistency at "
                    "beta=%.17g\n",
                    beta);
            free(szz_bins);
            free(szz_values);
            free(sperp_bins);
            free(sperp_values);
            free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin, Accbin);
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            (void)close_outputs(&mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage,
                                 plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }

        if (write_serial_bins(bin_fp, results, &p, &L, b, Ltr,
                               &szz_plan, sperp_plan) != 0) {
            fprintf(stderr, "ERROR: failed to finalize replica bins at beta=%.17g\n", beta);
            free(szz_bins);
            free(szz_values);
            free(sperp_bins);
            free(sperp_values);
            free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin, Accbin);
            free_replica_arrays(seeds, results, replica_profs, replica_failed, p.nrep);
            (void)close_outputs(&mpi_env, &replica_fp, &szz_fp, &sperp_fp,
                                &consistency_fp, &bin_fp, &prof);
            free_structure_plans(&szz_plan, &sperp_plan_storage, plans_shared);
            lattice_free(&L);
            mpi_finalize_if_enabled(&mpi_env);
            return 1;
        }

        free(szz_bins);
        free(szz_values);
        free(sperp_bins);
        free(sperp_values);
        free_measurement_arrays(Ehub, Egc, Eph, Nbin, Dbin, Sbin, Accbin);
        free_replica_arrays(seeds, results, replica_profs, replica_failed,
                            p.nrep);
        profiler_beta_end(&prof);
    }

    const int close_failed = close_outputs(
        &mpi_env, &scalar_fp, &replica_fp, &szz_fp, &sperp_fp, &consistency_fp, &bin_fp, &prof);
    free_structure_plans(&szz_plan, &sperp_plan_storage, plans_shared);
    lattice_free(&L);
    if (mpi_any_failed(&mpi_env, close_failed)) {
        mpi_finalize_if_enabled(&mpi_env);
        return 1;
    }
    mpi_finalize_if_enabled(&mpi_env);
    return 0;
}
