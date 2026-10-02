#ifndef TEMPERING_RUN_H
#define TEMPERING_RUN_H

#include <stdint.h>

#include "io.h"
#include "lattice.h"
#include "replica.h"
#include "structure_factor.h"

#define TEMPERING_MAX_SLOT 64

typedef struct {
    int nslot, nbin;
    int ladder_id;
    unsigned long long swap_seed;
    unsigned long long warm_attempts[TEMPERING_MAX_SLOT - 1], warm_accepted[TEMPERING_MAX_SLOT - 1];
    unsigned long long *bin_attempts;   /* [bin*(nslot-1)+k] */
    unsigned long long *bin_accepted;
    int *bin_walker_at;                 /* [bin*nslot+k], walker in slot k at bin end */
    unsigned long long *bin_n_up;       /* [bin*nslot+k] */
    unsigned long long *bin_n_down;
    unsigned long long round_trips[TEMPERING_MAX_SLOT]; /* completed within measurement, by walker */
    int final_walker_at[TEMPERING_MAX_SLOT];
    /* worker seconds of this ladder (a breakdown, not job wall or node-hours) */
    double sec_warmup;         /* warmup sweeps + global passes + exchanges */
    double sec_meas_sweep;     /* measurement sweeps + global passes */
    double sec_meas_exchange;  /* measurement exchanges */
    double sec_meas_measure;   /* measurement of observables */
    int failed;
} TemperingLadderOut;

int tempering_ladder_out_alloc(TemperingLadderOut *o, int nslot, int nbin);
void tempering_ladder_out_free(TemperingLadderOut *o);
/* Integers (seed, counts, walker ids, failed) travel losslessly as uint64_t;
   the four seconds travel as double. Gather with MPI_UINT64_T and MPI_DOUBLE. */
int tempering_ladder_out_width_u64(int nslot, int nbin);
int tempering_ladder_out_width_f64(void);                     /* 4 */
void tempering_ladder_out_pack(const TemperingLadderOut *o, uint64_t *ibuf, double *fbuf);
int tempering_ladder_out_unpack(const uint64_t *ibuf, const double *fbuf,
                                int nslot, int nbin, TemperingLadderOut *o);

/* Runs one ladder of p->nbeta slots with Ltr = p->tempering_ltr and
   dtau_k = beta_list[k] / Ltr. results[k] receives slot k exactly as
   dqmc_run_replica fills a replica. Returns nonzero on any failure. */
int dqmc_run_ladder(const Params *p, const Lattice *L, double mu, int ladder_id,
                    const StructureFactorPlan *szz_plan,
                    const StructureFactorPlan *sperp_plan,
                    ReplicaResult *results, TemperingLadderOut *out);

#endif
