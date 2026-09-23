#define _POSIX_C_SOURCE 199309L
#include "profiler.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static _Thread_local Profiler *g_current_profiler = NULL;

static const char *phase_name(ProfPhase phase)
{
    static const char *names[PROF_PHASE_COUNT] = {
        "setup", "warmup", "measurement", "finalize", "all"};
    return (phase >= 0 && phase < PROF_PHASE_COUNT) ? names[phase]
                                                    : "unknown";
}

static const char *region_name(ProfRegion region)
{
    static const char *names[PROF_REGION_COUNT] = {
        "beta_total",        "model_init", "field_init",
        "dqmc_init",         "dqmc_sweep", "green_from_scratch",
        "green_wrap",        "green_update", "measure_sample",
        "measure_szz",       "measure_sperp", "measure_spin",
        "jackknife",         "udv_lmul", "udv_rmul", "udv_combine",
        "udv_inv_one_plus",
        "green_stack_build", "green_from_stack",
        "la_gemm",           "la_inverse", "la_expm_sym"};
    return (region >= 0 && region < PROF_REGION_COUNT) ? names[region]
                                                       : "unknown";
}

static void clear_stats(Profiler *p)
{
    memset(p->stat, 0, sizeof(p->stat));
}

static void init_common(Profiler *p, int enabled)
{
    memset(p, 0, sizeof(*p));
    p->enabled = enabled ? 1 : 0;
    p->phase = PROF_PHASE_SETUP;
    p->nrep = 1;
    p->nranks = 1;
    strcpy(p->parallel, "serial");
}

double profiler_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

void profiler_init(Profiler *p, int enabled, const char *path)
{
    init_common(p, enabled);
    if (!p->enabled) {
        return;
    }

    const char *out = (path != NULL && path[0] != '\0') ? path : "profile.dat";
    p->fp = fopen(out, "w");
    if (p->fp == NULL) {
        p->error = 1;
        return;
    }
#ifdef AFQMC_USE_MPI
    fprintf(p->fp,
            "# beta T dtau Ltr phase region calls total_sec avg_sec frac_beta wall_sec thread_total_sec nrep parallel nranks\n");
#else
    fprintf(p->fp,
            "# beta T dtau Ltr phase region calls total_sec avg_sec frac_beta wall_sec thread_total_sec nrep parallel\n");
#endif
    if (ferror(p->fp)) {
        p->error = 1;
    }
}

void profiler_init_memory(Profiler *p, int enabled)
{
    init_common(p, enabled);
}

void profiler_close(Profiler *p)
{
    if (p == NULL || !p->enabled || p->fp == NULL) {
        return;
    }
    if (ferror(p->fp)) {
        p->error = 1;
    }
    if (fclose(p->fp) != 0) {
        p->error = 1;
    }
    p->fp = NULL;
}

int profiler_error(const Profiler *p)
{
    return (p != NULL) ? p->error : 0;
}

void profiler_beta_begin(Profiler *p, double beta, double T, double dtau,
                         int Ltr)
{
    if (p == NULL || !p->enabled) {
        return;
    }
    clear_stats(p);
    p->beta = beta;
    p->T = T;
    p->dtau = dtau;
    p->Ltr = Ltr;
    p->phase = PROF_PHASE_SETUP;
    p->beta_start_sec = profiler_now();
}

void profiler_phase_set(Profiler *p, ProfPhase phase)
{
    if (p == NULL || !p->enabled) {
        return;
    }
    if (phase >= 0 && phase < PROF_PHASE_COUNT) {
        p->phase = phase;
    }
}

void profiler_set_metadata(Profiler *p, int nrep, const char *parallel)
{
    if (p == NULL) {
        return;
    }
    p->nrep = (nrep > 0) ? nrep : 1;
    if (parallel != NULL && parallel[0] != '\0') {
        strncpy(p->parallel, parallel, sizeof p->parallel - 1);
        p->parallel[sizeof p->parallel - 1] = '\0';
    }
}

void profiler_set_mpi_metadata(Profiler *p, int nrep, const char *parallel,
                               int nranks)
{
    profiler_set_metadata(p, nrep, parallel);
    if (p != NULL) {
        p->nranks = (nranks > 0) ? nranks : 1;
    }
}

void profiler_merge(Profiler *dst, const Profiler *src)
{
    if (dst == NULL || src == NULL || !dst->enabled || !src->enabled) {
        return;
    }
    for (int ph = 0; ph < PROF_PHASE_COUNT; ph++) {
        for (int rg = 0; rg < PROF_REGION_COUNT; rg++) {
            dst->stat[ph][rg].calls += src->stat[ph][rg].calls;
            dst->stat[ph][rg].total_sec += src->stat[ph][rg].total_sec;
        }
    }
}

void profiler_add(Profiler *p, ProfRegion region, double elapsed_sec)
{
    if (p == NULL || !p->enabled || region < 0 || region >= PROF_REGION_COUNT) {
        return;
    }
    if (elapsed_sec < 0.0) {
        return;
    }

    ProfPhase phase = p->phase;
    if (phase < 0 || phase >= PROF_PHASE_COUNT) {
        phase = PROF_PHASE_SETUP;
    }
    p->stat[phase][region].calls++;
    p->stat[phase][region].total_sec += elapsed_sec;

    if (phase != PROF_PHASE_ALL) {
        p->stat[PROF_PHASE_ALL][region].calls++;
        p->stat[PROF_PHASE_ALL][region].total_sec += elapsed_sec;
    }
}

void profiler_beta_end(Profiler *p)
{
    if (p == NULL || !p->enabled || p->fp == NULL) {
        return;
    }

    const double beta_total = profiler_now() - p->beta_start_sec;
    p->stat[PROF_PHASE_ALL][PROF_BETA_TOTAL].calls = 1;
    p->stat[PROF_PHASE_ALL][PROF_BETA_TOTAL].total_sec = beta_total;

    for (int ph = 0; ph < PROF_PHASE_COUNT; ph++) {
        for (int rg = 0; rg < PROF_REGION_COUNT; rg++) {
            const ProfStat s = p->stat[ph][rg];
            if (s.calls == 0) {
                continue;
            }
            const double avg = s.total_sec / (double)s.calls;
            const double frac =
                (beta_total > 0.0) ? s.total_sec / beta_total : 0.0;
            const double thread_total = s.total_sec;
            const double wall_sec =
                (rg == PROF_BETA_TOTAL) ? beta_total : 0.0;
#ifdef AFQMC_USE_MPI
            fprintf(p->fp,
                    "%.17g %.17g %.17g %d %s %s %llu %.17g %.17g %.17g %.17g %.17g %d %s %d\n",
                    p->beta, p->T, p->dtau, p->Ltr,
                    phase_name((ProfPhase)ph), region_name((ProfRegion)rg),
                    s.calls, thread_total, avg, frac, wall_sec, thread_total,
                    p->nrep, p->parallel, p->nranks);
#else
            fprintf(p->fp,
                    "%.17g %.17g %.17g %d %s %s %llu %.17g %.17g %.17g %.17g %.17g %d %s\n",
                    p->beta, p->T, p->dtau, p->Ltr,
                    phase_name((ProfPhase)ph), region_name((ProfRegion)rg),
                    s.calls, thread_total, avg, frac, wall_sec, thread_total,
                    p->nrep, p->parallel);
#endif
        }
    }
    fflush(p->fp);
    if (ferror(p->fp)) {
        p->error = 1;
    }
}

void profiler_set_current(Profiler *p)
{
    g_current_profiler = p;
}

Profiler *profiler_current(void)
{
    return g_current_profiler;
}
