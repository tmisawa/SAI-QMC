#include "sign_ratio.h"

#include <limits.h>
#include <math.h>

const char *sign_ratio_status_name(SignRatioStatus status)
{
    switch (status) {
    case SIGN_RATIO_OK:
        return "ok";
    case SIGN_RATIO_NONPOSITIVE_TOTAL:
        return "nonpositive_total";
    case SIGN_RATIO_NONPOSITIVE_LOO:
        return "nonpositive_loo";
    }
    return NULL;
}

/* Neumaier compensated sum: the bin sums of a long run differ in magnitude,
   and the pooled numerator must not depend on the block order. */
static double compensated_sum(const double *x, size_t stride, int n)
{
    double sum = 0.0;
    double c = 0.0;
    for (int b = 0; b < n; b++) {
        const double v = x[(size_t)b * stride];
        const double t = sum + v;
        if (fabs(sum) >= fabs(v)) {
            c += (sum - t) + v;
        } else {
            c += (v - t) + sum;
        }
        sum = t;
    }
    return sum + c;
}

static int block_is_valid(double s, int count, int expected_count)
{
    return count == expected_count && isfinite(s) && s == floor(s) &&
           fabs(s) <= (double)count &&
           (((long long)count - (long long)s) % 2) == 0;
}

int sign_ratio_first_invalid_block(const double *S, const int *count,
                                   int nblock, int expected_count)
{
    if (S == NULL || count == NULL) {
        return 0;
    }
    for (int k = 0; k < nblock; k++) {
        if (expected_count <= 0 || !block_is_valid(S[k], count[k], expected_count)) {
            return k;
        }
    }
    return -1;
}

int sign_ratio_blocks_init(SignRatioBlocks *b, const double *S,
                           const int *count, int nrep, int nbin,
                           int expected_count)
{
    if (b == NULL) {
        return 1;
    }
    *b = (SignRatioBlocks){0};
    b->sign = b->dsign = b->f_neg = b->df_neg = NAN;
    b->replica_sign_mean = b->replica_sign_se = NAN;
    if (S == NULL || count == NULL || nrep < 1 || nbin < 1 ||
        expected_count <= 0 || nrep > INT_MAX / nbin) {
        return 1;
    }
    const int B = nrep * nbin;
    if (B < 2) {
        return 1;
    }
    b->nrep = nrep;
    b->nbin = nbin;
    b->nblock = B;
    b->S = S;
    b->count = count;

    long long C = 0;
    double total = 0.0;
    for (int k = 0; k < B; k++) {
        const double s = S[k];
        if (!block_is_valid(s, count[k], expected_count)) {
            return 1;
        }
        C += count[k];
        total += s; /* integers below 2^53: exact */
        if (s <= 0.0) {
            b->n_bins_nonpositive++;
        }
    }
    b->S_total = total;
    b->count_total = C;
    b->negative_count = (C - (long long)total) / 2;

    for (int k = 0; k < B; k++) {
        if (total - S[k] <= 0.0) {
            b->n_loo_nonpositive++;
        }
    }
    b->status = total <= 0.0 ? SIGN_RATIO_NONPOSITIVE_TOTAL
                : b->n_loo_nonpositive > 0 ? SIGN_RATIO_NONPOSITIVE_LOO
                                           : SIGN_RATIO_OK;

    /* the sign itself has positive denominators C and C - C_b */
    b->sign = total / (double)C;
    double loo_mean = 0.0;
    for (int k = 0; k < B; k++) {
        loo_mean += (total - S[k]) / (double)(C - count[k]);
    }
    loo_mean /= (double)B;
    double var = 0.0;
    for (int k = 0; k < B; k++) {
        const double d = (total - S[k]) / (double)(C - count[k]) - loo_mean;
        var += d * d;
    }
    b->dsign = sqrt(var * (double)(B - 1) / (double)B);
    b->f_neg = 0.5 * (1.0 - b->sign);
    b->df_neg = 0.5 * b->dsign;
    b->sign_unresolved = b->sign > 0.0 && b->sign <= 2.0 * b->dsign;

    double rmean = 0.0;
    for (int r = 0; r < nrep; r++) {
        double sr = 0.0;
        long long cr = 0;
        for (int k = r * nbin; k < (r + 1) * nbin; k++) {
            sr += S[k];
            cr += count[k];
        }
        if (sr <= 0.0) {
            b->n_replica_nonpositive++;
        }
        rmean += sr / (double)cr;
    }
    rmean /= (double)nrep;
    b->replica_sign_mean = rmean;
    if (nrep > 1) {
        double rvar = 0.0;
        for (int r = 0; r < nrep; r++) {
            double sr = 0.0;
            long long cr = 0;
            for (int k = r * nbin; k < (r + 1) * nbin; k++) {
                sr += S[k];
                cr += count[k];
            }
            const double d = sr / (double)cr - rmean;
            rvar += d * d;
        }
        b->replica_sign_se = sqrt(rvar / (double)(nrep - 1) / (double)nrep);
    }
    return (isfinite(b->sign) && isfinite(b->dsign)) ? 0 : 1;
}

int sign_ratio_estimate(const SignRatioBlocks *b, const double *A,
                        size_t stride, double *mean, double *err)
{
    if (mean != NULL) {
        *mean = NAN;
    }
    if (err != NULL) {
        *err = NAN;
    }
    if (b == NULL || b->S == NULL || A == NULL || stride == 0 ||
        mean == NULL || err == NULL || b->nblock < 2) {
        return 1;
    }
    const int B = b->nblock;
    for (int k = 0; k < B; k++) {
        if (!isfinite(A[(size_t)k * stride])) {
            return 1;
        }
    }
    const double total = compensated_sum(A, stride, B);
    if (!isfinite(total)) {
        return 1;
    }
    if (b->status != SIGN_RATIO_OK) {
        return 0;
    }
    const double S = b->S_total;
    const double R = total / S;
    double loo_mean = 0.0;
    for (int k = 0; k < B; k++) {
        const double loo = (total - A[(size_t)k * stride]) / (S - b->S[k]);
        if (!isfinite(loo)) {
            return 1;
        }
        loo_mean += loo;
    }
    loo_mean /= (double)B;
    double var = 0.0;
    for (int k = 0; k < B; k++) {
        const double d =
            (total - A[(size_t)k * stride]) / (S - b->S[k]) - loo_mean;
        var += d * d;
    }
    const double e = sqrt(var * (double)(B - 1) / (double)B);
    if (!isfinite(R) || !isfinite(e)) {
        return 1;
    }
    *mean = R;
    *err = e;
    return 0;
}
