#ifndef LINALG_H
#define LINALG_H

/* All matrices are n x n, column-major: A[i + j*n]. */

void la_eye(int n, double *A);
void la_matmul(int n, const double *A, const double *B, double *C);
void la_gemm(int n, int ta, int tb, double alpha, const double *A,
             const double *B, double beta, double *C);
void la_gemm_nt_rect(int n, int k, double alpha, const double *A,
                     const double *B, double beta, double *C);
int la_inverse(int n, const double *A, double *Ainv);
int la_logdet(int n, double *A, int *sign, double *logabs);
void la_expm_sym(int n, const double *A, double scale, double *expA);

typedef struct {
    int n;
    double *U;
    double *D;
    double *T;
    /* Positive scalar scale stored outside D in centered mode:
       product = exp(log_offset) * U * diag(D) * T. Legacy factors keep 0. */
    double log_offset;
} UDV;

typedef struct {
    int n;
    int lwork;
    int ok;
    int failed;
    int failure_reason;
    double *M;
    double *A;
    double *B;
    double *C;
    double *Dmat;
    double *E;
    double *F;
    double *R;
    double *Ttmp;
    double *v1;
    double *v2;
    double *tau;
    double *work;
    int *ipiv;
} LinalgWork;

enum {
    LINALG_FAILURE_NONE = 0,
    LINALG_FAILURE_CENTERED_RADIUS = 1,
    LINALG_FAILURE_CENTERED_UPDATE_MARGIN = 2,
    LINALG_FAILURE_EFFECTIVE_LOG_RANGE = 3
};

void linalg_work_init(LinalgWork *w, int n);
void linalg_work_free(LinalgWork *w);
const char *linalg_failure_reason_string(int reason);

void udv_init(UDV *s, int n);
void udv_free(UDV *s);
void udv_identity(UDV *s);
void udv_copy(UDV *dst, const UDV *src);
int udv_recenter(UDV *s, double hard_margin, double *radius,
                 double *remaining_margin);
void udv_lmul(UDV *s, const double *B);
void udv_lmul_work(UDV *s, const double *B, LinalgWork *w);
void udv_lmul_centered_work(UDV *s, const double *B, LinalgWork *w);
void udv_rmul(UDV *s, const double *B, LinalgWork *w);
void udv_rmul_centered(UDV *s, const double *B, LinalgWork *w);
/* Combines out = l * r. out must not alias l or r because the result factors
   are written while the input factors are still being read. */
void udv_combine(const UDV *l, const UDV *r, UDV *out, LinalgWork *w);
/* Computes g = (1 + U*diag(D)*T)^{-1}. If det_sign != NULL, also returns
   sign(det(1 + U*diag(D)*T)) in *det_sign (+1/-1), derived from the
   well-conditioned/triangular factors (robust at low temperature). On failure
   returns nonzero and sets *det_sign = 0. */
int udv_inv_one_plus(const UDV *s, double *g, int *det_sign);
int udv_inv_one_plus_work(const UDV *s, double *g, int *det_sign,
                          LinalgWork *w);
int udv_inv_one_plus_two_sided_work(const UDV *l, const UDV *r, double *g,
                                    int *det_sign, LinalgWork *w);
int la_inverse_work(int n, const double *A, double *Ainv, LinalgWork *w);
int la_logdet_work(int n, double *A, int *sign, double *logabs,
                   LinalgWork *w);

#endif
