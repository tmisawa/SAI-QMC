#ifndef GLOBAL_SITE_DIAG_OUT_H
#define GLOBAL_SITE_DIAG_OUT_H

#include <stdio.h>
#include "global_select.h"

typedef struct {
    int beta_index, Ltr, nsite;
    double beta_requested, U, dtau, lambda;
    int nwarm, nmeas, nbin;
    const char *lattice;
    int Lx, Ly;
    char boundary[64];  /* params_boundary_label */
    int global_interval;
    const char *global_site_select;
} GlobalSiteDiagMeta;

int global_site_diag_write(FILE *fp, const GlobalSiteDiag *diags, int nrep,
                           const int *replica_ids,
                           const unsigned long long *seeds,
                           const GlobalSiteDiagMeta *meta, int write_header);

#endif
