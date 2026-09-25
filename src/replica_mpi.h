#ifndef REPLICA_MPI_H
#define REPLICA_MPI_H

#include "replica.h"

#define REPLICA_MPI_BIN_DOUBLES 10

void replica_mpi_rank_range(int nrep, int nranks, int rank, int *first_replica,
                            int *local_nrep);
int replica_mpi_rank_for_replica(int nrep, int nranks, int replica_id);
int replica_mpi_gatherv_layout(int nrep, int nbin, int nranks,
                               int item_width, int *recvcounts, int *displs);
int replica_mpi_local_failed(int run_failed, const ReplicaResult *result);
void replica_mpi_pack_bins(const ReplicaResult *results, int local_nrep,
                           int nbin, double *values, int *counts);
void replica_mpi_unpack_bins(const double *values, const int *counts,
                             int total_bins, ReplicaBin *bins);
int replica_mpi_pack_szz(const ReplicaResult *results, int local_nrep,
                         int nbin, int nq, double *values);
int replica_mpi_pack_sperp(const ReplicaResult *results, int local_nrep,
                           int nbin, int nq, double *values);

#endif
