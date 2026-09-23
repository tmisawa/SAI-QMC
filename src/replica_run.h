#ifndef REPLICA_RUN_H
#define REPLICA_RUN_H

#include "io.h"
#include "lattice.h"
#include "profiler.h"
#include "replica.h"
#include "structure_factor.h"

int dqmc_run_replica(const Params *p, const Lattice *L, int beta_index,
                     int Ltr, double mu, int replica_id,
                     unsigned long long seed,
                     const StructureFactorPlan *szz_plan,
                     const StructureFactorPlan *sperp_plan, Profiler *prof,
                     ReplicaResult *result);

#endif
