#ifndef SIGN_RATIO_H
#define SIGN_RATIO_H

#include <stddef.h>

/* Pooled sign-reweighted ratio over the B = nrep * nbin replica-bin blocks of
   one beta point (block b = replica * nbin + bin). Every block is used,
   including blocks whose sign sum is zero or negative:

     R = sum_b A_b / S,  S = sum_b S_b,
     R(-b) = (A - A_b) / (S - S_b),
     err^2 = (B - 1) / B * sum_b (R(-b) - mean_b R(-b))^2.

   The central value is R itself (no bias correction). */

typedef enum {
    SIGN_RATIO_OK = 0,
    SIGN_RATIO_NONPOSITIVE_TOTAL = 1, /* S <= 0 */
    SIGN_RATIO_NONPOSITIVE_LOO = 2    /* S > 0 but some S - S_b <= 0 */
} SignRatioStatus;

/* "ok", "nonpositive_total", "nonpositive_loo"; NULL for other values. */
const char *sign_ratio_status_name(SignRatioStatus status);

typedef struct {
    int nrep, nbin, nblock;
    const double *S;              /* [nblock] block sign sums, not owned */
    const int *count;             /* [nblock] measurements per block, not owned */
    double S_total;
    long long count_total;
    long long negative_count;     /* (count_total - S_total) / 2 */
    int n_bins_nonpositive;       /* blocks with S_b <= 0 */
    int n_loo_nonpositive;        /* blocks with S - S_b <= 0 */
    int n_replica_nonpositive;    /* replicas with sum_b S_rb <= 0 */
    SignRatioStatus status;
    double sign, dsign;           /* S / C and its delete-one-block jackknife error */
    double f_neg, df_neg;         /* (1 - sign) / 2 and dsign / 2, never clipped */
    double replica_sign_mean;     /* mean_r S_r / C_r over every replica */
    double replica_sign_se;       /* sample standard error; NaN for nrep = 1 */
    int sign_unresolved;          /* 0 < sign <= 2 dsign */
} SignRatioBlocks;

/* Validates the blocks and fills every field. Each count must equal
   expected_count > 0; each S_b must be finite and integral with
   |S_b| <= count_b and count_b - S_b even (measured signs are +-1).
   Returns 1 on a violation (an execution error, not a statistical status). */
int sign_ratio_blocks_init(SignRatioBlocks *b, const double *S,
                           const int *count, int nrep, int nbin,
                           int expected_count);
/* Index of the first block that sign_ratio_blocks_init rejects, or -1. */
int sign_ratio_first_invalid_block(const double *S, const int *count,
                                   int nblock, int expected_count);

/* Pooled ratio of the numerators A_b = A[b * stride] and its jackknife error.
   When b->status != SIGN_RATIO_OK both outputs are NaN and 0 is returned.
   Returns 1 when a numerator is non-finite or the evaluation overflows. */
int sign_ratio_estimate(const SignRatioBlocks *b, const double *A,
                        size_t stride, double *mean, double *err);

#endif
