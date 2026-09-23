#include "test_util.h"
#include "measure.h"
#include "replica.h"

#include <math.h>
#include <stdlib.h>

int main(void)
{
    const unsigned long long base = 246813579ULL;
    CHECK(replica_seed(base, 0, 0) == base);
    CHECK(replica_seed(base, 3, 0) == base + 3000ULL);

    const unsigned long long s10 = replica_seed(base, 0, 1);
    const unsigned long long s11 = replica_seed(base, 0, 1);
    const unsigned long long s20 = replica_seed(base, 0, 2);
    CHECK(s10 == s11);
    CHECK(s10 != base);
    CHECK(s10 != s20);

    unsigned long long seeds[4];
    for (int r = 0; r < 4; r++) {
        seeds[r] = replica_seed(base, 2, r);
    }
    CHECK(replica_check_seed_unique(seeds, 4) == 0);
    seeds[3] = seeds[1];
    CHECK(replica_check_seed_unique(seeds, 4) != 0);

    ReplicaBin bin = {0};
    MeasSample sample;
    sample.ekin = -2.0;
    sample.eint = 0.5;
    sample.ntot = 4.0;
    sample.doublon = 0.125;
    sample.E = -1.5;
    CHECK(replica_bin_add(&bin, &sample, 2.0, 4.0, 4, 1.0) == 0);
    CHECK(replica_bin_add(&bin, &sample, 2.0, 4.0, 4, 1.0) == 0);
    replica_bin_add_acceptance(&bin, 3, 8);
    replica_bin_add_acceptance(&bin, 1, 2);
    CHECK(bin.count == 2);
    CHECK(fabs(bin.sum_sign_Ehub - (-3.0)) < 1e-12);
    CHECK(fabs(bin.sum_sign_Egc - (-19.0)) < 1e-12);
    CHECK(fabs(bin.sum_sign_Eph - (-11.0)) < 1e-12);
    CHECK(fabs(bin.sum_sign_N - 8.0) < 1e-12);
    CHECK(fabs(bin.sum_sign_D - 0.25) < 1e-12);
    CHECK(fabs(bin.sum_sign - 2.0) < 1e-12);
    CHECK(bin.accept_accepted == 4);
    CHECK(bin.accept_attempts == 10);
    CHECK(fabs(replica_bin_acceptance(&bin) - 0.4) < 1e-12);

    double eh, eg, ep, nn, dd, ss;
    CHECK(replica_bin_values(&bin, &eh, &eg, &ep, &nn, &dd, &ss) == 0);
    CHECK(fabs(eh - (-1.5)) < 1e-12);
    CHECK(fabs(eg - (-9.5)) < 1e-12);
    CHECK(fabs(ep - (-5.5)) < 1e-12);
    CHECK(fabs(nn - 4.0) < 1e-12);
    CHECK(fabs(dd - 0.125) < 1e-12);
    CHECK(fabs(ss - 1.0) < 1e-12);

    ReplicaBin bad = {0};
    bad.count = 1;
    CHECK(replica_bin_values(&bad, &eh, &eg, &ep, &nn, &dd, &ss) != 0);

    ReplicaBin nan_sum = {0};
    nan_sum.count = 1;
    nan_sum.sum_sign = 1.0;
    nan_sum.sum_sign_Ehub = NAN;
    nan_sum.sum_sign_Egc = -1.0;
    nan_sum.sum_sign_Eph = -1.0;
    nan_sum.sum_sign_N = 4.0;
    nan_sum.sum_sign_D = 0.125;
    CHECK(replica_bin_values(&nan_sum, &eh, &eg, &ep, &nn, &dd, &ss) != 0);

    ReplicaBin reject = {0};
    MeasSample nan_sample = sample;
    nan_sample.E = NAN;
    CHECK(replica_bin_add(&reject, &nan_sample, 2.0, 4.0, 4, 1.0) != 0);
    CHECK(replica_bin_add(&reject, &sample, 2.0, 4.0, 4, NAN) != 0);
    CHECK(reject.count == 0);

    ReplicaResult result;
    CHECK(replica_result_alloc(&result, 3) == 0);
    CHECK(result.nbin == 3);
    CHECK(result.bins != NULL);
    CHECK(result.szz.nq == 0);
    CHECK(result.szz.sum_sign_values == NULL);
    CHECK(result.sperp.nq == 0);
    CHECK(result.sperp.sum_sign_values == NULL);
    CHECK(replica_result_enable_szz(&result, 2) == 0);
    CHECK(replica_result_enable_sperp(&result, 2) == 0);
    CHECK(result.szz.nq == 2);
    CHECK(result.szz.sum_sign_values != NULL);
    CHECK(result.sperp.nq == 2);
    CHECK(result.sperp.sum_sign_values != NULL);

    const double szz_a[2] = {2.0, 4.0};
    const double szz_b[2] = {1.0, 2.0};
    CHECK(replica_result_add_szz(&result, 0, szz_a, 2, 1.0) == 0);
    CHECK(replica_bin_add(&result.bins[0], &sample, 2.0, 4.0, 4, 1.0) == 0);
    CHECK(replica_result_add_szz(&result, 0, szz_a, 2, 1.0) == 0);
    CHECK(replica_bin_add(&result.bins[0], &sample, 2.0, 4.0, 4, 1.0) == 0);
    CHECK(replica_result_add_szz(&result, 0, szz_b, 2, -1.0) == 0);
    CHECK(replica_bin_add(&result.bins[0], &sample, 2.0, 4.0, 4, -1.0) == 0);
    double szz_value[2] = {0};
    CHECK(replica_result_szz_bin_values(&result, 0, szz_value, 2) == 0);
    CHECK_CLOSE(szz_value[0], 3.0, 1e-12);
    CHECK_CLOSE(szz_value[1], 6.0, 1e-12);

    const double sperp_a[2] = {6.0, 8.0};
    const double sperp_b[2] = {3.0, 4.0};
    CHECK(replica_result_add_sperp(&result, 0, sperp_a, 2, 1.0) == 0);
    CHECK(replica_result_add_sperp(&result, 0, sperp_a, 2, 1.0) == 0);
    CHECK(replica_result_add_sperp(&result, 0, sperp_b, 2, -1.0) == 0);
    double sperp_value[2] = {0};
    CHECK(replica_result_sperp_bin_values(&result, 0, sperp_value, 2) == 0);
    CHECK_CLOSE(sperp_value[0], 9.0, 1e-12);
    CHECK_CLOSE(sperp_value[1], 12.0, 1e-12);

    const double before0 = result.szz.sum_sign_values[0];
    const double sperp_before0 = result.sperp.sum_sign_values[0];
    const double bad_szz[2] = {NAN, 1.0};
    const double bad_sperp[2] = {1.0, INFINITY};
    CHECK(replica_result_add_szz(&result, 0, bad_szz, 2, 1.0) != 0);
    CHECK(replica_result_add_szz(&result, 0, szz_a, 1, 1.0) != 0);
    CHECK(replica_result_add_szz(&result, 3, szz_a, 2, 1.0) != 0);
    CHECK(replica_result_add_szz(&result, 0, szz_a, 2, NAN) != 0);
    CHECK(replica_result_add_sperp(&result, 0, bad_sperp, 2, 1.0) != 0);
    CHECK(replica_result_add_sperp(&result, 0, sperp_a, 1, 1.0) != 0);
    CHECK_CLOSE(result.szz.sum_sign_values[0], before0, 0.0);
    CHECK_CLOSE(result.sperp.sum_sign_values[0], sperp_before0, 0.0);
    CHECK(replica_result_szz_bin_values(&result, 0, szz_value, 1) != 0);
    CHECK(replica_result_szz_bin_values(&result, 1, szz_value, 2) != 0);

    const int count_before = result.bins[0].count;
    CHECK(replica_result_add_sample(&result, 0, &sample, 2.0, 4.0, 4,
                                    1.0, bad_szz, 2, sperp_a, 2) != 0);
    CHECK(replica_result_add_sample(&result, 0, &sample, 2.0, 4.0, 4,
                                    1.0, szz_a, 2, bad_sperp, 2) != 0);
    CHECK(result.bins[0].count == count_before);
    CHECK_CLOSE(result.szz.sum_sign_values[0], before0, 0.0);
    CHECK_CLOSE(result.sperp.sum_sign_values[0], sperp_before0, 0.0);
    replica_result_free(&result);
    CHECK(result.bins == NULL);
    CHECK(result.szz.sum_sign_values == NULL);
    CHECK(result.sperp.sum_sign_values == NULL);
    CHECK(result.nbin == 0);
    CHECK(result.szz.nq == 0);
    CHECK(result.sperp.nq == 0);

    ReplicaResult invalid_state;
    CHECK(replica_result_alloc(&invalid_state, 1) == 0);
    invalid_state.sperp.nq = 1;
    CHECK(replica_result_add_sample(&invalid_state, 0, &sample, 2.0, 4.0,
                                    4, 1.0, NULL, 0, NULL, 0) != 0);
    CHECK(invalid_state.bins[0].count == 0);
    replica_result_free(&invalid_state);

    ReplicaResult disabled_observables;
    CHECK(replica_result_alloc(&disabled_observables, 1) == 0);
    CHECK(replica_result_add_sample(&disabled_observables, 0, &sample, 2.0,
                                    4.0, 4, 1.0, szz_a, 2, NULL, 0) != 0);
    CHECK(disabled_observables.bins[0].count == 0);
    CHECK(replica_result_add_sample(&disabled_observables, 0, &sample, 2.0,
                                    4.0, 4, 1.0, NULL, 0, NULL, 0) == 0);
    CHECK(disabled_observables.bins[0].count == 1);
    replica_result_free(&disabled_observables);

    ReplicaResult r0;
    ReplicaResult r1;
    CHECK(replica_result_alloc(&r0, 2) == 0);
    CHECK(replica_result_alloc(&r1, 2) == 0);
    CHECK(replica_bin_add(&r0.bins[0], &sample, 2.0, 4.0, 4, 1.0) == 0);
    CHECK(replica_bin_add(&r0.bins[1], &sample, 2.0, 4.0, 4, 1.0) == 0);
    CHECK(replica_bin_add(&r1.bins[0], &sample, 2.0, 4.0, 4, 1.0) == 0);
    CHECK(replica_bin_add(&r1.bins[1], &sample, 2.0, 4.0, 4, 1.0) == 0);
    CHECK(r0.nbin + r1.nbin == 4);
    replica_result_free(&r0);
    replica_result_free(&r1);

    TEST_END();
}
