/* Pooled sign-reweighted ratio and its status (plan section 6.1): the five
   hand-computed examples with count 10 per bin, the legacy jackknife limit,
   replica statistics, and inputs that must be execution errors. */
#include "test_util.h"
#include "measure.h"
#include "sign_ratio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Delete-one jackknife of R = sum A / sum S written out directly. */
static void hand_jackknife(const double *S, const double *A, int B,
                           double *mean, double *err)
{
    double sa = 0.0;
    double ss = 0.0;
    for (int b = 0; b < B; b++) {
        sa += A[b];
        ss += S[b];
    }
    *mean = sa / ss;
    double avg = 0.0;
    for (int b = 0; b < B; b++) {
        avg += (sa - A[b]) / (ss - S[b]);
    }
    avg /= B;
    double var = 0.0;
    for (int b = 0; b < B; b++) {
        const double d = (sa - A[b]) / (ss - S[b]) - avg;
        var += d * d;
    }
    *err = sqrt(var * (B - 1) / B);
}

static void check_case(const double *S, const double *A, double ratio,
                       double sign, SignRatioStatus status, int n_loo,
                       int n_bins)
{
    const int count[3] = {10, 10, 10};
    SignRatioBlocks b;
    CHECK(sign_ratio_blocks_init(&b, S, count, 1, 3, 10) == 0);
    CHECK(b.status == status);
    CHECK(b.nblock == 3);
    CHECK(b.count_total == 30);
    CHECK_CLOSE(b.sign, sign, 1e-15);
    CHECK_CLOSE(b.f_neg, 0.5 * (1.0 - sign), 1e-15);
    CHECK(b.n_loo_nonpositive == n_loo);
    CHECK(b.n_bins_nonpositive == n_bins);
    CHECK(b.negative_count == (30 - (long long)(S[0] + S[1] + S[2])) / 2);
    double mean = 0.0;
    double err = 0.0;
    CHECK(sign_ratio_estimate(&b, A, 1, &mean, &err) == 0);
    if (status == SIGN_RATIO_OK) {
        double hm = 0.0;
        double he = 0.0;
        hand_jackknife(S, A, 3, &hm, &he);
        CHECK_CLOSE(mean, ratio, 1e-15);
        CHECK_CLOSE(mean, hm, 1e-15);
        CHECK_CLOSE(err, he, 1e-15);
    } else {
        CHECK(isnan(mean) && isnan(err));
    }
    /* the sign error is the jackknife of (S - S_b) / (C - C_b) */
    double avg = 0.0;
    const double tot = S[0] + S[1] + S[2];
    for (int k = 0; k < 3; k++) {
        avg += (tot - S[k]) / 20.0;
    }
    avg /= 3.0;
    double var = 0.0;
    for (int k = 0; k < 3; k++) {
        const double d = (tot - S[k]) / 20.0 - avg;
        var += d * d;
    }
    CHECK_CLOSE(b.dsign, sqrt(var * 2.0 / 3.0), 1e-15);
    CHECK_CLOSE(b.df_neg, 0.5 * b.dsign, 1e-15);
}

static void check_plan_examples(void)
{
    /* a zero bin does not stop the run */
    check_case((double[]){0, 4, 6}, (double[]){3, 8, 12}, 2.3, 10.0 / 30.0,
               SIGN_RATIO_OK, 0, 1);
    /* a negative bin does not stop the run */
    check_case((double[]){-2, 6, 6}, (double[]){1, 12, 12}, 2.5, 10.0 / 30.0,
               SIGN_RATIO_OK, 0, 1);
    /* total 4 but a leave-one-out denominator of -2 */
    check_case((double[]){6, -2, 0}, (double[]){12, 2, 1}, NAN, 4.0 / 30.0,
               SIGN_RATIO_NONPOSITIVE_LOO, 1, 2);
    /* a leave-one-out denominator of exactly 0 */
    check_case((double[]){4, 0, 0}, (double[]){8, 2, 1}, NAN, 4.0 / 30.0,
               SIGN_RATIO_NONPOSITIVE_LOO, 1, 2);
    /* total sign sum 0 */
    check_case((double[]){-2, 0, 2}, (double[]){1, 3, 5}, NAN, 0.0,
               SIGN_RATIO_NONPOSITIVE_TOTAL, 2, 2);
    /* negative total: the sign and f_neg are not clipped */
    check_case((double[]){-2, -4, 0}, (double[]){1, 3, 5}, NAN, -6.0 / 30.0,
               SIGN_RATIO_NONPOSITIVE_TOTAL, 3, 3);
}

/* sign = 1 and equal counts: the pooled ratio is the legacy bin jackknife */
static void check_ph_limit(void)
{
    enum { B = 40 };
    double S[B];
    double A[B];
    double x[B];
    int count[B];
    for (int b = 0; b < B; b++) {
        count[b] = 25;
        S[b] = 25.0;
        x[b] = -1.7 + 0.37 * sin(1.3 * b) + 0.01 * b;
        A[b] = x[b] * S[b];
    }
    SignRatioBlocks blk;
    CHECK(sign_ratio_blocks_init(&blk, S, count, 4, 10, 25) == 0);
    CHECK(blk.status == SIGN_RATIO_OK);
    CHECK(blk.sign == 1.0 && blk.dsign == 0.0);
    CHECK(blk.negative_count == 0 && blk.n_replica_nonpositive == 0);
    CHECK(blk.replica_sign_mean == 1.0 && blk.replica_sign_se == 0.0);
    CHECK(blk.sign_unresolved == 0);
    double m = 0.0, e = 0.0, lm = 0.0, le = 0.0;
    CHECK(sign_ratio_estimate(&blk, A, 1, &m, &e) == 0);
    jackknife(x, B, &lm, &le);
    CHECK_CLOSE(m, lm, 1e-14 * fabs(lm));
    CHECK_CLOSE(e, le, 1e-14 * fabs(le) + 1e-16);

    /* two blocks are enough */
    CHECK(sign_ratio_blocks_init(&blk, S, count, 1, 2, 25) == 0);
    CHECK(sign_ratio_estimate(&blk, A, 1, &m, &e) == 0);
    jackknife(x, 2, &lm, &le);
    CHECK_CLOSE(m, lm, 1e-14 * fabs(lm));
    CHECK_CLOSE(e, le, 1e-14 * fabs(le) + 1e-16);

    /* the stride selects one q column of a block-major array */
    double q2[2 * B];
    for (int b = 0; b < B; b++) {
        q2[2 * b] = 1e9;
        q2[2 * b + 1] = A[b];
    }
    CHECK(sign_ratio_blocks_init(&blk, S, count, 4, 10, 25) == 0);
    CHECK(sign_ratio_estimate(&blk, q2 + 1, 2, &m, &e) == 0);
    jackknife(x, B, &lm, &le);
    CHECK_CLOSE(m, lm, 1e-14 * fabs(lm));
}

static void check_replica_statistics(void)
{
    /* replica 0: sums 6, 4 of 20; replica 1: -2, 0 of 20 (non-positive) */
    const double S[4] = {6, 4, -2, 0};
    const int count[4] = {10, 10, 10, 10};
    SignRatioBlocks b;
    CHECK(sign_ratio_blocks_init(&b, S, count, 2, 2, 10) == 0);
    CHECK(b.n_replica_nonpositive == 1);
    CHECK(b.n_bins_nonpositive == 2);
    CHECK_CLOSE(b.replica_sign_mean, 0.5 * (10.0 / 20.0 + -2.0 / 20.0), 1e-15);
    const double d = 0.5 * (10.0 / 20.0 - -2.0 / 20.0);
    CHECK_CLOSE(b.replica_sign_se, sqrt(2.0 * d * d / 1.0 / 2.0), 1e-15);
    CHECK(b.negative_count == (40 - 8) / 2);

    /* nrep = 1: the replica standard error is undefined */
    CHECK(sign_ratio_blocks_init(&b, S, count, 1, 4, 10) == 0);
    CHECK(isnan(b.replica_sign_se));
    CHECK_CLOSE(b.replica_sign_mean, 8.0 / 40.0, 1e-15);
}

static void check_unresolved_flag(void)
{
    /* sign 2/40 with a jackknife error above sign / 2 */
    const double S[4] = {4, -2, 2, -2};
    const int count[4] = {10, 10, 10, 10};
    SignRatioBlocks b;
    CHECK(sign_ratio_blocks_init(&b, S, count, 1, 4, 10) == 0);
    CHECK(b.sign > 0.0 && b.sign <= 2.0 * b.dsign);
    CHECK(b.sign_unresolved == 1);
    /* a well resolved sign */
    const double T[4] = {10, 10, 8, 10};
    CHECK(sign_ratio_blocks_init(&b, T, count, 1, 4, 10) == 0);
    CHECK(b.sign_unresolved == 0);
}

static void check_errors(void)
{
    SignRatioBlocks b;
    const double S[3] = {10, 8, 6};
    const int good[3] = {10, 10, 10};
    /* count mismatch, zero count (missing replica), wrong expectation */
    CHECK(sign_ratio_blocks_init(&b, S, (int[]){10, 9, 10}, 1, 3, 10) != 0);
    CHECK(sign_ratio_first_invalid_block(S, (int[]){10, 9, 10}, 3, 10) == 1);
    CHECK(sign_ratio_blocks_init(&b, S, (int[]){10, 10, 0}, 1, 3, 10) != 0);
    CHECK(sign_ratio_blocks_init(&b, S, good, 1, 3, 11) != 0);
    /* sign sums must be integral, bounded by the count, and of its parity */
    CHECK(sign_ratio_blocks_init(&b, (double[]){10, 7.5, 6}, good, 1, 3, 10) != 0);
    CHECK(sign_ratio_blocks_init(&b, (double[]){12, 8, 6}, good, 1, 3, 10) != 0);
    CHECK(sign_ratio_blocks_init(&b, (double[]){10, 7, 6}, good, 1, 3, 10) != 0);
    CHECK(sign_ratio_first_invalid_block((double[]){10, 7, 6}, good, 3, 10) == 1);
    CHECK(sign_ratio_blocks_init(&b, (double[]){10, NAN, 6}, good, 1, 3, 10) != 0);
    CHECK(sign_ratio_blocks_init(&b, (double[]){10, 8, INFINITY}, good, 1, 3, 10) != 0);
    CHECK(sign_ratio_first_invalid_block(S, good, 3, 10) == -1);
    /* one block cannot be jackknifed */
    CHECK(sign_ratio_blocks_init(&b, S, good, 1, 1, 10) != 0);

    /* a non-finite numerator is an error even when the status is not ok */
    double m = 0.0, e = 0.0;
    CHECK(sign_ratio_blocks_init(&b, S, good, 1, 3, 10) == 0);
    CHECK(sign_ratio_estimate(&b, (double[]){1, NAN, 2}, 1, &m, &e) != 0);
    CHECK(isnan(m) && isnan(e));
    CHECK(sign_ratio_blocks_init(&b, (double[]){-2, 0, 2}, good, 1, 3, 10) == 0);
    CHECK(sign_ratio_estimate(&b, (double[]){1, INFINITY, 2}, 1, &m, &e) != 0);
    CHECK(sign_ratio_estimate(&b, (double[]){1, 2, 2}, 1, &m, &e) == 0);
    CHECK(isnan(m) && isnan(e));

    /* overflow of the pooled numerator is an error, not a NaN status */
    CHECK(sign_ratio_blocks_init(&b, S, good, 1, 3, 10) == 0);
    CHECK(sign_ratio_estimate(&b, (double[]){1e308, 1e308, 1e308}, 1, &m, &e) != 0);
    CHECK(sign_ratio_status_name(SIGN_RATIO_OK) != NULL &&
          strcmp(sign_ratio_status_name(SIGN_RATIO_NONPOSITIVE_LOO),
                 "nonpositive_loo") == 0);
}

/* Many blocks of large, nearly equal numerators: the pooled sum is
   compensated, so the ratio keeps the small offsets. */
static void check_many_blocks(void)
{
    enum { B = 100000 };
    double *S = malloc(B * sizeof *S);
    double *A = malloc(B * sizeof *A);
    int *count = malloc(B * sizeof *count);
    /* A_b - 1e8 is exact (Sterbenz), and the offsets sum to about 0.6, so
       B * 1e8 + sum(offsets) is the sum to double precision; a plain
       running sum of A_b loses about 0.5 of it. */
    double offsets = 0.0;
    for (int b = 0; b < B; b++) {
        count[b] = 2;
        S[b] = (b % 7 == 0) ? 0.0 : 2.0;
        A[b] = 1e8 + 1e-6 * (double)(b % 13);
        offsets += A[b] - 1e8;
    }
    const double exact = (double)B * 1e8 + offsets;
    SignRatioBlocks blk;
    CHECK(sign_ratio_blocks_init(&blk, S, count, 10, B / 10, 2) == 0);
    CHECK(blk.status == SIGN_RATIO_OK);
    double m = 0.0, e = 0.0;
    CHECK(sign_ratio_estimate(&blk, A, 1, &m, &e) == 0);
    const double ref = exact / blk.S_total;
    CHECK_CLOSE(m, ref, 4e-16 * fabs(ref));
    CHECK(isfinite(e));
    free(S);
    free(A);
    free(count);
}

int main(void)
{
    check_plan_examples();
    check_ph_limit();
    check_replica_statistics();
    check_unresolved_flag();
    check_errors();
    check_many_blocks();
    TEST_END();
}
