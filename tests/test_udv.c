#include "test_util.h"
#include "linalg.h"

#include <math.h>

int main(void)
{
    const int n = 3;
    double B1[9] = {1.2, 0.1, 0.0, 0.3, 0.9, 0.2, 0.0, 0.1, 1.1};
    double B2[9] = {0.8, 0.0, 0.2, 0.1, 1.3, 0.0, 0.3, 0.2, 0.7};
    double B3[9] = {1.0, 0.2, 0.1, 0.0, 0.6, 0.3, 0.2, 0.0, 1.4};

    double t[9];
    double P[9];
    la_matmul(n, B3, B2, t);
    la_matmul(n, t, B1, P);

    double IpP[9];
    for (int i = 0; i < 9; i++) {
        IpP[i] = P[i];
    }
    for (int i = 0; i < n; i++) {
        IpP[i + i * n] += 1.0;
    }

    double gref[9];
    CHECK(la_inverse(n, IpP, gref) == 0);

    UDV s;
    udv_init(&s, n);
    udv_lmul(&s, B1);
    udv_lmul(&s, B2);
    udv_lmul(&s, B3);
    double g[9];
    int ds = 0;
    CHECK(udv_inv_one_plus(&s, g, &ds) == 0);
    for (int i = 0; i < 9; i++) {
        CHECK_CLOSE(g[i], gref[i], 1e-9);
    }
    /* det_sign must match the brute-force sign(det(I + P)). */
    double IpPc[9];
    for (int i = 0; i < 9; i++) {
        IpPc[i] = IpP[i];
    }
    int sref = 0;
    double ladref = 0.0;
    CHECK(la_logdet(n, IpPc, &sref, &ladref) == 0);
    CHECK(ds == sref);
    CHECK(ds == 1 || ds == -1);
    udv_free(&s);

    double S1[9] = {1e4, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1e-4};
    double Mix[9] = {1.0, 0.2, -0.1, -0.3, 1.0, 0.4, 0.15, -0.25, 1.0};
    double S2[9] = {1e-3, 0.0, 0.0, 0.0, 1e2, 0.0, 0.0, 0.0, 2.0};

    UDV st;
    udv_init(&st, n);
    udv_lmul(&st, S1);
    udv_lmul(&st, Mix);
    udv_lmul(&st, S2);
    double gst[9];
    int dst = 0;
    CHECK(udv_inv_one_plus(&st, gst, &dst) == 0);
    CHECK(dst == 1 || dst == -1);

    double tmpa[9];
    double Pst[9];
    double Ip[9];
    double R[9];
    la_matmul(n, Mix, S1, tmpa);
    la_matmul(n, S2, tmpa, Pst);
    for (int i = 0; i < 9; i++) {
        Ip[i] = Pst[i];
    }
    for (int i = 0; i < n; i++) {
        Ip[i + i * n] += 1.0;
    }
    la_matmul(n, Ip, gst, R);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            CHECK_CLOSE(R[i + j * n], (i == j) ? 1.0 : 0.0, 1e-6);
        }
    }
    udv_free(&st);

    UDV centered;
    UDV centered_copy;
    udv_init(&centered, 4);
    udv_init(&centered_copy, 4);
    CHECK_CLOSE(centered.log_offset, 0.0, 0.0);
    centered.D[0] = exp(620.0);
    centered.D[1] = -exp(210.0);
    centered.D[2] = exp(-180.0);
    centered.D[3] = -exp(-600.0);
    double before_log[4];
    int before_sign[4];
    for (int i = 0; i < 4; i++) {
        before_log[i] = log(fabs(centered.D[i])) + centered.log_offset;
        before_sign[i] = (centered.D[i] < 0.0) ? -1 : 1;
    }
    double radius = 0.0;
    double margin = 0.0;
    CHECK(udv_recenter(&centered, 8.0, &radius, &margin) == 0);
    CHECK_CLOSE(radius, 610.0, 1e-12);
    CHECK(margin > 99.0 && margin < 101.0);
    CHECK_CLOSE(centered.log_offset, 10.0, 1e-12);
    for (int i = 0; i < 4; i++) {
        CHECK(isfinite(centered.D[i]));
        CHECK(centered.D[i] != 0.0);
        CHECK(((centered.D[i] < 0.0) ? -1 : 1) == before_sign[i]);
        CHECK_CLOSE(log(fabs(centered.D[i])) + centered.log_offset,
                    before_log[i], 1e-12);
    }
    udv_copy(&centered_copy, &centered);
    CHECK_CLOSE(centered_copy.log_offset, centered.log_offset, 0.0);
    udv_identity(&centered_copy);
    CHECK_CLOSE(centered_copy.log_offset, 0.0, 0.0);

    centered.D[0] = exp(705.0);
    centered.D[1] = exp(-705.0);
    centered.D[2] = 1.0;
    centered.D[3] = -1.0;
    centered.log_offset = 17.0;
    double failed_D[4];
    for (int i = 0; i < 4; i++) failed_D[i] = centered.D[i];
    CHECK(udv_recenter(&centered, 8.0, &radius, &margin) != 0);
    CHECK(margin < 8.0);
    CHECK_CLOSE(centered.log_offset, 17.0, 0.0);
    for (int i = 0; i < 4; i++) CHECK_CLOSE(centered.D[i], failed_D[i], 0.0);
    udv_free(&centered_copy);
    udv_free(&centered);

    UDV shifted;
    UDV shifted_ref;
    udv_init(&shifted, n);
    udv_init(&shifted_ref, n);
    udv_lmul(&shifted_ref, B1);
    udv_lmul(&shifted_ref, B2);
    udv_copy(&shifted, &shifted_ref);
    const double shift = 5.0;
    for (int i = 0; i < n; i++) shifted.D[i] *= exp(-shift);
    shifted.log_offset = shift;
    double g_shifted[9];
    double g_shifted_ref[9];
    int sign_shifted = 0;
    int sign_shifted_ref = 0;
    CHECK(udv_inv_one_plus(&shifted, g_shifted, &sign_shifted) == 0);
    CHECK(udv_inv_one_plus(&shifted_ref, g_shifted_ref,
                           &sign_shifted_ref) == 0);
    CHECK(sign_shifted == sign_shifted_ref);
    for (int i = 0; i < n * n; i++)
        CHECK_CLOSE(g_shifted[i], g_shifted_ref[i], 1e-12);
    udv_free(&shifted_ref);
    udv_free(&shifted);

    UDV huge;
    udv_init(&huge, n);
    huge.D[0] = exp(600.0);
    huge.D[1] = 1.0;
    huge.D[2] = exp(-600.0);
    huge.log_offset = 50.0;
    double g_huge[9];
    int sign_huge = 0;
    CHECK(udv_inv_one_plus(&huge, g_huge, &sign_huge) == 0);
    CHECK(sign_huge == 1);
    CHECK(isfinite(g_huge[0]));
    CHECK_CLOSE(g_huge[0], exp(-650.0), 1e-290);
    CHECK_CLOSE(g_huge[4], 1.0 / (1.0 + exp(50.0)), 1e-32);
    CHECK_CLOSE(g_huge[8], 1.0, 1e-15);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            if (i != j) CHECK_CLOSE(g_huge[i + j * n], 0.0, 1e-15);
    udv_free(&huge);

    UDV signed_base;
    UDV signed_shifted;
    udv_init(&signed_base, n);
    udv_init(&signed_shifted, n);
    signed_base.D[0] = -3.0;
    signed_base.D[1] = 0.5;
    signed_base.D[2] = 2.0;
    udv_copy(&signed_shifted, &signed_base);
    signed_shifted.log_offset = 2.0;
    for (int i = 0; i < n; i++) {
        signed_shifted.D[i] *= exp(-signed_shifted.log_offset);
    }
    CHECK(fabs(signed_shifted.D[0]) < 1.0);
    CHECK(udv_inv_one_plus(&signed_base, g_shifted_ref,
                           &sign_shifted_ref) == 0);
    CHECK(udv_inv_one_plus(&signed_shifted, g_shifted,
                           &sign_shifted) == 0);
    CHECK(sign_shifted == sign_shifted_ref);
    for (int i = 0; i < n * n; i++) {
        CHECK_CLOSE(g_shifted[i], g_shifted_ref[i], 1e-12);
    }
    udv_free(&signed_shifted);
    udv_free(&signed_base);

    UDV out_of_range;
    udv_init(&out_of_range, n);
    out_of_range.log_offset = 800.0;
    LinalgWork range_work;
    linalg_work_init(&range_work, n);
    CHECK(range_work.ok);
    CHECK(udv_inv_one_plus_work(&out_of_range, g, &ds, &range_work) != 0);
    CHECK(range_work.failed);
    CHECK(range_work.failure_reason == LINALG_FAILURE_EFFECTIVE_LOG_RANGE);
    linalg_work_free(&range_work);
    udv_free(&out_of_range);

    TEST_END();
}
