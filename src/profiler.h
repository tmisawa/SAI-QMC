#ifndef PROFILER_H
#define PROFILER_H

#include <stdio.h>

typedef enum {
    PROF_PHASE_SETUP = 0,
    PROF_PHASE_WARMUP,
    PROF_PHASE_MEASUREMENT,
    PROF_PHASE_FINALIZE,
    PROF_PHASE_ALL,
    PROF_PHASE_COUNT
} ProfPhase;

typedef enum {
    PROF_BETA_TOTAL = 0,
    PROF_MODEL_INIT,
    PROF_FIELD_INIT,
    PROF_DQMC_INIT,
    PROF_DQMC_SWEEP,
    PROF_GREEN_FROM_SCRATCH,
    PROF_GREEN_WRAP,
    PROF_GREEN_UPDATE,
    PROF_MEASURE_SAMPLE,
    PROF_MEASURE_SZZ,
    PROF_MEASURE_SPERP,
    PROF_MEASURE_SPIN,
    PROF_JACKKNIFE,
    PROF_UDV_LMUL,
    PROF_UDV_RMUL,
    PROF_UDV_COMBINE,
    PROF_UDV_INV_ONE_PLUS,
    PROF_GREEN_STACK_BUILD,
    PROF_GREEN_FROM_STACK,
    PROF_LA_GEMM,
    PROF_LA_INVERSE,
    PROF_LA_EXPM_SYM,
    PROF_REGION_COUNT
} ProfRegion;

typedef struct {
    unsigned long long calls;
    double total_sec;
} ProfStat;

typedef struct {
    int enabled;
    int error;
    FILE *fp;
    double beta;
    double T;
    double dtau;
    int Ltr;
    double beta_start_sec;
    int nrep;
    int nranks;
    char parallel[16];
    ProfPhase phase;
    ProfStat stat[PROF_PHASE_COUNT][PROF_REGION_COUNT];
} Profiler;

void profiler_init(Profiler *p, int enabled, const char *path);
void profiler_init_memory(Profiler *p, int enabled);
void profiler_close(Profiler *p);
int profiler_error(const Profiler *p);
void profiler_beta_begin(Profiler *p, double beta, double T, double dtau,
                         int Ltr);
void profiler_beta_end(Profiler *p);
void profiler_phase_set(Profiler *p, ProfPhase phase);
void profiler_set_metadata(Profiler *p, int nrep, const char *parallel);
void profiler_set_mpi_metadata(Profiler *p, int nrep, const char *parallel,
                               int nranks);
void profiler_merge(Profiler *dst, const Profiler *src);
double profiler_now(void);
void profiler_add(Profiler *p, ProfRegion region, double elapsed_sec);
void profiler_set_current(Profiler *p);
Profiler *profiler_current(void);

static inline double profiler_now_if_enabled(const Profiler *p)
{
    return (p != NULL && p->enabled) ? profiler_now() : 0.0;
}

static inline void profiler_add_elapsed(Profiler *p, ProfRegion region,
                                        double start_sec)
{
    if (p == NULL || !p->enabled) {
        return;
    }
    profiler_add(p, region, profiler_now() - start_sec);
}

#define PROF_BEGIN(prof, var) double var = profiler_now_if_enabled((prof))
#define PROF_END(prof, region, var) profiler_add_elapsed((prof), (region), (var))

#endif
