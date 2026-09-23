#include "linalg.h"

#include "profiler.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *linalg_failure_reason_string(int reason)
{
    switch (reason) {
    case LINALG_FAILURE_NONE:
        return "none";
    case LINALG_FAILURE_CENTERED_RADIUS:
        return "centered_radius";
    case LINALG_FAILURE_CENTERED_UPDATE_MARGIN:
        return "centered_update_margin";
    case LINALG_FAILURE_EFFECTIVE_LOG_RANGE:
        return "effective_log_range";
    default:
        return "unknown";
    }
}

extern void dgemm_(const char *, const char *, const int *, const int *,
                   const int *, const double *, const double *, const int *,
                   const double *, const int *, const double *, double *,
                   const int *);
extern void dgetrf_(const int *, const int *, double *, const int *, int *,
                    int *);
extern void dgetri_(const int *, double *, const int *, const int *, double *,
                    const int *, int *);
extern void dsyev_(const char *, const char *, const int *, double *,
                   const int *, double *, double *, const int *, int *);
extern void dgeqrf_(const int *, const int *, double *, const int *, double *,
                    double *, const int *, int *);
extern void dorgqr_(const int *, const int *, const int *, double *,
                    const int *, const double *, double *, const int *, int *);
extern void dtrtri_(const char *, const char *, const int *, double *,
                    const int *, int *);

static int check_vector_finite(const char *func, const char *stage, int n,
                               const double *x)
{
    if (n <= 0 || x == NULL) {
        fprintf(stderr,
                "ERROR: %s invalid vector at stage=%s n=%d ptr=%p\n", func,
                stage, n, (const void *)x);
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (!isfinite(x[i])) {
            fprintf(stderr,
                    "ERROR: %s non-finite vector at stage=%s index=%d "
                    "value=%.17g n=%d\n",
                    func, stage, i, x[i], n);
            return 1;
        }
    }
    return 0;
}

static int check_vector_nonzero(const char *func, const char *stage, int n,
                                const double *x)
{
    if (n <= 0 || x == NULL) {
        fprintf(stderr,
                "ERROR: %s invalid nonzero-vector check at stage=%s n=%d "
                "ptr=%p\n",
                func, stage, n, (const void *)x);
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (x[i] == 0.0) {
            fprintf(stderr,
                    "ERROR: %s zero vector entry at stage=%s index=%d n=%d\n",
                    func, stage, i, n);
            return 1;
        }
    }
    return 0;
}

static int check_matrix_finite(const char *func, const char *stage, int n,
                               const double *A)
{
    if (n <= 0 || A == NULL) {
        fprintf(stderr,
                "ERROR: %s invalid matrix at stage=%s n=%d ptr=%p\n", func,
                stage, n, (const void *)A);
        return 1;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            const double v = A[i + j * n];
            if (!isfinite(v)) {
                fprintf(stderr,
                        "ERROR: %s non-finite matrix at stage=%s row=%d "
                        "col=%d value=%.17g n=%d\n",
                        func, stage, i, j, v, n);
                return 1;
            }
        }
    }
    return 0;
}

static double logaddexp_pair(double a, double b)
{
    if (a == -INFINITY) return b;
    if (b == -INFINITY) return a;
    const double hi = (a > b) ? a : b;
    const double lo = (a > b) ? b : a;
    return hi + log1p(exp(lo - hi));
}

/* Conservative bound for the largest QR-input entry formed by a centered
   update, without forming a product that could itself overflow. */
static int centered_update_has_margin(const UDV *s, const double *B,
                                      int left, double safety_margin)
{
    const int n = s->n;
    const double limit = log(DBL_MAX) - safety_margin;
    double worst = -INFINITY;

    if (left) {
        /* |(B U)_ij| <= sum_k |B_ik| because U is orthogonal. */
        double log_bnorm = -INFINITY;
        for (int i = 0; i < n; i++) {
            double row = -INFINITY;
            for (int k = 0; k < n; k++) {
                const double b = fabs(B[i + k * n]);
                if (b != 0.0) row = logaddexp_pair(row, log(b));
            }
            if (row > log_bnorm) log_bnorm = row;
        }
        for (int j = 0; j < n; j++) {
            const double candidate = log(fabs(s->D[j])) + log_bnorm;
            if (candidate > worst) worst = candidate;
        }
    } else {
        /* |(T B)_ij| <= sum_k |T_ik| max_j |B_kj|. */
        for (int i = 0; i < n; i++) {
            double row = -INFINITY;
            for (int k = 0; k < n; k++) {
                double bmax = 0.0;
                for (int j = 0; j < n; j++) {
                    const double b = fabs(B[k + j * n]);
                    if (b > bmax) bmax = b;
                }
                const double t = fabs(s->T[i + k * n]);
                if (t != 0.0 && bmax != 0.0) {
                    row = logaddexp_pair(row, log(t) + log(bmax));
                }
            }
            const double candidate = log(fabs(s->D[i])) + row;
            if (candidate > worst) worst = candidate;
        }
    }

    return isfinite(worst) && worst <= limit;
}

static int effective_log_representable(const char *func, const char *side,
                                       int index, double ell, LinalgWork *w)
{
    const double split_exponent = -fabs(ell);
    const double minimum = log(DBL_TRUE_MIN);
    if (!isfinite(ell) || split_exponent < minimum) {
        fprintf(stderr,
                "ERROR: %s effective-log range exceeded side=%s index=%d "
                "ell=%.17g split_exponent=%.17g minimum=%.17g\n",
                func, side, index, ell, split_exponent, minimum);
        if (w != NULL) {
            w->failure_reason = LINALG_FAILURE_EFFECTIVE_LOG_RANGE;
        }
        return 0;
    }
    return 1;
}

void la_eye(int n, double *A)
{
    memset(A, 0, sizeof(double) * (size_t)n * (size_t)n);
    for (int i = 0; i < n; i++) {
        A[i + i * n] = 1.0;
    }
}

void la_gemm(int n, int ta, int tb, double alpha, const double *A,
             const double *B, double beta, double *C)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_la_gemm);
    const char transa = ta ? 'T' : 'N';
    const char transb = tb ? 'T' : 'N';
    dgemm_(&transa, &transb, &n, &n, &n, &alpha, A, &n, B, &n, &beta, C,
           &n);
    PROF_END(prof, PROF_LA_GEMM, t_la_gemm);
}

void la_matmul(int n, const double *A, const double *B, double *C)
{
    la_gemm(n, 0, 0, 1.0, A, B, 0.0, C);
}

void la_gemm_nt_rect(int n, int k, double alpha, const double *A,
                     const double *B, double beta, double *C)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_la_gemm);
    const char transa = 'N';
    const char transb = 'T';
    dgemm_(&transa, &transb, &n, &n, &k, &alpha, A, &n, B, &n, &beta, C,
           &n);
    PROF_END(prof, PROF_LA_GEMM, t_la_gemm);
}

int la_inverse(int n, const double *A, double *Ainv)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_la_inverse);
    int info = 0;
    int *ipiv = malloc(sizeof(int) * (size_t)n);
    if (ipiv == NULL) {
        PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);
        return -1;
    }

    memcpy(Ainv, A, sizeof(double) * (size_t)n * (size_t)n);
    dgetrf_(&n, &n, Ainv, &n, ipiv, &info);
    if (info != 0) {
        free(ipiv);
        PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);
        return info;
    }

    int lwork = n * 64;
    double *work = malloc(sizeof(double) * (size_t)lwork);
    if (work == NULL) {
        free(ipiv);
        PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);
        return -1;
    }
    dgetri_(&n, Ainv, &n, ipiv, work, &lwork, &info);

    free(work);
    free(ipiv);
    PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);
    return info;
}

int la_logdet(int n, double *A, int *sign, double *logabs)
{
    int info = 0;
    int *ipiv = malloc(sizeof(int) * (size_t)n);
    if (ipiv == NULL) {
        return -1;
    }

    dgetrf_(&n, &n, A, &n, ipiv, &info);
    if (info != 0) {
        free(ipiv);
        return info;
    }

    int s = 1;
    double la = 0.0;
    for (int i = 0; i < n; i++) {
        const double d = A[i + i * n];
        if (d < 0.0) {
            s = -s;
        }
        la += log(fabs(d));
        if (ipiv[i] != i + 1) {
            s = -s;
        }
    }

    *sign = s;
    *logabs = la;
    free(ipiv);
    return 0;
}

int la_logdet_work(int n, double *A, int *sign, double *logabs,
                   LinalgWork *w)
{
    if (w == NULL || !w->ok || w->n != n || w->ipiv == NULL) {
        return la_logdet(n, A, sign, logabs);
    }

    int info = 0;
    dgetrf_(&n, &n, A, &n, w->ipiv, &info);
    if (info != 0) {
        return info;
    }

    int s = 1;
    double la = 0.0;
    for (int i = 0; i < n; i++) {
        const double d = A[i + i * n];
        if (d < 0.0) {
            s = -s;
        }
        la += log(fabs(d));
        if (w->ipiv[i] != i + 1) {
            s = -s;
        }
    }

    *sign = s;
    *logabs = la;
    return 0;
}

void la_expm_sym(int n, const double *A, double scale, double *expA)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_la_expm_sym);
    int info = 0;
    int lwork = n * 64;
    double *V = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *w = malloc(sizeof(double) * (size_t)n);
    double *work = malloc(sizeof(double) * (size_t)lwork);
    double *VD = malloc(sizeof(double) * (size_t)n * (size_t)n);

    memcpy(V, A, sizeof(double) * (size_t)n * (size_t)n);
    dsyev_("V", "U", &n, V, &n, w, work, &lwork, &info);

    for (int j = 0; j < n; j++) {
        const double f = exp(scale * w[j]);
        for (int i = 0; i < n; i++) {
            VD[i + j * n] = V[i + j * n] * f;
        }
    }
    la_gemm(n, 0, 1, 1.0, VD, V, 0.0, expA);

    free(VD);
    free(work);
    free(w);
    free(V);
    PROF_END(prof, PROF_LA_EXPM_SYM, t_la_expm_sym);
}

void linalg_work_init(LinalgWork *w, int n)
{
    memset(w, 0, sizeof(*w));
    w->n = n;
    w->lwork = n * 64;
    const size_t nn = (size_t)n * (size_t)n;
    w->M = malloc(sizeof(double) * nn);
    w->A = malloc(sizeof(double) * nn);
    w->B = malloc(sizeof(double) * nn);
    w->C = malloc(sizeof(double) * nn);
    w->Dmat = malloc(sizeof(double) * nn);
    w->E = malloc(sizeof(double) * nn);
    w->F = malloc(sizeof(double) * nn);
    w->R = malloc(sizeof(double) * nn);
    w->Ttmp = malloc(sizeof(double) * nn);
    w->v1 = malloc(sizeof(double) * (size_t)n);
    w->v2 = malloc(sizeof(double) * (size_t)n);
    w->tau = malloc(sizeof(double) * (size_t)n);
    w->work = malloc(sizeof(double) * (size_t)w->lwork);
    w->ipiv = malloc(sizeof(int) * (size_t)n);
    w->ok = (w->M != NULL && w->A != NULL && w->B != NULL &&
             w->C != NULL && w->Dmat != NULL && w->E != NULL &&
             w->F != NULL && w->R != NULL && w->Ttmp != NULL &&
             w->v1 != NULL && w->v2 != NULL && w->tau != NULL &&
             w->work != NULL && w->ipiv != NULL);
}

void linalg_work_free(LinalgWork *w)
{
    free(w->M);
    free(w->A);
    free(w->B);
    free(w->C);
    free(w->Dmat);
    free(w->E);
    free(w->F);
    free(w->R);
    free(w->Ttmp);
    free(w->v1);
    free(w->v2);
    free(w->tau);
    free(w->work);
    free(w->ipiv);
    memset(w, 0, sizeof(*w));
}

int la_inverse_work(int n, const double *A, double *Ainv, LinalgWork *w)
{
    if (w == NULL || !w->ok || w->n != n || w->ipiv == NULL ||
        w->work == NULL) {
        return la_inverse(n, A, Ainv);
    }

    int info = 0;
    memcpy(Ainv, A, sizeof(double) * (size_t)n * (size_t)n);
    dgetrf_(&n, &n, Ainv, &n, w->ipiv, &info);
    if (info != 0) {
        return info;
    }
    dgetri_(&n, Ainv, &n, w->ipiv, w->work, &w->lwork, &info);
    return info;
}

void udv_init(UDV *s, int n)
{
    s->n = n;
    s->U = malloc(sizeof(double) * (size_t)n * (size_t)n);
    s->D = malloc(sizeof(double) * (size_t)n);
    s->T = malloc(sizeof(double) * (size_t)n * (size_t)n);

    udv_identity(s);
}

void udv_free(UDV *s)
{
    free(s->U);
    free(s->D);
    free(s->T);
    s->n = 0;
    s->U = NULL;
    s->D = NULL;
    s->T = NULL;
}

void udv_identity(UDV *s)
{
    const int n = s->n;
    la_eye(n, s->U);
    la_eye(n, s->T);
    for (int i = 0; i < n; i++) {
        s->D[i] = 1.0;
    }
    s->log_offset = 0.0;
}

void udv_copy(UDV *dst, const UDV *src)
{
    const int n = src->n;
    dst->n = n;
    memcpy(dst->U, src->U, sizeof(double) * (size_t)n * (size_t)n);
    memcpy(dst->D, src->D, sizeof(double) * (size_t)n);
    memcpy(dst->T, src->T, sizeof(double) * (size_t)n * (size_t)n);
    dst->log_offset = src->log_offset;
}

int udv_recenter(UDV *s, double hard_margin, double *radius,
                 double *remaining_margin)
{
    if (radius != NULL) {
        *radius = NAN;
    }
    if (remaining_margin != NULL) {
        *remaining_margin = NAN;
    }
    if (s == NULL || s->n <= 0 || s->D == NULL || hard_margin < 0.0 ||
        !isfinite(hard_margin) || !isfinite(s->log_offset)) {
        return 1;
    }

    double lo = 0.0;
    double hi = 0.0;
    for (int i = 0; i < s->n; i++) {
        const double d = s->D[i];
        if (!isfinite(d) || d == 0.0) {
            return 1;
        }
        const double ld = log(fabs(d));
        if (!isfinite(ld)) {
            return 1;
        }
        if (i == 0 || ld < lo) {
            lo = ld;
        }
        if (i == 0 || ld > hi) {
            hi = ld;
        }
    }

    const double center = lo + 0.5 * (hi - lo);
    const double r = 0.5 * (hi - lo);
    const double margin = log(DBL_MAX) - r;
    if (radius != NULL) {
        *radius = r;
    }
    if (remaining_margin != NULL) {
        *remaining_margin = margin;
    }
    if (!isfinite(center) || !isfinite(r) || !isfinite(margin) ||
        margin < hard_margin || !isfinite(s->log_offset + center)) {
        return 1;
    }

    for (int i = 0; i < s->n; i++) {
        const double d = s->D[i];
        const double centered = exp(log(fabs(d)) - center);
        if (!isfinite(centered) || centered == 0.0) {
            return 1;
        }
    }
    for (int i = 0; i < s->n; i++) {
        const double d = s->D[i];
        s->D[i] = copysign(exp(log(fabs(d)) - center), d);
    }
    s->log_offset += center;
    return 0;
}

static void udv_lmul_impl(UDV *s, const double *B, LinalgWork *w,
                          int centered)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_udv_lmul);
    const int n = s->n;
    if (w == NULL || !w->ok || w->n != n) {
        LinalgWork local;
        linalg_work_init(&local, n);
        if (local.ok) {
            udv_lmul_impl(s, B, &local, centered);
        }
        linalg_work_free(&local);
        PROF_END(prof, PROF_UDV_LMUL, t_udv_lmul);
        return;
    }
    if (w->failed) {
        PROF_END(prof, PROF_UDV_LMUL, t_udv_lmul);
        return;
    }
    if (check_matrix_finite("udv_lmul_work", "input_U", n, s->U) ||
        check_vector_finite("udv_lmul_work", "input_D", n, s->D) ||
        check_matrix_finite("udv_lmul_work", "input_T", n, s->T) ||
        check_matrix_finite("udv_lmul_work", "input_B", n, B)) {
        goto fail;
    }
    double prior_offset = s->log_offset;
    if (centered) {
        memcpy(w->v1, s->D, sizeof(double) * (size_t)n);
    }
    if (centered && udv_recenter(s, 8.0, NULL, NULL) != 0) {
        w->failed = 1;
        w->failure_reason = LINALG_FAILURE_CENTERED_RADIUS;
        PROF_END(prof, PROF_UDV_LMUL, t_udv_lmul);
        return;
    }
    if (centered && !centered_update_has_margin(s, B, 1, 8.0)) {
        memcpy(s->D, w->v1, sizeof(double) * (size_t)n);
        s->log_offset = prior_offset;
        w->failure_reason = LINALG_FAILURE_CENTERED_UPDATE_MARGIN;
        goto fail;
    }
    int info = 0;
    double *M = w->M;
    double *R = w->R;
    double *Tnew = w->Ttmp;

    la_matmul(n, B, s->U, M);
    if (check_matrix_finite("udv_lmul_work", "B_U", n, M)) {
        goto fail;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            M[i + j * n] *= s->D[j];
        }
    }
    if (check_matrix_finite("udv_lmul_work", "B_U_D", n, M)) {
        goto fail;
    }

    dgeqrf_(&n, &n, M, &n, w->tau, w->work, &w->lwork, &info);
    if (info != 0) {
        fprintf(stderr, "ERROR: udv_lmul_work dgeqrf failed info=%d n=%d\n",
                info, n);
        goto fail;
    }
    if (check_matrix_finite("udv_lmul_work", "qr_raw", n, M)) {
        goto fail;
    }

    memset(R, 0, sizeof(double) * (size_t)n * (size_t)n);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i <= j; i++) {
            R[i + j * n] = M[i + j * n];
        }
    }
    if (check_matrix_finite("udv_lmul_work", "R", n, R)) {
        goto fail;
    }

    dorgqr_(&n, &n, &n, M, &n, w->tau, w->work, &w->lwork, &info);
    if (info != 0) {
        fprintf(stderr, "ERROR: udv_lmul_work dorgqr failed info=%d n=%d\n",
                info, n);
        goto fail;
    }
    if (check_matrix_finite("udv_lmul_work", "Q", n, M)) {
        goto fail;
    }
    memcpy(s->U, M, sizeof(double) * (size_t)n * (size_t)n);

    for (int i = 0; i < n; i++) {
        s->D[i] = R[i + i * n];
    }
    if (check_vector_finite("udv_lmul_work", "D", n, s->D) ||
        check_vector_nonzero("udv_lmul_work", "D", n, s->D)) {
        goto fail;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            R[i + j * n] = (i <= j) ? R[i + j * n] / s->D[i] : 0.0;
        }
    }
    if (check_matrix_finite("udv_lmul_work", "R_unit", n, R)) {
        goto fail;
    }

    la_matmul(n, R, s->T, Tnew);
    if (check_matrix_finite("udv_lmul_work", "Tnew", n, Tnew)) {
        goto fail;
    }
    memcpy(s->T, Tnew, sizeof(double) * (size_t)n * (size_t)n);
    if (check_matrix_finite("udv_lmul_work", "output_U", n, s->U) ||
        check_vector_finite("udv_lmul_work", "output_D", n, s->D) ||
        check_matrix_finite("udv_lmul_work", "output_T", n, s->T)) {
        goto fail;
    }
    if (centered && udv_recenter(s, 8.0, NULL, NULL) != 0) {
        w->failure_reason = LINALG_FAILURE_CENTERED_RADIUS;
        goto fail;
    }

    goto done;
fail:
    w->failed = 1;
done:
    PROF_END(prof, PROF_UDV_LMUL, t_udv_lmul);
}

void udv_lmul_work(UDV *s, const double *B, LinalgWork *w)
{
    udv_lmul_impl(s, B, w, 0);
}

void udv_lmul_centered_work(UDV *s, const double *B, LinalgWork *w)
{
    udv_lmul_impl(s, B, w, 1);
}

void udv_lmul(UDV *s, const double *B)
{
    LinalgWork w;
    linalg_work_init(&w, s->n);
    udv_lmul_work(s, B, &w);
    linalg_work_free(&w);
}

static void udv_rmul_impl(UDV *s, const double *B, LinalgWork *w,
                          int centered)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_udv_rmul);
    const int n = s->n;
    if (w == NULL || !w->ok || w->n != n) {
        LinalgWork local;
        linalg_work_init(&local, n);
        if (local.ok) {
            udv_rmul_impl(s, B, &local, centered);
        }
        linalg_work_free(&local);
        PROF_END(prof, PROF_UDV_RMUL, t_udv_rmul);
        return;
    }
    if (w->failed) {
        PROF_END(prof, PROF_UDV_RMUL, t_udv_rmul);
        return;
    }
    if (check_matrix_finite("udv_rmul", "input_U", n, s->U) ||
        check_vector_finite("udv_rmul", "input_D", n, s->D) ||
        check_matrix_finite("udv_rmul", "input_T", n, s->T) ||
        check_matrix_finite("udv_rmul", "input_B", n, B)) {
        goto fail;
    }
    double prior_offset = s->log_offset;
    if (centered) {
        memcpy(w->v1, s->D, sizeof(double) * (size_t)n);
    }
    if (centered && udv_recenter(s, 8.0, NULL, NULL) != 0) {
        w->failed = 1;
        w->failure_reason = LINALG_FAILURE_CENTERED_RADIUS;
        PROF_END(prof, PROF_UDV_RMUL, t_udv_rmul);
        return;
    }
    if (centered && !centered_update_has_margin(s, B, 0, 8.0)) {
        memcpy(s->D, w->v1, sizeof(double) * (size_t)n);
        s->log_offset = prior_offset;
        w->failure_reason = LINALG_FAILURE_CENTERED_UPDATE_MARGIN;
        goto fail;
    }

    int info = 0;
    double *M = w->M;
    double *R = w->R;
    double *Unew = w->Ttmp;
    double *TP = w->A;

    la_matmul(n, s->T, B, TP);
    if (check_matrix_finite("udv_rmul", "T_B", n, TP)) {
        goto fail;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            M[i + j * n] = s->D[i] * TP[i + j * n];
        }
    }
    if (check_matrix_finite("udv_rmul", "D_T_B", n, M)) {
        goto fail;
    }

    dgeqrf_(&n, &n, M, &n, w->tau, w->work, &w->lwork, &info);
    if (info != 0) {
        fprintf(stderr, "ERROR: udv_rmul dgeqrf failed info=%d n=%d\n", info,
                n);
        goto fail;
    }
    if (check_matrix_finite("udv_rmul", "qr_raw", n, M)) {
        goto fail;
    }
    memset(R, 0, sizeof(double) * (size_t)n * (size_t)n);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i <= j; i++) {
            R[i + j * n] = M[i + j * n];
        }
    }
    if (check_matrix_finite("udv_rmul", "R", n, R)) {
        goto fail;
    }
    dorgqr_(&n, &n, &n, M, &n, w->tau, w->work, &w->lwork, &info);
    if (info != 0) {
        fprintf(stderr, "ERROR: udv_rmul dorgqr failed info=%d n=%d\n", info,
                n);
        goto fail;
    }
    if (check_matrix_finite("udv_rmul", "Q", n, M)) {
        goto fail;
    }

    la_matmul(n, s->U, M, Unew);
    if (check_matrix_finite("udv_rmul", "Unew", n, Unew)) {
        goto fail;
    }
    memcpy(s->U, Unew, sizeof(double) * (size_t)n * (size_t)n);

    for (int i = 0; i < n; i++) {
        s->D[i] = R[i + i * n];
    }
    if (check_vector_finite("udv_rmul", "D", n, s->D) ||
        check_vector_nonzero("udv_rmul", "D", n, s->D)) {
        goto fail;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            s->T[i + j * n] = (i <= j) ? R[i + j * n] / s->D[i] : 0.0;
        }
    }
    if (check_matrix_finite("udv_rmul", "output_U", n, s->U) ||
        check_vector_finite("udv_rmul", "output_D", n, s->D) ||
        check_matrix_finite("udv_rmul", "output_T", n, s->T)) {
        goto fail;
    }
    if (centered && udv_recenter(s, 8.0, NULL, NULL) != 0) {
        w->failure_reason = LINALG_FAILURE_CENTERED_RADIUS;
        goto fail;
    }
    goto done;
fail:
    w->failed = 1;
done:
    PROF_END(prof, PROF_UDV_RMUL, t_udv_rmul);
}

void udv_rmul(UDV *s, const double *B, LinalgWork *w)
{
    udv_rmul_impl(s, B, w, 0);
}

void udv_rmul_centered(UDV *s, const double *B, LinalgWork *w)
{
    udv_rmul_impl(s, B, w, 1);
}

void udv_combine(const UDV *l, const UDV *r, UDV *out, LinalgWork *w)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_udv_combine);
    const int n = l->n;
    if (w == NULL || !w->ok || w->n != n) {
        LinalgWork local;
        linalg_work_init(&local, n);
        if (local.ok) {
            udv_combine(l, r, out, &local);
        }
        linalg_work_free(&local);
        PROF_END(prof, PROF_UDV_COMBINE, t_udv_combine);
        return;
    }
    if (w->failed) {
        PROF_END(prof, PROF_UDV_COMBINE, t_udv_combine);
        return;
    }
    if (!isfinite(l->log_offset) || !isfinite(r->log_offset) ||
        l->log_offset != 0.0 || r->log_offset != 0.0) {
        fprintf(stderr,
                "ERROR: udv_combine does not support centered factors "
                "left_log_offset=%.17g right_log_offset=%.17g\n",
                l->log_offset, r->log_offset);
        w->failed = 1;
        PROF_END(prof, PROF_UDV_COMBINE, t_udv_combine);
        return;
    }

    int info = 0;
    double *TLUR = w->A;
    double *C = w->M;
    double *R = w->R;
    double *TC = w->Ttmp;

    if (check_matrix_finite("udv_combine", "left_U", n, l->U) ||
        check_vector_finite("udv_combine", "left_D", n, l->D) ||
        check_matrix_finite("udv_combine", "left_T", n, l->T) ||
        check_matrix_finite("udv_combine", "right_U", n, r->U) ||
        check_vector_finite("udv_combine", "right_D", n, r->D) ||
        check_matrix_finite("udv_combine", "right_T", n, r->T)) {
        goto fail;
    }
    la_matmul(n, l->T, r->U, TLUR);
    if (check_matrix_finite("udv_combine", "TLUR", n, TLUR)) {
        goto fail;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            C[i + j * n] = l->D[i] * TLUR[i + j * n] * r->D[j];
        }
    }
    if (check_matrix_finite("udv_combine", "C", n, C)) {
        goto fail;
    }

    dgeqrf_(&n, &n, C, &n, w->tau, w->work, &w->lwork, &info);
    if (info != 0) {
        fprintf(stderr, "ERROR: udv_combine dgeqrf failed info=%d n=%d\n",
                info, n);
        goto fail;
    }
    if (check_matrix_finite("udv_combine", "qr_raw", n, C)) {
        goto fail;
    }
    memset(R, 0, sizeof(double) * (size_t)n * (size_t)n);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i <= j; i++) {
            R[i + j * n] = C[i + j * n];
        }
    }
    if (check_matrix_finite("udv_combine", "R", n, R)) {
        goto fail;
    }
    dorgqr_(&n, &n, &n, C, &n, w->tau, w->work, &w->lwork, &info);
    if (info != 0) {
        fprintf(stderr, "ERROR: udv_combine dorgqr failed info=%d n=%d\n",
                info, n);
        goto fail;
    }
    if (check_matrix_finite("udv_combine", "Q", n, C)) {
        goto fail;
    }

    la_matmul(n, l->U, C, out->U);
    if (check_matrix_finite("udv_combine", "out_U", n, out->U)) {
        goto fail;
    }
    for (int i = 0; i < n; i++) {
        out->D[i] = R[i + i * n];
    }
    if (check_vector_finite("udv_combine", "out_D", n, out->D) ||
        check_vector_nonzero("udv_combine", "out_D", n, out->D)) {
        goto fail;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            TC[i + j * n] = (i <= j) ? R[i + j * n] / out->D[i] : 0.0;
        }
    }
    if (check_matrix_finite("udv_combine", "TC", n, TC)) {
        goto fail;
    }
    la_matmul(n, TC, r->T, out->T);
    if (check_matrix_finite("udv_combine", "out_T", n, out->T)) {
        goto fail;
    }
    goto done;
fail:
    w->failed = 1;
done:
    PROF_END(prof, PROF_UDV_COMBINE, t_udv_combine);
}

int udv_inv_one_plus_work(const UDV *s, double *g, int *det_sign,
                          LinalgWork *w)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_udv_inv_one_plus);
    if (det_sign != NULL) {
        *det_sign = 0;
    }
    if (s == NULL || g == NULL) {
        fprintf(stderr,
                "ERROR: udv_inv_one_plus_work invalid input s=%p g=%p\n",
                (const void *)s, (void *)g);
        PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
        return 1;
    }
    const int n = s->n;
    if (w == NULL || !w->ok || w->n != n) {
        LinalgWork local;
        linalg_work_init(&local, n);
        if (!local.ok) {
            if (det_sign != NULL) {
                *det_sign = 0;
            }
            linalg_work_free(&local);
            PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
            return 1;
        }
        const int rc = udv_inv_one_plus_work(s, g, det_sign, &local);
        linalg_work_free(&local);
        PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
        return rc;
    }
    double *Db = w->v1;
    double *Ds = w->v2;
    double *Tinv = w->A;
    double *UT_Tinv = w->B;
    double *M = w->C;
    double *Minv = w->Dmat;
    double *tmp = w->E;
    double *gp = w->F;
    const int centered = (s->log_offset != 0.0);

    if (w->failed) {
        goto fail;
    }
    if (check_matrix_finite("udv_inv_one_plus_work", "input_U", n, s->U) ||
        check_vector_finite("udv_inv_one_plus_work", "input_D", n, s->D) ||
        check_matrix_finite("udv_inv_one_plus_work", "input_T", n, s->T) ||
        !isfinite(s->log_offset)) {
        goto fail;
    }

    for (int i = 0; i < n; i++) {
        if (!centered && fabs(s->D[i]) > 1.0) {
            Db[i] = s->D[i];
            Ds[i] = 1.0;
        } else if (!centered) {
            Db[i] = 1.0;
            Ds[i] = s->D[i];
        } else {
            const double ell = log(fabs(s->D[i])) + s->log_offset;
            if (!effective_log_representable("udv_inv_one_plus_work",
                                             "single", i, ell, w)) {
                goto fail;
            }
            if (ell > 0.0) {
                Db[i] = copysign(exp(-ell), s->D[i]);
                Ds[i] = 1.0;
            } else {
                Db[i] = 1.0;
                Ds[i] = copysign(exp(ell), s->D[i]);
            }
        }
    }
    if (check_vector_finite("udv_inv_one_plus_work", "Db", n, Db) ||
        check_vector_finite("udv_inv_one_plus_work", "Ds", n, Ds) ||
        check_vector_nonzero("udv_inv_one_plus_work", "Db", n, Db) ||
        check_vector_nonzero("udv_inv_one_plus_work", "Ds", n, Ds)) {
        goto fail;
    }

    memcpy(Tinv, s->T, sizeof(double) * (size_t)n * (size_t)n);
    {
        const char uplo = 'U';
        const char diag = 'U';
        int info = 0;
        dtrtri_(&uplo, &diag, &n, Tinv, &n, &info);
        if (info != 0) {
            fprintf(stderr,
                    "ERROR: udv_inv_one_plus_work dtrtri failed info=%d n=%d\n",
                    info, n);
            goto fail;
        }
    }
    if (check_matrix_finite("udv_inv_one_plus_work", "Tinv", n, Tinv)) {
        goto fail;
    }

    la_gemm(n, 1, 0, 1.0, s->U, Tinv, 0.0, UT_Tinv);
    if (check_matrix_finite("udv_inv_one_plus_work", "UT_Tinv", n,
                            UT_Tinv)) {
        goto fail;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            M[i + j * n] = centered ? UT_Tinv[i + j * n] * Db[i]
                                      : UT_Tinv[i + j * n] / Db[i];
        }
    }
    for (int i = 0; i < n; i++) {
        M[i + i * n] += Ds[i];
    }
    if (check_matrix_finite("udv_inv_one_plus_work", "M", n, M)) {
        goto fail;
    }

    const int inv_info = la_inverse_work(n, M, Minv, w);
    if (inv_info != 0) {
        fprintf(stderr,
                "ERROR: udv_inv_one_plus_work la_inverse_work(M) failed "
                "info=%d n=%d\n",
                inv_info, n);
        goto fail;
    }
    if (check_matrix_finite("udv_inv_one_plus_work", "Minv", n, Minv)) {
        goto fail;
    }

    /* sign(det(1 + U D T)) = sign(det U) * sign(det Db) * sign(det M).
       (1 + U D T = U * Db * M * T with M = Db^{-1} U^T T^{-1} + Ds; T is unit
       upper-triangular so det T = 1.) U is orthogonal and M is well-conditioned,
       so their determinant signs are robust even at low temperature, unlike the
       ill-conditioned g itself. */
    if (det_sign != NULL) {
        int sgn = 1;
        for (int i = 0; i < n; i++) {
            const int big = centered
                                ? (log(fabs(s->D[i])) + s->log_offset > 0.0)
                                : (fabs(s->D[i]) > 1.0);
            if (big && s->D[i] < 0.0) {
                sgn = -sgn;
            }
        }
        int su = 0;
        int sm = 0;
        double logabs = 0.0;
        memcpy(UT_Tinv, s->U, sizeof(double) * (size_t)n * (size_t)n);
        const int iu = la_logdet_work(n, UT_Tinv, &su, &logabs, w);
        if (iu == 0 && !isfinite(logabs)) {
            fprintf(stderr,
                    "ERROR: udv_inv_one_plus_work non-finite logdet(U) "
                    "logabs=%.17g n=%d\n",
                    logabs, n);
            goto fail;
        }
        memcpy(UT_Tinv, M, sizeof(double) * (size_t)n * (size_t)n);
        const int im = la_logdet_work(n, UT_Tinv, &sm, &logabs, w);
        if (im == 0 && !isfinite(logabs)) {
            fprintf(stderr,
                    "ERROR: udv_inv_one_plus_work non-finite logdet(M) "
                    "logabs=%.17g n=%d\n",
                    logabs, n);
            goto fail;
        }
        if (iu != 0 || im != 0) {
            fprintf(stderr,
                    "ERROR: udv_inv_one_plus_work logdet failed iu=%d im=%d "
                    "n=%d\n",
                    iu, im, n);
            goto fail;
        }
        *det_sign = sgn * su * sm;
    }

    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            tmp[i + j * n] = centered ? Minv[i + j * n] * Db[j]
                                      : Minv[i + j * n] / Db[j];
        }
    }
    if (check_matrix_finite("udv_inv_one_plus_work", "tmp", n, tmp)) {
        goto fail;
    }
    la_gemm(n, 0, 1, 1.0, tmp, s->U, 0.0, gp);
    if (check_matrix_finite("udv_inv_one_plus_work", "gp", n, gp)) {
        goto fail;
    }
    la_matmul(n, Tinv, gp, g);
    if (check_matrix_finite("udv_inv_one_plus_work", "g", n, g)) {
        goto fail;
    }

    PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
    return 0;

fail:
    if (w != NULL && w->ok && w->n == n) {
        w->failed = 1;
    }
    if (det_sign != NULL) {
        *det_sign = 0;
    }
    PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
    return 1;
}

int udv_inv_one_plus_two_sided_work(const UDV *l, const UDV *r, double *g,
                                    int *det_sign, LinalgWork *w)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_udv_inv_one_plus);
    if (det_sign != NULL) {
        *det_sign = 0;
    }
    if (l == NULL || r == NULL || g == NULL) {
        fprintf(stderr,
                "ERROR: udv_inv_one_plus_two_sided_work invalid input "
                "l=%p r=%p g=%p\n",
                (const void *)l, (const void *)r, (void *)g);
        PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
        return 1;
    }
    const int n = l->n;
    if (r->n != n) {
        fprintf(stderr,
                "ERROR: udv_inv_one_plus_two_sided_work size mismatch "
                "l->n=%d r->n=%d\n",
                l->n, r->n);
        PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
        return 1;
    }
    if (w == NULL || !w->ok || w->n != n) {
        LinalgWork local;
        linalg_work_init(&local, n);
        if (!local.ok) {
            if (det_sign != NULL) {
                *det_sign = 0;
            }
            linalg_work_free(&local);
            PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
            return 1;
        }
        const int rc =
            udv_inv_one_plus_two_sided_work(l, r, g, det_sign, &local);
        linalg_work_free(&local);
        PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
        return rc;
    }

    double *Dlb = w->v1;
    double *Drb = w->v2;
    double *TLUR = w->A;
    double *Tinv = w->B;
    double *ULTinv = w->C;
    double *H = w->M;
    double *Hinv = w->Dmat;
    double *tmp = w->E;
    double *tmp2 = w->F;
    double *det_tmp = w->R;
    const int left_centered = (l->log_offset != 0.0);
    const int right_centered = (r->log_offset != 0.0);

    if (w->failed) {
        goto fail;
    }
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "left_U", n,
                            l->U) ||
        check_vector_finite("udv_inv_one_plus_two_sided_work", "left_D", n,
                            l->D) ||
        check_matrix_finite("udv_inv_one_plus_two_sided_work", "left_T", n,
                            l->T) ||
        check_matrix_finite("udv_inv_one_plus_two_sided_work", "right_U", n,
                            r->U) ||
        check_vector_finite("udv_inv_one_plus_two_sided_work", "right_D", n,
                            r->D) ||
        check_matrix_finite("udv_inv_one_plus_two_sided_work", "right_T", n,
                            r->T) ||
        !isfinite(l->log_offset) || !isfinite(r->log_offset)) {
        goto fail;
    }

    for (int i = 0; i < n; i++) {
        if (left_centered) {
            const double ell = log(fabs(l->D[i])) + l->log_offset;
            if (!effective_log_representable(
                    "udv_inv_one_plus_two_sided_work", "left", i, ell, w))
                goto fail;
            Dlb[i] = (ell > 0.0) ? copysign(exp(-ell), l->D[i]) : 1.0;
        } else {
            Dlb[i] = (fabs(l->D[i]) > 1.0) ? l->D[i] : 1.0;
        }
        if (right_centered) {
            const double ell = log(fabs(r->D[i])) + r->log_offset;
            if (!effective_log_representable(
                    "udv_inv_one_plus_two_sided_work", "right", i, ell, w))
                goto fail;
            Drb[i] = (ell > 0.0) ? copysign(exp(-ell), r->D[i]) : 1.0;
        } else {
            Drb[i] = (fabs(r->D[i]) > 1.0) ? r->D[i] : 1.0;
        }
    }
    if (check_vector_finite("udv_inv_one_plus_two_sided_work", "Dlb", n,
                            Dlb) ||
        check_vector_finite("udv_inv_one_plus_two_sided_work", "Drb", n,
                            Drb) ||
        check_vector_nonzero("udv_inv_one_plus_two_sided_work", "Dlb", n,
                             Dlb) ||
        check_vector_nonzero("udv_inv_one_plus_two_sided_work", "Drb", n,
                             Drb)) {
        goto fail;
    }

    la_matmul(n, l->T, r->U, TLUR);
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "TLUR", n,
                            TLUR)) {
        goto fail;
    }

    memcpy(Tinv, r->T, sizeof(double) * (size_t)n * (size_t)n);
    {
        const char uplo = 'U';
        const char diag = 'U';
        int info = 0;
        dtrtri_(&uplo, &diag, &n, Tinv, &n, &info);
        if (info != 0) {
            fprintf(stderr,
                    "ERROR: udv_inv_one_plus_two_sided_work dtrtri failed "
                    "info=%d n=%d\n",
                    info, n);
            goto fail;
        }
    }
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "Tinv", n,
                            Tinv)) {
        goto fail;
    }

    la_gemm(n, 1, 0, 1.0, l->U, Tinv, 0.0, ULTinv);
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "ULTinv", n,
                            ULTinv)) {
        goto fail;
    }

    for (int j = 0; j < n; j++) {
        const double rell =
            right_centered ? log(fabs(r->D[j])) + r->log_offset : 0.0;
        const double drs = right_centered
                               ? ((rell > 0.0)
                                      ? 1.0
                                      : copysign(exp(rell), r->D[j]))
                               : ((fabs(r->D[j]) > 1.0) ? 1.0 : r->D[j]);
        if (!isfinite(drs) || drs == 0.0) goto fail;
        for (int i = 0; i < n; i++) {
            const double lell =
                left_centered ? log(fabs(l->D[i])) + l->log_offset : 0.0;
            const double dls = left_centered
                                   ? ((lell > 0.0)
                                          ? 1.0
                                          : copysign(exp(lell), l->D[i]))
                                   : ((fabs(l->D[i]) > 1.0) ? 1.0
                                                                    : l->D[i]);
            if (!isfinite(dls) || dls == 0.0) goto fail;
            double large = ULTinv[i + j * n];
            large = left_centered ? large * Dlb[i] : large / Dlb[i];
            large = right_centered ? large * Drb[j] : large / Drb[j];
            H[i + j * n] = large + dls * TLUR[i + j * n] * drs;
        }
    }
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "H", n, H)) {
        goto fail;
    }

    const int inv_info = la_inverse_work(n, H, Hinv, w);
    if (inv_info != 0) {
        fprintf(stderr,
                "ERROR: udv_inv_one_plus_two_sided_work la_inverse_work(H) "
                "failed info=%d n=%d\n",
                inv_info, n);
        goto fail;
    }
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "Hinv", n,
                            Hinv)) {
        goto fail;
    }

    if (det_sign != NULL) {
        int sgn = 1;
        for (int i = 0; i < n; i++) {
            const int lbig =
                left_centered
                    ? (log(fabs(l->D[i])) + l->log_offset > 0.0)
                    : (fabs(l->D[i]) > 1.0);
            const int rbig =
                right_centered
                    ? (log(fabs(r->D[i])) + r->log_offset > 0.0)
                    : (fabs(r->D[i]) > 1.0);
            if (lbig && l->D[i] < 0.0) {
                sgn = -sgn;
            }
            if (rbig && r->D[i] < 0.0) {
                sgn = -sgn;
            }
        }
        int su = 0;
        int sh = 0;
        double logabs = 0.0;
        memcpy(det_tmp, l->U, sizeof(double) * (size_t)n * (size_t)n);
        const int iu = la_logdet_work(n, det_tmp, &su, &logabs, w);
        if (iu == 0 && !isfinite(logabs)) {
            fprintf(stderr,
                    "ERROR: udv_inv_one_plus_two_sided_work non-finite "
                    "logdet(left_U) logabs=%.17g n=%d\n",
                    logabs, n);
            goto fail;
        }
        memcpy(det_tmp, H, sizeof(double) * (size_t)n * (size_t)n);
        const int ih = la_logdet_work(n, det_tmp, &sh, &logabs, w);
        if (ih == 0 && !isfinite(logabs)) {
            fprintf(stderr,
                    "ERROR: udv_inv_one_plus_two_sided_work non-finite "
                    "logdet(H) logabs=%.17g n=%d\n",
                    logabs, n);
            goto fail;
        }
        if (iu != 0 || ih != 0) {
            fprintf(stderr,
                    "ERROR: udv_inv_one_plus_two_sided_work logdet failed "
                    "iu=%d ih=%d n=%d\n",
                    iu, ih, n);
            goto fail;
        }
        *det_sign = sgn * su * sh;
    }

    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            tmp[i + j * n] = left_centered
                                 ? Hinv[i + j * n] * Dlb[j]
                                 : Hinv[i + j * n] / Dlb[j];
        }
    }
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "Hinv_Dlb",
                            n, tmp)) {
        goto fail;
    }
    la_gemm(n, 0, 1, 1.0, tmp, l->U, 0.0, tmp2);
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "right_part",
                            n, tmp2)) {
        goto fail;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            tmp[i + j * n] = right_centered
                                 ? tmp2[i + j * n] * Drb[i]
                                 : tmp2[i + j * n] / Drb[i];
        }
    }
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "Drb_right",
                            n, tmp)) {
        goto fail;
    }
    la_matmul(n, Tinv, tmp, g);
    if (check_matrix_finite("udv_inv_one_plus_two_sided_work", "g", n, g)) {
        goto fail;
    }

    PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
    return 0;

fail:
    if (w != NULL && w->ok && w->n == n) {
        w->failed = 1;
    }
    if (det_sign != NULL) {
        *det_sign = 0;
    }
    PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
    return 1;
}

int udv_inv_one_plus(const UDV *s, double *g, int *det_sign)
{
    LinalgWork w;
    linalg_work_init(&w, s->n);
    const int rc = udv_inv_one_plus_work(s, g, det_sign, &w);
    linalg_work_free(&w);
    return rc;
}
