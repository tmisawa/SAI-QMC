#include "dqmc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dqmc_map_ph_down(Dqmc *D)
{
    green_build_ph_down(&D->Gu, D->m->bipart, D->Gd.g);
    D->Gd.n = D->Gu.n;
    D->Gd.L = D->Gu.L;
    D->Gd.cur_l = D->Gu.cur_l;
    D->Gd.sigma = -1.0;
    D->Gd.det_sign = D->Gu.det_sign;
}

static double mat_max_abs_diff(int n, const double *a, const double *b)
{
    double m = 0.0;
    for (int i = 0; i < n * n; i++) {
        const double d = fabs(a[i] - b[i]);
        if (d > m) {
            m = d;
        }
    }
    return m;
}

static void dqmc_invalidate_carried(Dqmc *D)
{
    D->carried_prefix_valid = 0;
    D->carried_suffix_valid = 0;
}

static void dqmc_udv_lmul(Dqmc *D, UDV *s, const double *B, LinalgWork *w)
{
    if (D->green_rebuild_mode == GREEN_REBUILD_CENTERED) {
        udv_lmul_centered_work(s, B, w);
    } else {
        udv_lmul_work(s, B, w);
    }
}

static void dqmc_udv_rmul(Dqmc *D, UDV *s, const double *B, LinalgWork *w)
{
    if (D->green_rebuild_mode == GREEN_REBUILD_CENTERED) {
        udv_rmul_centered(s, B, w);
    } else {
        udv_rmul(s, B, w);
    }
}

static int dqmc_rebuild_without_profile(Green *G, int tau)
{
    Profiler *old_prof = profiler_current();
    profiler_set_current(NULL);
    const int rc = green_from_scratch(G, tau);
    profiler_set_current(old_prof);
    return rc;
}

static int dqmc_record_stab_drift(Dqmc *D, int tau)
{
    DqmcStabDrift *drift = &D->stab_drift;
    if (!drift->enabled) {
        return 0;
    }

    if (dqmc_rebuild_without_profile(&drift->Gu_ref, tau) != 0 ||
        drift->Gu_ref.det_sign == 0) {
        drift->failed = 1;
        return 1;
    }
    double inf = mat_max_abs_diff(D->n, D->Gu.g, drift->Gu_ref.g);

    if (!D->use_ph) {
        if (dqmc_rebuild_without_profile(&drift->Gd_ref, tau) != 0 ||
            drift->Gd_ref.det_sign == 0) {
            drift->failed = 1;
            return 1;
        }
        const double inf_d = mat_max_abs_diff(D->n, D->Gd.g, drift->Gd_ref.g);
        if (inf_d > inf) {
            inf = inf_d;
        }
    }

    drift->samples++;
    drift->sum_inf += inf;
    if (inf > drift->max_inf || drift->samples == 1) {
        drift->max_inf = inf;
        drift->max_tau = tau;
        drift->max_sweep = D->sweep_count;
    }
    return 0;
}

typedef struct {
    int finite_count;
    int zero_count;
    int nonfinite_count;
    double min_log;
    double max_log;
} UdvScaleStats;

static UdvScaleStats udv_scale_stats(const UDV *s)
{
    UdvScaleStats out;
    memset(&out, 0, sizeof(out));
    if (s == NULL || s->D == NULL || s->n <= 0) {
        out.nonfinite_count = 1;
        return out;
    }
    for (int i = 0; i < s->n; i++) {
        const double d = s->D[i];
        if (!isfinite(d)) {
            out.nonfinite_count++;
            continue;
        }
        const double a = fabs(d);
        if (a == 0.0) {
            out.zero_count++;
            continue;
        }
        const double ld = log(a);
        if (!isfinite(ld)) {
            out.nonfinite_count++;
            continue;
        }
        if (out.finite_count == 0 || ld < out.min_log) {
            out.min_log = ld;
        }
        if (out.finite_count == 0 || ld > out.max_log) {
            out.max_log = ld;
        }
        out.finite_count++;
    }
    return out;
}

static double scale_stat_min(const UdvScaleStats *s)
{
    return (s->finite_count > 0) ? s->min_log : NAN;
}

static double scale_stat_max(const UdvScaleStats *s)
{
    return (s->finite_count > 0) ? s->max_log : NAN;
}

static double scale_stat_spread(const UdvScaleStats *s)
{
    return (s->finite_count > 0) ? s->max_log - s->min_log : NAN;
}

static int dqmc_write_udv_scale_diag(Dqmc *D, const char *spin,
                                     const char *direction, int tau,
                                     int boundary, const UdvScaleStats *ls,
                                     const UdvScaleStats *rs,
                                     double cross_max)
{
    DqmcUdvScaleDiag *diag = &D->udv_scale_diag;
    if (!diag->enabled) {
        return 0;
    }
    const double left_max = scale_stat_max(ls);
    const double right_max = scale_stat_max(rs);

    FILE *fp = fopen(diag->file, "a");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: failed to open udv_scale_file %s\n",
                diag->file);
        diag->failed = 1;
        return 1;
    }
    fprintf(fp,
            "%d %d %d %llu %.17g %.17g %d %llu %d %d %s %s %.17g %.17g %.17g %d %d %d %.17g %.17g %.17g %d %d %d %.17g\n",
            diag->beta_index, diag->Ltr, diag->replica_id, diag->seed,
            diag->U, diag->dtau, D->stab_interval, D->sweep_count, tau,
            boundary, spin, direction, scale_stat_min(ls), left_max,
            scale_stat_spread(ls), ls->finite_count, ls->zero_count,
            ls->nonfinite_count, scale_stat_min(rs), right_max,
            scale_stat_spread(rs), rs->finite_count, rs->zero_count,
            rs->nonfinite_count, cross_max);
    if (fclose(fp) != 0) {
        fprintf(stderr, "ERROR: failed to close udv_scale_file %s\n",
                diag->file);
        diag->failed = 1;
        return 1;
    }
    return 0;
}

static int dqmc_record_udv_scale_diag(Dqmc *D, const char *spin,
                                      const char *direction, int tau,
                                      int boundary, const UDV *left,
                                      const UDV *right)
{
    DqmcUdvScaleDiag *diag = &D->udv_scale_diag;
    if (!diag->enabled) {
        return 0;
    }
    const UdvScaleStats ls = udv_scale_stats(left);
    const UdvScaleStats rs = udv_scale_stats(right);
    const double left_max = scale_stat_max(&ls);
    const double right_max = scale_stat_max(&rs);
    const double cross_max =
        (isfinite(left_max) && isfinite(right_max)) ? left_max + right_max
                                                    : NAN;
    return dqmc_write_udv_scale_diag(D, spin, direction, tau, boundary, &ls,
                                     &rs, cross_max);
}

static int dqmc_record_single_udv_scale_diag(Dqmc *D, const char *spin,
                                             const char *direction, int tau,
                                             int boundary, const UDV *factor)
{
    DqmcUdvScaleDiag *diag = &D->udv_scale_diag;
    if (!diag->enabled) {
        return 0;
    }
    const UdvScaleStats ls = udv_scale_stats(factor);
    UdvScaleStats rs;
    memset(&rs, 0, sizeof(rs));
    return dqmc_write_udv_scale_diag(D, spin, direction, tau, boundary, &ls,
                                     &rs, NAN);
}

typedef struct {
    int valid;
    double stored_min;
    double stored_max;
    double radius;
    double log_offset;
    double effective_min;
    double effective_max;
    double remaining_margin;
} UdvCenteredStats;

static UdvCenteredStats udv_centered_stats(const UDV *factor)
{
    UdvCenteredStats out;
    memset(&out, 0, sizeof(out));
    out.stored_min = NAN;
    out.stored_max = NAN;
    out.radius = NAN;
    out.log_offset = (factor != NULL) ? factor->log_offset : NAN;
    out.effective_min = NAN;
    out.effective_max = NAN;
    out.remaining_margin = NAN;

    const UdvScaleStats scale = udv_scale_stats(factor);
    if (factor == NULL || scale.finite_count != factor->n ||
        scale.zero_count != 0 || scale.nonfinite_count != 0 ||
        !isfinite(factor->log_offset)) {
        return out;
    }

    out.stored_min = scale.min_log;
    out.stored_max = scale.max_log;
    out.radius = 0.5 * (scale.max_log - scale.min_log);
    out.effective_min = scale.min_log + factor->log_offset;
    out.effective_max = scale.max_log + factor->log_offset;
    out.remaining_margin = log(DBL_MAX) - out.radius;
    out.valid = isfinite(out.radius) && isfinite(out.effective_min) &&
                isfinite(out.effective_max) &&
                isfinite(out.remaining_margin);
    return out;
}

static const char *centered_failure_status(int reason)
{
    switch (reason) {
    case LINALG_FAILURE_CENTERED_RADIUS:
        return "hard_fail_centered_radius";
    case LINALG_FAILURE_CENTERED_UPDATE_MARGIN:
        return "hard_fail_centered_update_margin";
    case LINALG_FAILURE_EFFECTIVE_LOG_RANGE:
        return "hard_fail_effective_log_range";
    default:
        return "failed_unspecified";
    }
}

static int dqmc_record_udv_centered_diag(Dqmc *D, const char *spin,
                                         const char *direction, int tau,
                                         int boundary, const UDV *factor,
                                         const LinalgWork *work)
{
    DqmcUdvCenteredDiag *diag = &D->udv_centered_diag;
    if (!diag->enabled) {
        return 0;
    }

    const UdvCenteredStats stats = udv_centered_stats(factor);
    const int work_failed = work != NULL && work->failed;
    int warning = stats.valid && stats.remaining_margin <= 32.0;
    int diag_failure = 0;
    const char *status = "ok";
    if (work_failed) {
        status = centered_failure_status(work->failure_reason);
    } else if (!stats.valid) {
        status = "invalid_factor";
        diag_failure = 1;
    } else if (stats.remaining_margin <= 8.0) {
        status = "hard_fail_centered_radius";
        warning = 1;
        diag_failure = 1;
    } else if (warning) {
        status = "warning";
    }

    FILE *fp = fopen(diag->file, "a");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: failed to open udv_centered_file %s\n",
                diag->file);
        diag->failed = 1;
        return 1;
    }
    fprintf(fp,
            "%d %d %d %llu %llu %d %d %s %s %.17g %.17g %.17g %.17g %.17g %.17g %.17g %d %s\n",
            diag->beta_index, diag->Ltr, diag->replica_id, diag->seed,
            D->sweep_count, tau, boundary, spin, direction, stats.stored_min,
            stats.stored_max, stats.radius, stats.log_offset,
            stats.effective_min, stats.effective_max,
            stats.remaining_margin, warning, status);
    if (fclose(fp) != 0) {
        fprintf(stderr, "ERROR: failed to close udv_centered_file %s\n",
                diag->file);
        diag->failed = 1;
        return 1;
    }
    if (diag_failure) {
        diag->failed = 1;
        return 1;
    }
    return 0;
}

static int dqmc_record_udv_pair_diags(Dqmc *D, const char *spin,
                                      const char *direction, int tau,
                                      int boundary, const UDV *left,
                                      const UDV *right,
                                      const UDV *centered_factor,
                                      const LinalgWork *work)
{
    if (dqmc_record_udv_scale_diag(D, spin, direction, tau, boundary, left,
                                   right) != 0) {
        return 1;
    }
    return dqmc_record_udv_centered_diag(D, spin, direction, tau, boundary,
                                         centered_factor, work);
}

static int dqmc_record_single_udv_diags(Dqmc *D, const char *spin,
                                        const char *direction, int tau,
                                        int boundary, const UDV *factor,
                                        const LinalgWork *work)
{
    if (dqmc_record_single_udv_scale_diag(D, spin, direction, tau, boundary,
                                          factor) != 0) {
        return 1;
    }
    return dqmc_record_udv_centered_diag(D, spin, direction, tau, boundary,
                                         factor, work);
}

static int dqmc_green_scale_observer(void *ctx, const Green *G,
                                     const char *direction, int tau,
                                     int boundary, const UDV *factor)
{
    Dqmc *D = (Dqmc *)ctx;
    const char *spin =
        (G->scale_observer_spin != NULL) ? G->scale_observer_spin : "?";
    return dqmc_record_single_udv_diags(D, spin, direction, tau, boundary,
                                        factor, &G->work);
}

static void dqmc_clear_green_scale_observer(Green *G)
{
    G->scale_observer = NULL;
    G->scale_observer_ctx = NULL;
    G->scale_observer_spin = NULL;
}

static void dqmc_set_green_scale_observer(Dqmc *D, Green *G,
                                          const char *spin)
{
    G->scale_observer = dqmc_green_scale_observer;
    G->scale_observer_ctx = D;
    G->scale_observer_spin = spin;
}

static void dqmc_update_green_scale_observers(Dqmc *D)
{
    if (D->udv_scale_diag.enabled || D->udv_centered_diag.enabled) {
        dqmc_set_green_scale_observer(D, &D->Gu, "u");
        dqmc_set_green_scale_observer(D, &D->Gd, "d");
    } else {
        dqmc_clear_green_scale_observer(&D->Gu);
        dqmc_clear_green_scale_observer(&D->Gd);
    }
}

static int dqmc_stack_build_failed(Dqmc *D, const Green *G)
{
    if (G->work.failed || D->udv_scale_diag.failed ||
        D->udv_centered_diag.failed) {
        D->status = 1;
        dqmc_invalidate_carried(D);
        return 1;
    }
    return 0;
}

int dqmc_init_modes(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval,
                    Profiler *prof, DqmcSweepMode mode,
                    GreenRebuildMode rebuild_mode)
{
    memset(D, 0, sizeof(*D));
    D->n = m->n;
    D->L = f->L;
    D->m = m;
    D->f = f;
    D->rng = rng;
    D->stab_interval = (stab_interval > 0) ? stab_interval : 8;
    D->prof = prof;
    D->use_ph = m->ph_symmetric;
    D->sweep_mode = mode;
    D->sweep_dir = DQMC_DIR_FORWARD;
    D->green_rebuild_mode = rebuild_mode;
    if (mode != DQMC_SWEEP_FORWARD && mode != DQMC_SWEEP_ALTERNATING) {
        fprintf(stderr, "ERROR: invalid DQMC sweep mode %d\n", (int)mode);
        D->status = 1;
        D->sign = 0.0;
        return 1;
    }
    if (mode == DQMC_SWEEP_ALTERNATING && !D->use_ph) {
        fprintf(stderr,
                "ERROR: sweep_order=alternating currently requires "
                "half-filled particle-hole symmetry\n");
        D->status = 1;
        D->sign = 0.0;
        return 1;
    }
    green_alloc(&D->Gu, m, f, 1.0);
    green_alloc(&D->Gd, m, f, -1.0);
    green_set_rebuild_mode(&D->Gu, rebuild_mode);
    green_set_rebuild_mode(&D->Gd, rebuild_mode);
    green_stack_alloc(&D->stack_u, D->n, D->L, D->stab_interval);
    udv_init(&D->left_u, D->n);
    udv_init(&D->combined_u, D->n);
    if (!D->use_ph) {
        green_stack_alloc(&D->stack_d, D->n, D->L, D->stab_interval);
        udv_init(&D->left_d, D->n);
        udv_init(&D->combined_d, D->n);
    }
    if (D->sweep_mode == DQMC_SWEEP_ALTERNATING) {
        green_stack_alloc(&D->stack_alt_u, D->n, D->L, D->stab_interval);
        D->stack_alt_u_allocated = 1;
    }
    D->status = 0;
    D->sweep_count = 0;
    const int rcu = green_from_scratch(&D->Gu, 0);
    const int rcd = D->use_ph ? 0 : green_from_scratch(&D->Gd, 0);
    if (rcu != 0 || rcd != 0 || D->Gu.det_sign == 0 ||
        (!D->use_ph && D->Gd.det_sign == 0) || D->Gd.g == NULL) {
        D->status = 1;
        D->sign = 0.0;
        dqmc_invalidate_carried(D);
        return 1;
    } else {
        if (D->use_ph) {
            dqmc_map_ph_down(D);
            D->sign = 1.0;
        } else {
            D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);
        }
    }
    return 0;
}

int dqmc_init_mode(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval,
                   Profiler *prof, DqmcSweepMode mode)
{
    return dqmc_init_modes(D, m, f, rng, stab_interval, prof, mode,
                           GREEN_REBUILD_COMBINE);
}

void dqmc_init(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval,
               Profiler *prof)
{
    (void)dqmc_init_mode(D, m, f, rng, stab_interval, prof,
                         DQMC_SWEEP_FORWARD);
}

void dqmc_free(Dqmc *D)
{
    if (D->stab_drift.allocated) {
        if (!D->use_ph) {
            green_free(&D->stab_drift.Gd_ref);
        }
        green_free(&D->stab_drift.Gu_ref);
        D->stab_drift.allocated = 0;
    }
    udv_free(&D->combined_d);
    udv_free(&D->combined_u);
    udv_free(&D->left_d);
    udv_free(&D->left_u);
    if (D->stack_alt_u_allocated) {
        green_stack_free(&D->stack_alt_u);
    }
    green_stack_free(&D->stack_d);
    green_stack_free(&D->stack_u);
    green_free(&D->Gu);
    green_free(&D->Gd);
}

void dqmc_set_green_rebuild_mode(Dqmc *D, GreenRebuildMode mode)
{
    D->green_rebuild_mode = mode;
    green_set_rebuild_mode(&D->Gu, mode);
    green_set_rebuild_mode(&D->Gd, mode);
    if (D->stab_drift.allocated) {
        green_set_rebuild_mode(&D->stab_drift.Gu_ref, mode);
        if (!D->use_ph) {
            green_set_rebuild_mode(&D->stab_drift.Gd_ref, mode);
        }
    }
}

int dqmc_enable_stab_drift(Dqmc *D, int enabled)
{
    if (!enabled) {
        D->stab_drift.enabled = 0;
        return 0;
    }
    if (!D->stab_drift.allocated) {
        green_alloc(&D->stab_drift.Gu_ref, D->m, D->f, 1.0);
        if (!D->use_ph) {
            green_alloc(&D->stab_drift.Gd_ref, D->m, D->f, -1.0);
        }
        D->stab_drift.allocated = 1;
    }
    green_set_rebuild_mode(&D->stab_drift.Gu_ref, D->green_rebuild_mode);
    if (!D->use_ph) {
        green_set_rebuild_mode(&D->stab_drift.Gd_ref,
                               D->green_rebuild_mode);
    }
    D->stab_drift.enabled = 1;
    D->stab_drift.failed = 0;
    D->stab_drift.samples = 0;
    D->stab_drift.max_sweep = 0;
    D->stab_drift.max_tau = 0;
    D->stab_drift.max_inf = 0.0;
    D->stab_drift.sum_inf = 0.0;
    return 0;
}

int dqmc_enable_udv_scale_diag(Dqmc *D, const char *path, int beta_index,
                               int Ltr, int replica_id,
                               unsigned long long seed, double U,
                               double dtau)
{
    DqmcUdvScaleDiag *diag = &D->udv_scale_diag;
    if (path == NULL || path[0] == '\0') {
        memset(diag, 0, sizeof(*diag));
        dqmc_update_green_scale_observers(D);
        return 0;
    }
    diag->enabled = 1;
    diag->failed = 0;
    strncpy(diag->file, path, sizeof diag->file - 1);
    diag->file[sizeof diag->file - 1] = '\0';
    diag->beta_index = beta_index;
    diag->Ltr = Ltr;
    diag->replica_id = replica_id;
    diag->seed = seed;
    diag->U = U;
    diag->dtau = dtau;
    dqmc_update_green_scale_observers(D);
    return 0;
}

int dqmc_enable_udv_centered_diag(Dqmc *D, const char *path, int beta_index,
                                  int Ltr, int replica_id,
                                  unsigned long long seed)
{
    DqmcUdvCenteredDiag *diag = &D->udv_centered_diag;
    if (path == NULL || path[0] == '\0') {
        memset(diag, 0, sizeof(*diag));
        dqmc_update_green_scale_observers(D);
        return 0;
    }
    diag->enabled = 1;
    diag->failed = 0;
    strncpy(diag->file, path, sizeof diag->file - 1);
    diag->file[sizeof diag->file - 1] = '\0';
    diag->beta_index = beta_index;
    diag->Ltr = Ltr;
    diag->replica_id = replica_id;
    diag->seed = seed;
    dqmc_update_green_scale_observers(D);
    return 0;
}

static void dqmc_sweep_forward(Dqmc *D, int carry_prefix)
{
    if (D->status) {
        dqmc_invalidate_carried(D);
        return;
    }
    PROF_BEGIN(D->prof, t_sweep);
    const int n = D->n;
    const int L = D->L;
    int tau_left = 0;

    if (carry_prefix) {
        if (!D->use_ph || !D->stack_alt_u_allocated) {
            D->status = 1;
            dqmc_invalidate_carried(D);
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
        if (!D->carried_suffix_valid) {
            green_stack_build_suffix(&D->Gu, &D->stack_u);
            if (dqmc_stack_build_failed(D, &D->Gu)) {
                PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
                return;
            }
        }
    } else {
        green_stack_build(&D->Gu, &D->stack_u);
        if (dqmc_stack_build_failed(D, &D->Gu)) {
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
    }
    if (!D->use_ph) {
        green_stack_build(&D->Gd, &D->stack_d);
        if (dqmc_stack_build_failed(D, &D->Gd)) {
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
    }
    udv_identity(&D->left_u);
    if (carry_prefix) {
        green_stack_store(&D->stack_alt_u, 0, &D->left_u);
    }
    if (!D->use_ph) {
        udv_identity(&D->left_d);
    }

    for (int l = 0; l < L; l++) {
        for (int i = 0; i < n; i++) {
            const double Nu = green_flipN(&D->Gu, i);
            const double Nd =
                D->use_ph ? field_N(D->f, -1.0, D->f->s[l * n + i])
                          : green_flipN(&D->Gd, i);
            const double Ru = green_delay_ratio_N(&D->Gu, i, Nu);
            const double Rd = D->use_ph ? green_ph_down_ratio_N(&D->Gu, i, Nd)
                                        : green_delay_ratio_N(&D->Gd, i, Nd);
            const double R = Ru * Rd;

            D->accept_attempts++;
            if (rng_double(D->rng) < fabs(R)) {
                D->accept_accepted++;
                green_delay_accept(&D->Gu, i, Nu);
                if (!D->use_ph) {
                    green_delay_accept(&D->Gd, i, Nd);
                }
                D->f->s[l * n + i] *= -1;
                if (R < 0.0) {
                    D->sign = -D->sign;
                }
            }
        }

        green_wrap(&D->Gu);
        if (!D->use_ph) {
            green_wrap(&D->Gd);
        }
        if (((l + 1) % D->stab_interval) == 0 && (l + 1) < L) {
            const int tau = l + 1;
            const int j = tau / D->stab_interval;
            const int len = tau - tau_left;
            if (dqmc_record_stab_drift(D, tau) != 0) {
                D->status = 1;
                dqmc_invalidate_carried(D);
                PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
                return;
            }
            green_build_Bblock(&D->Gu, tau_left, len, D->Gu.B, D->Gu.Binv,
                               D->Gu.tmp);
            dqmc_udv_lmul(D, &D->left_u, D->Gu.B, &D->Gu.work);
            if (!D->use_ph) {
                green_build_Bblock(&D->Gd, tau_left, len, D->Gd.B,
                                   D->Gd.Binv, D->Gd.tmp);
                dqmc_udv_lmul(D, &D->left_d, D->Gd.B, &D->Gd.work);
            }
            tau_left = tau;
            if (carry_prefix) {
                green_stack_store(&D->stack_alt_u, j, &D->left_u);
            }
            if (dqmc_record_udv_pair_diags(
                    D, "u", "forward", tau, j, &D->left_u,
                    &D->stack_u.S[j], &D->left_u, &D->Gu.work) != 0) {
                D->status = 1;
                dqmc_invalidate_carried(D);
                PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
                return;
            }
            if (!D->use_ph &&
                dqmc_record_udv_pair_diags(
                    D, "d", "forward", tau, j, &D->left_d,
                    &D->stack_d.S[j], &D->left_d, &D->Gd.work) != 0) {
                D->status = 1;
                dqmc_invalidate_carried(D);
                PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
                return;
            }
            const int ru = green_from_stack(&D->Gu, &D->stack_u, &D->left_u,
                                            &D->combined_u, j);
            const int rd = D->use_ph
                               ? 0
                               : green_from_stack(&D->Gd, &D->stack_d,
                                                  &D->left_d, &D->combined_d,
                                                  j);
            if (ru != 0 || rd != 0 || D->Gu.det_sign == 0 ||
                (!D->use_ph && D->Gd.det_sign == 0)) {
                D->status = 1;
                dqmc_invalidate_carried(D);
                PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
                return;
            }
            /* Reset the running sign to the stabilized exact value, so wrap
               drift cannot accumulate spurious sign flips across the run. */
            if (D->use_ph) {
                dqmc_map_ph_down(D);
                D->sign = 1.0;
            } else {
                D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);
            }
        }
    }

    /* End-of-sweep rebuild: this always runs (even when Ltr < stab or
       Ltr % stab != 0), so the sign the measurement reads is always exact. */
    {
        const int len = L - tau_left;
        if (dqmc_record_stab_drift(D, 0) != 0) {
            D->status = 1;
            dqmc_invalidate_carried(D);
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
        if (len > 0) {
            green_build_Bblock(&D->Gu, tau_left, len, D->Gu.B, D->Gu.Binv,
                               D->Gu.tmp);
            dqmc_udv_lmul(D, &D->left_u, D->Gu.B, &D->Gu.work);
            if (!D->use_ph) {
                green_build_Bblock(&D->Gd, tau_left, len, D->Gd.B,
                                   D->Gd.Binv, D->Gd.tmp);
                dqmc_udv_lmul(D, &D->left_d, D->Gd.B, &D->Gd.work);
            }
        }
        if (carry_prefix) {
            green_stack_store(&D->stack_alt_u, D->stack_alt_u.M, &D->left_u);
        }
        if (dqmc_record_single_udv_diags(
                D, "u", "left_udv_forward", L, D->stack_u.M, &D->left_u,
                &D->Gu.work) != 0) {
            D->status = 1;
            dqmc_invalidate_carried(D);
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
        if (!D->use_ph &&
            dqmc_record_single_udv_diags(
                D, "d", "left_udv_forward", L, D->stack_u.M, &D->left_d,
                &D->Gd.work) != 0) {
            D->status = 1;
            dqmc_invalidate_carried(D);
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
        const int ru = green_from_left_udv(&D->Gu, &D->left_u);
        const int rd =
            D->use_ph ? 0 : green_from_left_udv(&D->Gd, &D->left_d);
        if (ru != 0 || rd != 0 || D->Gu.det_sign == 0 ||
            (!D->use_ph && D->Gd.det_sign == 0)) {
            D->status = 1;
            dqmc_invalidate_carried(D);
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
        if (D->use_ph) {
            dqmc_map_ph_down(D);
            D->sign = 1.0;
        } else {
            D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);
        }
    }
    if (carry_prefix) {
        D->carried_prefix_valid = 1;
        D->carried_suffix_valid = 0;
        D->sweep_dir = DQMC_DIR_BACKWARD;
    }
    D->sweep_count++;
    PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
}

static int dqmc_backward_boundary(Dqmc *D, int j)
{
    const int tau = D->stack_u.b[j];
    const int len = D->stack_u.b[j + 1] - D->stack_u.b[j];

    green_delay_flush(&D->Gu);
    if (dqmc_record_stab_drift(D, tau) != 0) {
        D->status = 1;
        dqmc_invalidate_carried(D);
        return 1;
    }

    green_build_Bblock(&D->Gu, tau, len, D->Gu.B, D->Gu.Binv, D->Gu.tmp);
    dqmc_udv_rmul(D, &D->left_u, D->Gu.B, &D->Gu.work);
    green_stack_store(&D->stack_u, j, &D->left_u);

    if (dqmc_record_udv_pair_diags(
            D, "u", "backward", tau, j, &D->stack_alt_u.S[j], &D->left_u,
            &D->left_u, &D->Gu.work) != 0) {
        D->status = 1;
        dqmc_invalidate_carried(D);
        return 1;
    }

    const int rc =
        green_from_boundary_factors(&D->Gu, &D->stack_alt_u.S[j],
                                    &D->left_u, &D->combined_u, tau);
    if (rc != 0 || D->Gu.det_sign == 0) {
        D->status = 1;
        dqmc_invalidate_carried(D);
        return 1;
    }
    dqmc_map_ph_down(D);
    D->sign = 1.0;
    return 0;
}

static void dqmc_sweep_backward(Dqmc *D)
{
    if (D->status) {
        dqmc_invalidate_carried(D);
        return;
    }
    PROF_BEGIN(D->prof, t_sweep);
    const int n = D->n;
    const int L = D->L;

    if (!D->use_ph || !D->stack_alt_u_allocated) {
        D->status = 1;
        dqmc_invalidate_carried(D);
        PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
        return;
    }
    if (!D->carried_prefix_valid) {
        green_stack_build_prefix(&D->Gu, &D->stack_alt_u);
        if (dqmc_stack_build_failed(D, &D->Gu)) {
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
    }

    udv_identity(&D->left_u);
    green_stack_store(&D->stack_u, D->stack_u.M, &D->left_u);
    green_wrap_backward(&D->Gu);

    for (int l = L - 1; l >= 0; l--) {
        for (int i = 0; i < n; i++) {
            const double Nu = green_flipN(&D->Gu, i);
            const double Nd = field_N(D->f, -1.0, D->f->s[l * n + i]);
            const double Ru = green_delay_ratio_N(&D->Gu, i, Nu);
            const double Rd = green_ph_down_ratio_N(&D->Gu, i, Nd);
            const double R = Ru * Rd;

            D->accept_attempts++;
            if (rng_double(D->rng) < fabs(R)) {
                D->accept_accepted++;
                green_delay_accept(&D->Gu, i, Nu);
                D->f->s[l * n + i] *= -1;
                if (R < 0.0) {
                    D->sign = -D->sign;
                }
            }
        }

        if (l == 0) {
            break;
        }
        if ((l % D->stab_interval) == 0) {
            const int j = l / D->stab_interval;
            if (dqmc_backward_boundary(D, j) != 0) {
                PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
                return;
            }
        }
        green_wrap_backward(&D->Gu);
    }

    {
        const int len = D->stack_u.b[1] - D->stack_u.b[0];
        green_delay_flush(&D->Gu);
        if (dqmc_record_stab_drift(D, 0) != 0) {
            D->status = 1;
            dqmc_invalidate_carried(D);
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
        if (len > 0) {
            green_build_Bblock(&D->Gu, 0, len, D->Gu.B, D->Gu.Binv,
                               D->Gu.tmp);
            dqmc_udv_rmul(D, &D->left_u, D->Gu.B, &D->Gu.work);
        }
        if (dqmc_record_single_udv_diags(
                D, "u", "left_udv_backward", 0, 0, &D->left_u,
                &D->Gu.work) != 0) {
            D->status = 1;
            dqmc_invalidate_carried(D);
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
        const int rc = green_from_left_udv(&D->Gu, &D->left_u);
        if (rc != 0 || D->Gu.det_sign == 0) {
            D->status = 1;
            dqmc_invalidate_carried(D);
            PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
            return;
        }
        dqmc_map_ph_down(D);
        D->sign = 1.0;
    }

    D->carried_prefix_valid = 0;
    D->carried_suffix_valid = 1;
    D->sweep_dir = DQMC_DIR_FORWARD;
    D->sweep_count++;
    PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
}

void dqmc_sweep(Dqmc *D)
{
    if (D->sweep_mode != DQMC_SWEEP_ALTERNATING) {
        dqmc_sweep_forward(D, 0);
        return;
    }
    if (D->sweep_dir == DQMC_DIR_BACKWARD) {
        dqmc_sweep_backward(D);
    } else {
        dqmc_sweep_forward(D, 1);
    }
}
