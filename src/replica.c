#include "replica.h"
#include "structure_factor.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t splitmix64_value(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

unsigned long long replica_seed(unsigned long long base_seed, int beta_index,
                                int replica_id)
{
    const unsigned long long legacy =
        base_seed + 1000ULL * (unsigned long long)beta_index;
    if (replica_id == 0) {
        return legacy;
    }

    uint64_t x = (uint64_t)base_seed;
    x ^= 0xD1B54A32D192ED03ULL * (uint64_t)(unsigned int)(beta_index + 1);
    x ^= 0xABC98388FB8FAC03ULL * (uint64_t)(unsigned int)(replica_id + 1);
    uint64_t mixed = splitmix64_value(x);
    if (mixed == legacy) {
        mixed = splitmix64_value(mixed);
    }
    return (unsigned long long)mixed;
}

int replica_check_seed_unique(const unsigned long long *seeds, int nseed)
{
    for (int i = 0; i < nseed; i++) {
        for (int j = i + 1; j < nseed; j++) {
            if (seeds[i] == seeds[j]) {
                return 1;
            }
        }
    }
    return 0;
}

int replica_bin_add(ReplicaBin *bin, const MeasSample *s, double mu, double U,
                    int nsite, double sign)
{
    if (bin == NULL || nsite <= 0 || !measure_sample_is_finite(s) ||
        !isfinite(mu) || !isfinite(U) || !isfinite(sign)) {
        return 1;
    }

    const double e_hub = s->E;
    const double e_gc = s->E - mu * s->ntot;
    const double e_ph = s->E - 0.5 * U * s->ntot + 0.25 * U * (double)nsite;
    if (!isfinite(e_hub) || !isfinite(e_gc) || !isfinite(e_ph)) {
        return 1;
    }

    bin->sum_sign_Ehub += sign * e_hub;
    bin->sum_sign_Egc += sign * e_gc;
    bin->sum_sign_Eph += sign * e_ph;
    bin->sum_sign_N += sign * s->ntot;
    bin->sum_sign_D += sign * s->doublon;
    bin->sum_sign += sign;
    bin->count++;
    return 0;
}

void replica_bin_add_acceptance(ReplicaBin *bin, unsigned long long accepted,
                                unsigned long long attempts)
{
    bin->accept_accepted += accepted;
    bin->accept_attempts += attempts;
}

double replica_bin_acceptance(const ReplicaBin *bin)
{
    if (bin->accept_attempts == 0) {
        return 0.0;
    }
    return (double)bin->accept_accepted / (double)bin->accept_attempts;
}

int replica_bin_values(const ReplicaBin *bin, double *Ehub, double *Egc,
                       double *Eph, double *N, double *D, double *sign)
{
    if (bin == NULL || Ehub == NULL || Egc == NULL || Eph == NULL ||
        N == NULL || D == NULL || sign == NULL) {
        return 1;
    }
    if (bin->count <= 0 || !isfinite(bin->sum_sign) ||
        fabs(bin->sum_sign) == 0.0 || !isfinite(bin->sum_sign_Ehub) ||
        !isfinite(bin->sum_sign_Egc) || !isfinite(bin->sum_sign_Eph) ||
        !isfinite(bin->sum_sign_N) || !isfinite(bin->sum_sign_D)) {
        return 1;
    }

    const double ehub = bin->sum_sign_Ehub / bin->sum_sign;
    const double egc = bin->sum_sign_Egc / bin->sum_sign;
    const double eph = bin->sum_sign_Eph / bin->sum_sign;
    const double ntot = bin->sum_sign_N / bin->sum_sign;
    const double doublon = bin->sum_sign_D / bin->sum_sign;
    const double avg_sign = bin->sum_sign / (double)bin->count;
    if (!isfinite(ehub) || !isfinite(egc) || !isfinite(eph) ||
        !isfinite(ntot) || !isfinite(doublon) || !isfinite(avg_sign)) {
        return 1;
    }

    *Ehub = ehub;
    *Egc = egc;
    *Eph = eph;
    *N = ntot;
    *D = doublon;
    *sign = avg_sign;
    return 0;
}

int replica_result_alloc(ReplicaResult *result, int nbin)
{
    if (result == NULL || nbin <= 0) {
        return 1;
    }
    memset(result, 0, sizeof(*result));
    result->bins = calloc((size_t)nbin, sizeof(ReplicaBin));
    if (result->bins == NULL) {
        return 1;
    }
    result->nbin = nbin;
    return 0;
}

static int q_observable_state_is_valid(const ReplicaQObservable *observable)
{
    return observable != NULL &&
           ((observable->nq == 0 && observable->sum_sign_values == NULL) ||
            (observable->nq > 0 && observable->sum_sign_values != NULL));
}

static int q_observable_enable(ReplicaResult *result,
                               ReplicaQObservable *observable, int nq)
{
    if (result == NULL || observable == NULL || result->bins == NULL ||
        result->nbin <= 0 || nq <= 0 ||
        !q_observable_state_is_valid(observable) || observable->nq != 0) {
        return 1;
    }
    if ((size_t)result->nbin > SIZE_MAX / (size_t)nq ||
        (size_t)result->nbin * (size_t)nq > SIZE_MAX / sizeof(double)) {
        return 1;
    }
    observable->sum_sign_values =
        calloc((size_t)result->nbin * (size_t)nq, sizeof(double));
    if (observable->sum_sign_values == NULL) {
        return 1;
    }
    observable->nq = nq;
    return 0;
}

int replica_result_enable_szz(ReplicaResult *result, int nq)
{
    return result == NULL ? 1 : q_observable_enable(result, &result->szz, nq);
}

int replica_result_enable_sperp(ReplicaResult *result, int nq)
{
    return result == NULL ? 1
                          : q_observable_enable(result, &result->sperp, nq);
}

static int q_add_is_valid(const ReplicaResult *result,
                          const ReplicaQObservable *observable, int bin,
                          const double *values, int nq, double sign)
{
    if (result == NULL || observable == NULL ||
        !q_observable_state_is_valid(observable) ||
        observable->sum_sign_values == NULL || values == NULL || bin < 0 ||
        bin >= result->nbin || nq <= 0 || nq != observable->nq ||
        !isfinite(sign)) {
        return 1;
    }
    for (int q = 0; q < nq; q++) {
        if (!isfinite(values[q]) || !isfinite(sign * values[q]) ||
            !isfinite(observable->sum_sign_values[q + nq * bin] +
                      sign * values[q])) {
            return 1;
        }
    }
    return 0;
}

static void q_add(ReplicaQObservable *observable, int bin,
                  const double *values, double sign)
{
    for (int q = 0; q < observable->nq; q++) {
        observable->sum_sign_values[q + observable->nq * bin] +=
            sign * values[q];
    }
}

int replica_result_add_szz(ReplicaResult *result, int bin, const double *szz,
                           int nq, double sign)
{
    if (result == NULL ||
        q_add_is_valid(result, &result->szz, bin, szz, nq, sign) != 0) {
        return 1;
    }
    q_add(&result->szz, bin, szz, sign);
    return 0;
}

int replica_result_add_sperp(ReplicaResult *result, int bin,
                             const double *sperp, int nq, double sign)
{
    if (result == NULL ||
        q_add_is_valid(result, &result->sperp, bin, sperp, nq, sign) != 0) {
        return 1;
    }
    q_add(&result->sperp, bin, sperp, sign);
    return 0;
}

static int q_sample_is_valid(const ReplicaResult *result,
                             const ReplicaQObservable *observable, int bin,
                             const double *values, int nq, double sign)
{
    if (!q_observable_state_is_valid(observable)) {
        return 1;
    }
    if (observable->nq == 0) {
        return values != NULL || nq != 0;
    }
    return q_add_is_valid(result, observable, bin, values, nq, sign);
}

int replica_result_add_sample(ReplicaResult *result, int bin,
                              const MeasSample *sample, double mu, double U,
                              int nsite, double sign, const double *szz,
                              int szz_nq, const double *sperp,
                              int sperp_nq)
{
    if (result == NULL || result->bins == NULL || bin < 0 ||
        bin >= result->nbin) {
        return 1;
    }
    ReplicaBin next = result->bins[bin];
    if (replica_bin_add(&next, sample, mu, U, nsite, sign) != 0) {
        return 1;
    }
    if (q_sample_is_valid(result, &result->szz, bin, szz, szz_nq, sign) !=
            0 ||
        q_sample_is_valid(result, &result->sperp, bin, sperp, sperp_nq,
                          sign) != 0) {
        return 1;
    }

    result->bins[bin] = next;
    if (result->szz.nq > 0) {
        q_add(&result->szz, bin, szz, sign);
    }
    if (result->sperp.nq > 0) {
        q_add(&result->sperp, bin, sperp, sign);
    }
    return 0;
}

int replica_result_szz_bin_values(const ReplicaResult *result, int bin,
                                  double *out, int nq)
{
    if (result == NULL || result->bins == NULL ||
        !q_observable_state_is_valid(&result->szz) ||
        result->szz.sum_sign_values == NULL || out == NULL || bin < 0 ||
        bin >= result->nbin || nq <= 0 || nq != result->szz.nq) {
        return 1;
    }
    return szz_bin_ratio(&result->szz.sum_sign_values[nq * bin], nq,
                         result->bins[bin].sum_sign, out);
}

int replica_result_sperp_bin_values(const ReplicaResult *result, int bin,
                                    double *out, int nq)
{
    if (result == NULL || result->bins == NULL ||
        !q_observable_state_is_valid(&result->sperp) ||
        result->sperp.sum_sign_values == NULL || out == NULL || bin < 0 ||
        bin >= result->nbin || nq <= 0 || nq != result->sperp.nq) {
        return 1;
    }
    return sperp_bin_ratio(&result->sperp.sum_sign_values[nq * bin], nq,
                           result->bins[bin].sum_sign, out);
}

void replica_result_free(ReplicaResult *result)
{
    if (result == NULL) {
        return;
    }
    free(result->bins);
    free(result->szz.sum_sign_values);
    free(result->sperp.sum_sign_values);
    result->bins = NULL;
    result->szz.sum_sign_values = NULL;
    result->sperp.sum_sign_values = NULL;
    result->nbin = 0;
    result->szz.nq = 0;
    result->sperp.nq = 0;
    result->replica_id = 0;
    result->seed = 0;
    result->status = 0;
}

void replica_bin_add_global(ReplicaBin *bin, unsigned long long accepted,
                            unsigned long long attempts)
{
    if (bin == NULL) {
        return;
    }
    bin->global_accepted += accepted;
    bin->global_attempts += attempts;
}

double replica_bins_global_acceptance(const ReplicaBin *bins, int nbins,
                                      unsigned long long *attempts)
{
    unsigned long long acc = 0ULL;
    unsigned long long att = 0ULL;
    for (int k = 0; bins != NULL && k < nbins; k++) {
        acc += bins[k].global_accepted;
        att += bins[k].global_attempts;
    }
    if (attempts != NULL) {
        *attempts = att;
    }
    return att == 0ULL ? NAN : (double)acc / (double)att;
}
