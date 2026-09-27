#ifndef REPLICA_CHAIN_H
#define REPLICA_CHAIN_H

#include "dqmc.h"
#include "field.h"
#include "io.h"
#include "lattice.h"
#include "model.h"
#include "profiler.h"
#include "replica.h"
#include "rng.h"
#include "structure_factor.h"

/* One Markov chain of dqmc_run_replica: model, RNG, field and Dqmc state
   plus the measurement workspaces. D keeps pointers to m, f and r, so a
   ReplicaChain must not be copied or moved after replica_chain_init. */
typedef struct {
    const Params *p;
    const Lattice *lat;
    int beta_index, Ltr, replica_id;
    unsigned long long seed;
    double dtau, beta, T;
    Profiler *prof;
    Model m;
    Rng r;
    Field f;
    Dqmc D;
    int have_model, have_field, have_dqmc;
    int use_global;
    unsigned long long global_sweep;   /* warmup + measurement sweeps */
    int measuring;                     /* set by replica_chain_measure_begin */
    const StructureFactorPlan *szz_plan, *sperp_plan;
    StructureFactorWorkspace szz_work, sperp_work;
    double *szz_sample, *sperp_sample;
#ifdef AFQMC_TEST_HOOKS
    unsigned long long fail_at;        /* AFQMC_TEST_GLOBAL_FAIL_AT test hook */
#endif
} ReplicaChain;

/* Setup phase of dqmc_run_replica: model, RNG, field, Dqmc, optional
   diagnostics and site selection. Prints the same stderr lines on failure. */
int replica_chain_init(ReplicaChain *c, const Params *p, const Lattice *L,
                       int beta_index, int Ltr, double dtau, int replica_id,
                       unsigned long long seed, Profiler *prof);
/* One sweep, then the global pass when due. Returns D.status. */
int replica_chain_step(ReplicaChain *c);
/* The part of replica_chain_step after dqmc_sweep: counts the sweep and runs
   the global pass when due (with the measurement-phase test hook). Returns
   D.status. */
int replica_chain_global_after_sweep(ReplicaChain *c);
/* Allocates the result and the measurement workspaces (same messages). */
int replica_chain_measure_begin(ReplicaChain *c, const StructureFactorPlan *szz_plan,
                                const StructureFactorPlan *sperp_plan,
                                ReplicaResult *result);
/* Failure checks, one sample, and the acceptance counters of bin bi.
   attempts0/accepted0/gatt0/gacc0 are the counters before this sweep. */
int replica_chain_measure(ReplicaChain *c, ReplicaResult *result, int bi, int k,
                          double mu, unsigned long long attempts0,
                          unsigned long long accepted0,
                          unsigned long long gatt0, unsigned long long gacc0);
/* 1 when the sign and both Green functions are finite. */
int replica_chain_state_is_finite(const ReplicaChain *c);
/* First non-NONE linalg failure reason of the up/down Green workspaces. */
int replica_chain_failure_reason(const ReplicaChain *c);
void replica_chain_free(ReplicaChain *c);

#endif
