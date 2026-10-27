#ifndef IO_H
#define IO_H

#include <stddef.h>

#include "lattice.h"

typedef struct {
    char lattice[16];
    char latfile[256];
    int Lx;
    int Ly;
    int pbc;
    int pbc_given;                 /* 1 when pbc= or bc= appears */
    LatBoundary bc_x;              /* resolved boundary of each built-in direction */
    LatBoundary bc_y;
    int bc_x_given;
    int bc_y_given;
    double thop;
    double tprime;                 /* lattice=square next-nearest hopping element; 0 when omitted */
    int tprime_given;
    double U;
    double mu;                     /* resolved chemical potential; U/2 when omitted */
    int mu_given;
    double dtau;
    double beta_list[64];
    int nbeta;
    int nwarm;
    int nmeas;
    int nbin;
    int stab_interval;
    char output_file[256];
    int profile;
    char profile_file[256];
    char stab_drift_file[256];
    char udv_scale_file[256];
    char udv_centered_file[256];
    char sweep_order[16];
    char green_rebuild[16];
    char parallel[16];
    int nrep;
    char replica_log[256];
    char global_update[16];
    int global_interval;
    char replica_bin_file[256];
    int conditional_measure;       /* 0 default; 1 appends comparison bin sums */
    char global_site_diag_file[256];
    char global_site_select[16];   /* fixed | polarized (spec 4) */
    double global_site_power;      /* alpha >= 0, finite; default 2 */

    char tempering[16];            /* none | dtau_ladder */
    int tempering_ltr;             /* fixed time slices of every slot */
    int tempering_interval;        /* sweeps between exchange rounds */
    char tempering_file[256];
    int dtau_given;                /* 1 when the input sets dtau */

    char field_init[16];           /* initial HS field: random (default) | uniform (all +1) */

    char szz_q[256];
    char szz_file[256];
    char sperp_q[256];
    char sperp_file[256];
    char spin_consistency_file[256];
    char sign_file[256];           /* none (default) | per-beta sign statistics table */
    int sign_file_given;           /* 1 when sign_file is explicit, including none */
    unsigned long long seed;
} Params;

int params_read(Params *p, const char *path);
/* 1 when the legacy pbc key describes the boundary (always for lattice=file). */
int params_boundary_is_legacy(const Params *p);
/* Header token: "pbc=0"/"pbc=1" when legacy, else "bc_x=<name>" (chain) or
   "bc_x=<name> bc_y=<name>" (square). Returns 1 when out is too small. */
int params_boundary_label(const Params *p, char *out, size_t size);
/* 1 when mu is omitted or equals U/2 exactly (no tolerance): the half-filling
   candidate that becomes particle-hole symmetric on a bipartite lattice. */
int params_mu_is_half(const Params *p);
/* 1 when sign_file names an output. */
int params_sign_output_enabled(const Params *p);
/* 1 when mu, tprime or sign_file is given (including sign_file=none). Such
   inputs use the exact bipartite test for hopping files and extended schema. */
int params_new_keys(const Params *p);
/* 1 when the extended output schema applies: params_new_keys, or a model
   without particle-hole symmetry. Inputs without any of these keep the
   legacy headers and columns. */
int params_extended_schema(const Params *p, int ph_symmetric);

#endif
