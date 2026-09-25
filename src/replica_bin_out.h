#ifndef REPLICA_BIN_OUT_H
#define REPLICA_BIN_OUT_H

#include <stdio.h>
#include "replica.h"

typedef struct {
    int nrep, nbin, szz_nq, sperp_nq;
    const ReplicaBin *bins;        /* [replica * nbin + bin] */
    const double *szz;             /* [(replica * nbin + bin) * szz_nq + q], NULL if unmeasured */
    const double *sperp;           /* [(replica * nbin + bin) * sperp_nq + q], NULL if unmeasured */
    const int *replica_ids;        /* [replica] */
    const unsigned long long *seeds;
} ReplicaBinView;

typedef struct {
    int beta_index, Ltr, nsite;
    double beta_requested, beta_effective, U, dtau;
    int nwarm, nmeas, nbin;
    const char *lattice;
    int Lx, Ly, pbc;
    const char *global_update;
    int global_interval;
    int szz_Q_index, szz_0_index, sperp_Q_index; /* -1: not measured */
} ReplicaBinMeta;

/* Writes the header comment (only when write_header != 0) and one row per
   replica and bin. Returns nonzero on any I/O error. */
int replica_bin_write(FILE *fp, const ReplicaBinView *view,
                      const ReplicaBinMeta *meta, int write_header);

#endif
