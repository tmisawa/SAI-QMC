#include "test_util.h"
#include "linalg.h"
#include "rng.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Dense reference: log|det(1 + exp(off) U diag(D) T)| via la_logdet. */
static void dense_ref(const UDV *s, int *sign, double *logabs)
{
    const int n = s->n;
    double *UD = calloc((size_t)n * n, sizeof(double));
    double *P = calloc((size_t)n * n, sizeof(double));
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            UD[i + j * n] = s->U[i + j * n] * s->D[j] * exp(s->log_offset);
        }
    }
    la_matmul(n, UD, s->T, P);
    for (int i = 0; i < n; i++) {
        P[i + i * n] += 1.0;
    }
    la_logdet(n, P, sign, logabs);
    free(UD);
    free(P);
}

static void set_identity_udv(UDV *s, int n)
{
    udv_init(s, n);
    udv_identity(s);
}

int main(void)
{
    LinalgWork w;

    /* 1. analytic: U=T=I, D=I, log_offset=100 -> 2*log(1+e^100) ~ 200 */
    {
        UDV s;
        set_identity_udv(&s, 2);
        s.log_offset = 100.0;
        linalg_work_init(&w, 2);
        int sg = 0;
        double la = 0.0;
        CHECK(udv_logdet_one_plus_work(&s, &sg, &la, &w) == 0);
        CHECK(sg == 1);
        CHECK_CLOSE(la, 200.0, 1e-9);
        linalg_work_free(&w);
        udv_free(&s);
    }
    /* 2. same effective product in two representations */
    {
        UDV a, b;
        set_identity_udv(&a, 3);
        set_identity_udv(&b, 3);
        for (int i = 0; i < 3; i++) {
            a.D[i] = exp(5.0) * (double)(i + 1);
            b.D[i] = (double)(i + 1);
        }
        b.log_offset = 5.0;
        linalg_work_init(&w, 3);
        int sa = 0, sb = 0;
        double la = 0.0, lb = 0.0;
        CHECK(udv_logdet_one_plus_work(&a, &sa, &la, &w) == 0);
        CHECK(udv_logdet_one_plus_work(&b, &sb, &lb, &w) == 0);
        CHECK(sa == sb);
        CHECK_CLOSE(la, lb, 1e-12);
        linalg_work_free(&w);
        udv_free(&a);
        udv_free(&b);
    }
    /* 3. signs: D=diag(-3,2) -> det(1+D)=-6 ; U=diag(1,-1), D=diag(2,3) -> det=-6 */
    {
        UDV s;
        set_identity_udv(&s, 2);
        s.D[0] = -3.0;
        s.D[1] = 2.0;
        linalg_work_init(&w, 2);
        int sg = 0;
        double la = 0.0;
        CHECK(udv_logdet_one_plus_work(&s, &sg, &la, &w) == 0);
        CHECK(sg == -1);
        CHECK_CLOSE(la, log(6.0), 1e-12);
        s.D[0] = 2.0;
        s.D[1] = 3.0;
        s.U[1 + 1 * 2] = -1.0;
        CHECK(udv_logdet_one_plus_work(&s, &sg, &la, &w) == 0);
        CHECK(sg == -1);
        CHECK_CLOSE(la, log(6.0), 1e-12);
        linalg_work_free(&w);
        udv_free(&s);
    }
    /* 4. extreme scales stay finite: D=diag(1e200,1e-200) */
    {
        UDV s;
        set_identity_udv(&s, 2);
        s.D[0] = 1e200;
        s.D[1] = 1e-200;
        linalg_work_init(&w, 2);
        int sg = 0;
        double la = 0.0;
        CHECK(udv_logdet_one_plus_work(&s, &sg, &la, &w) == 0);
        CHECK(sg == 1);
        CHECK_CLOSE(la, 200.0 * log(10.0), 1e-9);
        linalg_work_free(&w);
        udv_free(&s);
    }
    /* 5. random UDV built by the production multiply, legacy and centered */
    for (int centered = 0; centered < 2; centered++) {
        const int n = 5;
        Rng r;
        rng_seed(&r, 4242 + (uint64_t)centered);
        UDV s;
        set_identity_udv(&s, n);
        linalg_work_init(&w, n);
        double *B = calloc((size_t)n * n, sizeof(double));
        for (int step = 0; step < 6; step++) {
            for (int k = 0; k < n * n; k++) {
                B[k] = 2.0 * rng_double(&r) - 1.0;
            }
            for (int i = 0; i < n; i++) {
                B[i + i * n] += 2.0;
            }
            if (centered) {
                udv_lmul_centered_work(&s, B, &w);
            } else {
                udv_lmul_work(&s, B, &w);
            }
        }
        int sg = 0, sr = 0;
        double la = 0.0, lr = 0.0;
        CHECK(udv_logdet_one_plus_work(&s, &sg, &la, &w) == 0);
        dense_ref(&s, &sr, &lr);
        CHECK(isfinite(la) && isfinite(lr)); /* CHECK_CLOSE does not catch NaN */
        CHECK(sg == sr);
        CHECK_CLOSE(la, lr, 1e-9);
        /* NULL work falls back to a temporary one */
        int sg2 = 0;
        double la2 = 0.0;
        CHECK(udv_logdet_one_plus_work(&s, &sg2, &la2, NULL) == 0);
        CHECK(sg2 == sg);
        CHECK_CLOSE(la2, la, 1e-12);
        free(B);
        linalg_work_free(&w);
        udv_free(&s);
    }
    /* 6. invalid input and non-finite results are failures */
    {
        int sg = 5;
        double la = 0.0;
        CHECK(udv_logdet_one_plus_work(NULL, &sg, &la, NULL) != 0);
        CHECK(sg == 0);
        CHECK(isnan(la));
        UDV s;
        set_identity_udv(&s, 2);
        s.log_offset = 1e308;             /* sum of effective scales overflows */
        sg = 5;
        la = 0.0;
        CHECK(udv_logdet_one_plus_work(&s, &sg, &la, NULL) != 0);
        CHECK(sg == 0);
        CHECK(isnan(la));
        s.log_offset = 0.0;
        s.D[0] = 0.0;
        CHECK(udv_logdet_one_plus_work(&s, &sg, &la, NULL) != 0);
        udv_free(&s);
    }
    TEST_END();
}
