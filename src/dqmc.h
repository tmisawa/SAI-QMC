#ifndef DQMC_H
#define DQMC_H

#include "field.h"
#include "green.h"
#include "model.h"
#include "profiler.h"
#include "rng.h"

typedef struct {
    int enabled;
    int allocated;
    int failed;
    unsigned long long samples;
    unsigned long long max_sweep;
    int max_tau;
    double max_inf;
    double sum_inf;
    Green Gu_ref;
    Green Gd_ref;
} DqmcStabDrift;

typedef struct {
    int enabled;
    int failed;
    char file[256];
    int beta_index;
    int Ltr;
    int replica_id;
    unsigned long long seed;
    double U;
    double dtau;
} DqmcUdvScaleDiag;

typedef struct {
    int enabled;
    int failed;
    char file[256];
    int beta_index;
    int Ltr;
    int replica_id;
    unsigned long long seed;
} DqmcUdvCenteredDiag;

typedef enum {
    DQMC_SWEEP_FORWARD = 0,
    DQMC_SWEEP_ALTERNATING = 1
} DqmcSweepMode;

typedef enum {
    DQMC_DIR_FORWARD = 0,
    DQMC_DIR_BACKWARD = 1
} DqmcSweepDir;

typedef struct {
    int n;
    int L;
    Model *m;
    Field *f;
    Green Gu;
    Green Gd;
    GreenStack stack_u;
    GreenStack stack_d;
    GreenStack stack_alt_u;
    int stack_alt_u_allocated;
    UDV left_u;
    UDV left_d;
    UDV combined_u;
    UDV combined_d;
    Rng *rng;
    int stab_interval;
    int use_ph;
    DqmcSweepMode sweep_mode;
    DqmcSweepDir sweep_dir;
    GreenRebuildMode green_rebuild_mode;
    int carried_prefix_valid;
    int carried_suffix_valid;
    double sign;
    /* 0 = healthy, nonzero = numerical breakdown in a stabilized rebuild.
       Once set, dqmc_sweep is a no-op and callers must fail-fast. */
    int status;
    Profiler *prof;
    unsigned long long sweep_count;
    unsigned long long accept_attempts;
    unsigned long long accept_accepted;
    unsigned long long global_attempts;
    unsigned long long global_accepted;

    DqmcStabDrift stab_drift;
    DqmcUdvScaleDiag udv_scale_diag;
    DqmcUdvCenteredDiag udv_centered_diag;
} Dqmc;

int dqmc_init_mode(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval,
                   Profiler *prof, DqmcSweepMode mode);
int dqmc_init_modes(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval,
                    Profiler *prof, DqmcSweepMode sweep_mode,
                    GreenRebuildMode rebuild_mode);
void dqmc_init(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval,
               Profiler *prof);
void dqmc_free(Dqmc *D);
int dqmc_enable_stab_drift(Dqmc *D, int enabled);
int dqmc_enable_udv_scale_diag(Dqmc *D, const char *path, int beta_index,
                               int Ltr, int replica_id,
                               unsigned long long seed, double U,
                               double dtau);
int dqmc_enable_udv_centered_diag(Dqmc *D, const char *path, int beta_index,
                                  int Ltr, int replica_id,
                                  unsigned long long seed);
void dqmc_set_green_rebuild_mode(Dqmc *D, GreenRebuildMode mode);
void dqmc_sweep(Dqmc *D);

/* log|W| and sign(W) of the current field, W = det(1+A_up) det(1+A_down).
   Uses the PH identity when D->use_ph. Does not modify the Green functions. */
int dqmc_log_weight(Dqmc *D, double *logw, int *sign);
/* One proposal for site i: flip its world line, accept iff delta >= 0 or
   u < exp(delta), otherwise restore the field. u must be a uniform number in
   [0,1); *logw and *sign hold the current weight and are updated on
   acceptance. Counts the attempt. On numerical failure the field is restored,
   D->status is set and nonzero is returned (a failure is never a rejection).
   Does not rebuild the Green functions: only dqmc_global_site_pass does. */
int dqmc_global_site_step(Dqmc *D, int i, double u, double *logw, int *sign,
                          int *accepted);
/* One fixed-order pass of site world-line flips with exact Metropolis
   acceptance (spec 3.3). Leaves Gu/Gd/sign rebuilt at l=0 and carried stacks
   invalid. Returns nonzero and sets D->status on numerical failure. */
int dqmc_global_site_pass(Dqmc *D);

#endif
