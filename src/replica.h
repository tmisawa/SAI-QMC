#ifndef REPLICA_H
#define REPLICA_H

#include "measure.h"

typedef struct {
    double sum_sign_Ehub;
    double sum_sign_Egc;
    double sum_sign_Eph;
    double sum_sign_N;
    double sum_sign_D;
    double sum_sign;
    unsigned long long accept_accepted;
    unsigned long long accept_attempts;
    int count;
} ReplicaBin;

typedef struct {
    double *sum_sign_values;
    int nq;
} ReplicaQObservable;

typedef struct {
    ReplicaBin *bins;
    int nbin;
    ReplicaQObservable szz;
    ReplicaQObservable sperp;
    int replica_id;
    unsigned long long seed;
    int status;
} ReplicaResult;

unsigned long long replica_seed(unsigned long long base_seed, int beta_index,
                                int replica_id);
int replica_check_seed_unique(const unsigned long long *seeds, int nseed);
int replica_bin_add(ReplicaBin *bin, const MeasSample *s, double mu, double U,
                    int nsite, double sign);
void replica_bin_add_acceptance(ReplicaBin *bin, unsigned long long accepted,
                                unsigned long long attempts);
double replica_bin_acceptance(const ReplicaBin *bin);
int replica_bin_values(const ReplicaBin *bin, double *Ehub, double *Egc,
                       double *Eph, double *N, double *D, double *sign);
int replica_result_alloc(ReplicaResult *result, int nbin);
int replica_result_enable_szz(ReplicaResult *result, int nq);
int replica_result_enable_sperp(ReplicaResult *result, int nq);
int replica_result_add_szz(ReplicaResult *result, int bin, const double *szz,
                           int nq, double sign);
int replica_result_add_sperp(ReplicaResult *result, int bin,
                             const double *sperp, int nq, double sign);
int replica_result_add_sample(ReplicaResult *result, int bin,
                              const MeasSample *sample, double mu, double U,
                              int nsite, double sign, const double *szz,
                              int szz_nq, const double *sperp,
                              int sperp_nq);
int replica_result_szz_bin_values(const ReplicaResult *result, int bin,
                                  double *out, int nq);
int replica_result_sperp_bin_values(const ReplicaResult *result, int bin,
                                    double *out, int nq);
void replica_result_free(ReplicaResult *result);

#endif
