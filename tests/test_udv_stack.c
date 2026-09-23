#include "test_util.h"
#include "linalg.h"

#include <math.h>
#include <string.h>

static void udv_product(const UDV *s, double *out)
{
    const int n = s->n;
    double UD[36];
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            UD[i + j * n] =
                s->U[i + j * n] * s->D[j] * exp(s->log_offset);
        }
    }
    la_matmul(n, UD, s->T, out);
}

static void check_matrix_close(int n, const double *a, const double *b,
                               double tol)
{
    double num = 0.0;
    double den = 0.0;
    for (int i = 0; i < n * n; i++) {
        const double d = a[i] - b[i];
        num += d * d;
        den += b[i] * b[i];
    }
    const double rel = sqrt(num) / (sqrt(den) + 1e-30);
    CHECK(rel < tol);
}

static void check_orthogonal(const UDV *s, double tol)
{
    const int n = s->n;
    double utu[36];
    la_gemm(n, 1, 0, 1.0, s->U, s->U, 0.0, utu);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            CHECK_CLOSE(utu[i + j * n], (i == j) ? 1.0 : 0.0, tol);
        }
    }
}

static void check_unit_upper(const UDV *s, double tol)
{
    const int n = s->n;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            if (i > j) {
                CHECK_CLOSE(s->T[i + j * n], 0.0, tol);
            } else if (i == j) {
                CHECK_CLOSE(s->T[i + j * n], 1.0, tol);
            }
        }
    }
}

static void check_det_sign(int n, const UDV *s, const double *pref,
                           LinalgWork *w)
{
    double g[36];
    int ds = 0;
    CHECK(udv_inv_one_plus_work(s, g, &ds, w) == 0);
    double ip[36];
    memcpy(ip, pref, sizeof(double) * (size_t)n * (size_t)n);
    for (int i = 0; i < n; i++) {
        ip[i + i * n] += 1.0;
    }
    int sref = 0;
    double logabs = 0.0;
    CHECK(la_logdet_work(n, ip, &sref, &logabs, w) == 0);
    CHECK(ds == sref);
}

static void check_inv_one_plus(int n, const UDV *s, const double *pref,
                               LinalgWork *w, double tol)
{
    double g[36];
    int ds = 0;
    CHECK(udv_inv_one_plus_work(s, g, &ds, w) == 0);

    double ip[36];
    double gref[36];
    memcpy(ip, pref, sizeof(double) * (size_t)n * (size_t)n);
    for (int i = 0; i < n; i++) {
        ip[i + i * n] += 1.0;
    }
    CHECK(la_inverse_work(n, ip, gref, w) == 0);
    check_matrix_close(n, g, gref, tol);
}

int main(void)
{
    const int n = 4;
    double B1[16] = {1.2, 0.1, 0.0, 0.2, 0.3, 0.9, 0.2, 0.0,
                     0.0, 0.1, 1.1, 0.3, 0.2, 0.0, 0.1, 0.8};
    double B2[16] = {0.8, 0.0, 0.2, 0.1, 0.1, 1.3, 0.0, 0.2,
                     0.3, 0.2, 0.7, 0.0, 0.0, 0.1, 0.4, 1.1};
    double R1[16] = {1.0, 0.2, 0.1, 0.0, 0.0, 0.6, 0.3, 0.2,
                     0.2, 0.0, 1.4, 0.1, 0.1, 0.3, 0.0, 0.9};
    double R2[16] = {0.7, 0.1, 0.0, 0.2, 0.2, 1.1, 0.2, 0.0,
                     0.0, 0.2, 0.9, 0.3, 0.1, 0.0, 0.2, 1.2};

    LinalgWork w;
    linalg_work_init(&w, n);
    CHECK(w.ok);

    UDV left;
    udv_init(&left, n);
    udv_lmul_work(&left, B1, &w);
    udv_lmul_work(&left, B2, &w);
    double left_ref[16];
    la_matmul(n, B2, B1, left_ref);

    udv_rmul(&left, R1, &w);
    double rmul_ref[16];
    la_matmul(n, left_ref, R1, rmul_ref);
    double got[16];
    udv_product(&left, got);
    check_matrix_close(n, got, rmul_ref, 1e-12);
    check_orthogonal(&left, 1e-10);
    check_unit_upper(&left, 1e-10);
    check_det_sign(n, &left, rmul_ref, &w);
    check_inv_one_plus(n, &left, rmul_ref, &w, 1e-11);

    UDV l;
    UDV r;
    UDV c;
    udv_init(&l, n);
    udv_init(&r, n);
    udv_init(&c, n);
    udv_lmul_work(&l, B1, &w);
    udv_lmul_work(&l, B2, &w);
    udv_lmul_work(&r, R1, &w);
    udv_lmul_work(&r, R2, &w);
    double r_ref[16];
    double cref[16];
    la_matmul(n, R2, R1, r_ref);
    la_matmul(n, left_ref, r_ref, cref);
    udv_combine(&l, &r, &c, &w);
    udv_product(&c, got);
    check_matrix_close(n, got, cref, 1e-12);
    check_orthogonal(&c, 1e-10);
    check_unit_upper(&c, 1e-10);
    check_det_sign(n, &c, cref, &w);
    check_inv_one_plus(n, &c, cref, &w, 1e-11);

    LinalgWork guard_work;
    linalg_work_init(&guard_work, n);
    CHECK(guard_work.ok);
    UDV guard_out;
    udv_init(&guard_out, n);
    guard_out.D[0] = 7.0;
    l.log_offset = 1.0;
    udv_combine(&l, &r, &guard_out, &guard_work);
    CHECK(guard_work.failed);
    CHECK_CLOSE(guard_out.D[0], 7.0, 0.0);
    l.log_offset = 0.0;
    udv_free(&guard_out);
    linalg_work_free(&guard_work);

    UDV bad;
    udv_init(&bad, n);
    double S1[16] = {1e10, 0, 0, 0, 0, 1e4, 0, 0,
                     0, 0, 1e-4, 0, 0, 0, 0, 1e-10};
    double Mix[16] = {1.0, 0.2, -0.1, 0.05, -0.3, 1.0, 0.4, 0.1,
                      0.15, -0.25, 1.0, 0.2, 0.05, 0.1, -0.2, 1.0};
    double S2[16] = {1e-8, 0, 0, 0, 0, 1e2, 0, 0,
                     0, 0, 1e-2, 0, 0, 0, 0, 1e8};
    udv_lmul_work(&bad, S1, &w);
    udv_lmul_work(&bad, Mix, &w);
    udv_rmul(&bad, S2, &w);
    double tmp[16];
    double bad_ref[16];
    la_matmul(n, Mix, S1, tmp);
    la_matmul(n, tmp, S2, bad_ref);
    udv_product(&bad, got);
    check_matrix_close(n, got, bad_ref, 1e-8);
    check_inv_one_plus(n, &bad, bad_ref, &w, 1e-7);

    UDV centered;
    udv_init(&centered, n);
    udv_lmul_centered_work(&centered, B1, &w);
    udv_lmul_centered_work(&centered, B2, &w);
    udv_rmul_centered(&centered, R1, &w);
    CHECK(!w.failed);
    CHECK(isfinite(centered.log_offset));
    udv_product(&centered, got);
    check_matrix_close(n, got, rmul_ref, 1e-12);

    double Grow[16] = {exp(3.0), 0, 0, 0, 0, exp(2.0), 0, 0,
                       0, 0, 1.0, 0, 0, 0, 0, exp(-1.0)};
    for (int k = 0; k < 200; k++) {
        udv_lmul_centered_work(&centered, Grow, &w);
        CHECK(!w.failed);
        CHECK(isfinite(centered.log_offset));
        for (int i = 0; i < n; i++) {
            CHECK(isfinite(centered.D[i]));
            CHECK(centered.D[i] != 0.0);
        }
        double lo = log(fabs(centered.D[0]));
        double hi = lo;
        for (int i = 1; i < n; i++) {
            const double ld = log(fabs(centered.D[i]));
            if (ld < lo) lo = ld;
            if (ld > hi) hi = ld;
        }
        CHECK_CLOSE(lo + hi, 0.0, 1e-9);
    }
    CHECK(fabs(centered.log_offset) > 1.0);
    udv_free(&centered);

    double near_wall_D[4] = {exp(700.0), exp(-700.0), 1.0, 1.0};
    double amplify[16] = {exp(2.0), 0, 0, 0, 0, exp(2.0), 0, 0,
                          0, 0, exp(2.0), 0, 0, 0, 0, exp(2.0)};
    UDV near_wall;
    udv_init(&near_wall, n);
    memcpy(near_wall.D, near_wall_D, sizeof(near_wall_D));
    double before_U[16];
    double before_D[4];
    double before_T[16];
    memcpy(before_U, near_wall.U, sizeof(before_U));
    memcpy(before_D, near_wall.D, sizeof(before_D));
    memcpy(before_T, near_wall.T, sizeof(before_T));
    const double before_offset = near_wall.log_offset;

    LinalgWork margin_work;
    linalg_work_init(&margin_work, n);
    CHECK(margin_work.ok);
    udv_lmul_centered_work(&near_wall, amplify, &margin_work);
    CHECK(margin_work.failed);
    CHECK(margin_work.failure_reason ==
          LINALG_FAILURE_CENTERED_UPDATE_MARGIN);
    CHECK(memcmp(near_wall.U, before_U, sizeof(before_U)) == 0);
    CHECK(memcmp(near_wall.D, before_D, sizeof(before_D)) == 0);
    CHECK(memcmp(near_wall.T, before_T, sizeof(before_T)) == 0);
    CHECK_CLOSE(near_wall.log_offset, before_offset, 0.0);
    linalg_work_free(&margin_work);

    linalg_work_init(&margin_work, n);
    CHECK(margin_work.ok);
    udv_rmul_centered(&near_wall, amplify, &margin_work);
    CHECK(margin_work.failed);
    CHECK(margin_work.failure_reason ==
          LINALG_FAILURE_CENTERED_UPDATE_MARGIN);
    CHECK(memcmp(near_wall.U, before_U, sizeof(before_U)) == 0);
    CHECK(memcmp(near_wall.D, before_D, sizeof(before_D)) == 0);
    CHECK(memcmp(near_wall.T, before_T, sizeof(before_T)) == 0);
    CHECK_CLOSE(near_wall.log_offset, before_offset, 0.0);
    linalg_work_free(&margin_work);
    udv_free(&near_wall);

    udv_free(&bad);
    udv_free(&c);
    udv_free(&r);
    udv_free(&l);
    udv_free(&left);
    linalg_work_free(&w);
    TEST_END();
}
