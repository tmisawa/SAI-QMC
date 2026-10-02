#ifndef IO_H
#define IO_H

typedef struct {
    char lattice[16];
    char latfile[256];
    int Lx;
    int Ly;
    int pbc;
    double thop;
    double U;
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
    unsigned long long seed;
} Params;

int params_read(Params *p, const char *path);

#endif
