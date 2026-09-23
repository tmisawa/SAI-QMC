#include "test_util.h"
#include "linalg.h"

#include <math.h>
#include <string.h>

static void udv_product(const UDV *s, double *out)
{
    const int n = s->n;
    double UD[9];
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            UD[i + j * n] = s->U[i + j * n] * s->D[j];
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
    const double rel = sqrt(num) / (sqrt(den) + 1e-300);
    CHECK(rel < tol);
}

static void check_inverse_residual(int n, const double *A, const double *g,
                                   double tol)
{
    double R[9];
    la_matmul(n, A, g, R);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            CHECK_CLOSE(R[i + j * n], (i == j) ? 1.0 : 0.0, tol);
        }
    }
}

static void check_matrix_finite(int n, const double *A)
{
    for (int i = 0; i < n * n; i++) {
        CHECK(isfinite(A[i]));
    }
}

static void one_plus_lr(int n, const UDV *l, const UDV *r, double *out)
{
    double L[9];
    double R[9];
    udv_product(l, L);
    udv_product(r, R);
    la_matmul(n, L, R, out);
    for (int i = 0; i < n; i++) {
        out[i + i * n] += 1.0;
    }
}

static void check_two_sided_vs_combine(const UDV *l, const UDV *r,
                                       double tol)
{
    const int n = l->n;
    LinalgWork w;
    linalg_work_init(&w, n);
    CHECK(w.ok);

    double g[9];
    int ds = 0;
    CHECK(udv_inv_one_plus_two_sided_work(l, r, g, &ds, &w) == 0);
    CHECK(w.failed == 0);

    UDV c;
    udv_init(&c, n);
    udv_combine(l, r, &c, &w);
    CHECK(w.failed == 0);
    double gc[9];
    int dc = 0;
    CHECK(udv_inv_one_plus_work(&c, gc, &dc, &w) == 0);
    CHECK(dc == ds);
    check_matrix_close(n, g, gc, tol);

    udv_free(&c);
    linalg_work_free(&w);
}

static void check_combine_fails_but_two_sided_is_finite(const UDV *l,
                                                        const UDV *r)
{
    const int n = l->n;
    LinalgWork w;
    linalg_work_init(&w, n);
    CHECK(w.ok);

    double g[9];
    int ds = 0;
    CHECK(udv_inv_one_plus_two_sided_work(l, r, g, &ds, &w) == 0);
    CHECK(w.failed == 0);
    CHECK(ds == 1 || ds == -1);
    check_matrix_finite(n, g);
    linalg_work_free(&w);

    linalg_work_init(&w, n);
    CHECK(w.ok);
    UDV c;
    udv_init(&c, n);
    udv_combine(l, r, &c, &w);
    CHECK(w.failed == 1);
    udv_free(&c);
    linalg_work_free(&w);
}

static void set_manual_udv(UDV *s, double angle, const double *D, double t01,
                           double t02, double t12)
{
    const double c = cos(angle);
    const double q = sin(angle);
    s->U[0] = c;
    s->U[1] = q;
    s->U[2] = 0.0;
    s->U[3] = -q;
    s->U[4] = c;
    s->U[5] = 0.0;
    s->U[6] = 0.0;
    s->U[7] = 0.0;
    s->U[8] = 1.0;

    s->D[0] = D[0];
    s->D[1] = D[1];
    s->D[2] = D[2];

    s->T[0] = 1.0;
    s->T[1] = 0.0;
    s->T[2] = 0.0;
    s->T[3] = t01;
    s->T[4] = 1.0;
    s->T[5] = 0.0;
    s->T[6] = t02;
    s->T[7] = t12;
    s->T[8] = 1.0;
    s->log_offset = 0.0;
}

static void check_two_sided_dense(const UDV *l, const UDV *r, double g_tol,
                                  double residual_tol, int compare_combine)
{
    const int n = l->n;
    LinalgWork w;
    linalg_work_init(&w, n);
    CHECK(w.ok);

    double g[9];
    int ds = 0;
    const int rc = udv_inv_one_plus_two_sided_work(l, r, g, &ds, &w);
    CHECK(rc == 0);
    CHECK(w.failed == 0);
    if (rc != 0) {
        linalg_work_free(&w);
        return;
    }

    double A[9];
    double Ainv[9];
    one_plus_lr(n, l, r, A);
    CHECK(la_inverse_work(n, A, Ainv, &w) == 0);
    check_matrix_close(n, g, Ainv, g_tol);
    check_inverse_residual(n, A, g, residual_tol);

    double Acopy[9];
    memcpy(Acopy, A, sizeof(Acopy));
    int sref = 0;
    double logabs = 0.0;
    CHECK(la_logdet_work(n, Acopy, &sref, &logabs, &w) == 0);
    CHECK(isfinite(logabs));
    CHECK(ds == sref);
    CHECK(ds == 1 || ds == -1);

    if (compare_combine) {
        UDV c;
        udv_init(&c, n);
        udv_combine(l, r, &c, &w);
        CHECK(w.failed == 0);
        double gc[9];
        int dc = 0;
        CHECK(udv_inv_one_plus_work(&c, gc, &dc, &w) == 0);
        CHECK(dc == ds);
        check_matrix_close(n, g, gc, g_tol);
        udv_free(&c);
    }

    linalg_work_free(&w);
}

static void check_two_sided_dense_sign(const UDV *l, const UDV *r,
                                       double g_tol, int expected_sign)
{
    const int n = l->n;
    LinalgWork w;
    linalg_work_init(&w, n);
    CHECK(w.ok);

    double g[9];
    int ds = 0;
    CHECK(udv_inv_one_plus_two_sided_work(l, r, g, &ds, &w) == 0);
    CHECK(w.failed == 0);
    CHECK(ds == expected_sign);

    double A[9];
    double Ainv[9];
    one_plus_lr(n, l, r, A);
    CHECK(la_inverse_work(n, A, Ainv, &w) == 0);
    check_matrix_close(n, g, Ainv, g_tol);

    double Acopy[9];
    memcpy(Acopy, A, sizeof(Acopy));
    int sref = 0;
    double logabs = 0.0;
    CHECK(la_logdet_work(n, Acopy, &sref, &logabs, &w) == 0);
    CHECK(isfinite(logabs));
    CHECK(ds == sref);

    linalg_work_free(&w);
}

int main(void)
{
    const int n = 3;

    LinalgWork w;
    linalg_work_init(&w, n);
    CHECK(w.ok);

    UDV l;
    UDV r;
    udv_init(&l, n);
    udv_init(&r, n);

    double B1[9] = {1.2, 0.1, 0.0, 0.3, 0.9, 0.2, 0.0, 0.1, 1.1};
    double B2[9] = {0.8, 0.0, 0.2, 0.1, 1.3, 0.0, 0.3, 0.2, 0.7};
    double R1[9] = {1.0, 0.2, 0.1, 0.0, 0.6, 0.3, 0.2, 0.0, 1.4};
    double R2[9] = {0.7, 0.1, 0.0, 0.2, 1.1, 0.2, 0.1, 0.0, 1.2};
    udv_lmul_work(&l, B1, &w);
    udv_lmul_work(&l, B2, &w);
    udv_lmul_work(&r, R1, &w);
    udv_lmul_work(&r, R2, &w);
    CHECK(w.failed == 0);
    check_two_sided_dense(&l, &r, 1e-11, 1e-10, 1);

    UDV ls;
    UDV rs;
    udv_init(&ls, n);
    udv_init(&rs, n);
    udv_copy(&ls, &l);
    udv_copy(&rs, &r);
    const double lshift = 4.0;
    const double rshift = -3.0;
    for (int i = 0; i < n; i++) {
        ls.D[i] *= exp(-lshift);
        rs.D[i] *= exp(-rshift);
    }
    ls.log_offset = lshift;
    rs.log_offset = rshift;
    double g0[9];
    double gs[9];
    int d0 = 0;
    int ds = 0;
    CHECK(udv_inv_one_plus_two_sided_work(&l, &r, g0, &d0, &w) == 0);
    CHECK(udv_inv_one_plus_two_sided_work(&ls, &rs, gs, &ds, &w) == 0);
    CHECK(d0 == ds);
    for (int i = 0; i < n * n; i++) CHECK_CLOSE(g0[i], gs[i], 1e-12);
    udv_free(&rs);
    udv_free(&ls);

    const double one_sided_l[3] = {exp(600.0), exp(600.0), exp(600.0)};
    const double one_sided_r[3] = {0.7, 1.3, 0.5};
    set_manual_udv(&l, 0.05, one_sided_l, 0.02, -0.01, 0.03);
    set_manual_udv(&r, -0.08, one_sided_r, -0.04, 0.02, -0.01);
    check_two_sided_dense(&l, &r, 1e-8, 1e-7, 0);

    const double two_sided_l[3] = {exp(300.0), exp(300.0), exp(300.0)};
    const double two_sided_r[3] = {exp(300.0), exp(300.0), exp(300.0)};
    set_manual_udv(&l, 0.03, two_sided_l, 0.01, 0.02, -0.02);
    set_manual_udv(&r, -0.04, two_sided_r, -0.02, 0.01, 0.03);
    check_two_sided_dense(&l, &r, 1e-8, 1e-7, 0);

    const double graded250[3] = {exp(250.0), 1.0, exp(-250.0)};
    set_manual_udv(&l, 0.06, graded250, 0.02, -0.01, 0.03);
    set_manual_udv(&r, -0.05, graded250, -0.03, 0.02, -0.01);
    /* Dense inverse is cancellation-dominated here; compare the two stable
       paths directly while the cross scale remains below overflow. */
    check_two_sided_vs_combine(&l, &r, 1e-12);

    const double graded400[3] = {exp(400.0), 1.0, exp(-400.0)};
    set_manual_udv(&l, 0.0, graded400, 0.0, 0.0, 0.0);
    set_manual_udv(&r, 0.0, graded400, 0.0, 0.0, 0.0);

    udv_init(&ls, n);
    udv_init(&rs, n);
    udv_copy(&ls, &l);
    udv_copy(&rs, &r);
    ls.log_offset = 50.0;
    rs.log_offset = -40.0;
    for (int i = 0; i < n; i++) {
        ls.D[i] *= exp(-ls.log_offset);
        rs.D[i] *= exp(-rs.log_offset);
    }
    CHECK(udv_inv_one_plus_two_sided_work(&l, &r, g0, &d0, &w) == 0);
    CHECK(udv_inv_one_plus_two_sided_work(&ls, &r, gs, &ds, &w) == 0);
    CHECK(d0 == ds);
    check_matrix_close(n, gs, g0, 1e-12);
    CHECK(udv_inv_one_plus_two_sided_work(&ls, &rs, gs, &ds, &w) == 0);
    CHECK(d0 == ds);
    check_matrix_close(n, gs, g0, 1e-12);
    udv_free(&rs);
    udv_free(&ls);

    check_combine_fails_but_two_sided_is_finite(&l, &r);

    const double neg_l[3] = {-3.0, 0.5, 2.0};
    const double neg_r[3] = {1.5, 0.4, 2.5};
    set_manual_udv(&l, 0.07, neg_l, 0.01, -0.02, 0.03);
    set_manual_udv(&r, -0.02, neg_r, -0.01, 0.02, -0.03);
    check_two_sided_dense_sign(&l, &r, 1e-12, -1);

    udv_init(&ls, n);
    udv_init(&rs, n);
    udv_copy(&ls, &l);
    udv_copy(&rs, &r);
    ls.log_offset = 2.0;
    rs.log_offset = -2.0;
    for (int i = 0; i < n; i++) {
        ls.D[i] *= exp(-ls.log_offset);
        rs.D[i] *= exp(-rs.log_offset);
    }
    CHECK(fabs(ls.D[0]) < 1.0); /* stored small, effective big and negative */
    CHECK(fabs(rs.D[1]) > 1.0); /* stored big, effective small */
    CHECK(udv_inv_one_plus_two_sided_work(&l, &r, g0, &d0, &w) == 0);
    CHECK(udv_inv_one_plus_two_sided_work(&ls, &rs, gs, &ds, &w) == 0);
    CHECK(d0 == -1);
    CHECK(ds == d0);
    check_matrix_close(n, gs, g0, 1e-12);
    udv_free(&rs);
    udv_free(&ls);

    l.log_offset = 800.0;
    CHECK(udv_inv_one_plus_two_sided_work(&l, &r, g0, &d0, &w) != 0);
    CHECK(w.failed);
    CHECK(w.failure_reason == LINALG_FAILURE_EFFECTIVE_LOG_RANGE);

    udv_free(&r);
    udv_free(&l);
    linalg_work_free(&w);
    TEST_END();
}
