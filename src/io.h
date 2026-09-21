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

    char szz_q[256];
    char szz_file[256];
    char sperp_q[256];
    char sperp_file[256];
    char spin_consistency_file[256];
    unsigned long long seed;
} Params;

int params_read(Params *p, const char *path);

#endif
