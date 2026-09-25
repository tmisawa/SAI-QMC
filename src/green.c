#include "green.h"

#include "linalg.h"
#include "profiler.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GREEN_DELAY_CAPACITY 16

static int sidx(signed char s)
{
    return (s > 0) ? 1 : 0;
}

static int green_uses_centered(const Green *G)
{
    return G->rebuild_mode == GREEN_REBUILD_CENTERED;
}

static void green_udv_lmul(Green *G, UDV *s, const double *B)
{
    if (green_uses_centered(G)) {
        udv_lmul_centered_work(s, B, &G->work);
    } else {
        udv_lmul_work(s, B, &G->work);
    }
}

static void green_udv_rmul(Green *G, UDV *s, const double *B)
{
    if (green_uses_centered(G)) {
        udv_rmul_centered(s, B, &G->work);
    } else {
        udv_rmul(s, B, &G->work);
    }
}

static int green_record_scale(Green *G, const char *direction, int tau,
                              int boundary, const UDV *factor)
{
    if (G->scale_observer == NULL) {
        return 0;
    }
    const int rc = G->scale_observer(G->scale_observer_ctx, G, direction, tau,
                                     boundary, factor);
    if (rc != 0) {
        G->work.failed = 1;
    }
    return rc;
}

void green_alloc(Green *G, const Model *m, const Field *f, double sigma)
{
    G->n = m->n;
    G->L = f->L;
    G->m = m;
    G->f = f;
    G->g = malloc(sizeof(double) * (size_t)G->n * (size_t)G->n);
    G->cur_l = 0;
    G->sigma = sigma;
    const double c = -G->m->dtau * G->m->U / 2.0;
    G->expv[0] = exp(G->f->lambda * G->sigma * -1.0 + c);
    G->expv[1] = exp(G->f->lambda * G->sigma * 1.0 + c);
    G->expvinv[0] = 1.0 / G->expv[0];
    G->expvinv[1] = 1.0 / G->expv[1];
    const size_t nn = (size_t)G->n * (size_t)G->n;
    G->B = malloc(sizeof(double) * nn);
    G->Binv = malloc(sizeof(double) * nn);
    G->tmp = malloc(sizeof(double) * nn);
    G->u = malloc(sizeof(double) * (size_t)G->n);
    G->v = malloc(sizeof(double) * (size_t)G->n);
    G->delay_count = 0;
    G->delay_capacity = GREEN_DELAY_CAPACITY;
    G->delay_c =
        malloc(sizeof(double) * (size_t)G->n * (size_t)G->delay_capacity);
    G->delay_v =
        malloc(sizeof(double) * (size_t)G->n * (size_t)G->delay_capacity);
    linalg_work_init(&G->work, G->n);
    G->scale_observer = NULL;
    G->scale_observer_ctx = NULL;
    G->scale_observer_spin = NULL;
    G->rebuild_mode = GREEN_REBUILD_COMBINE;
    G->det_sign = 1;
}

void green_free(Green *G)
{
    free(G->g);
    free(G->B);
    free(G->Binv);
    free(G->tmp);
    free(G->u);
    free(G->v);
    free(G->delay_c);
    free(G->delay_v);
    linalg_work_free(&G->work);
    G->n = 0;
    G->L = 0;
    G->m = NULL;
    G->f = NULL;
    G->g = NULL;
    G->B = NULL;
    G->Binv = NULL;
    G->tmp = NULL;
    G->u = NULL;
    G->v = NULL;
    G->delay_count = 0;
    G->delay_capacity = 0;
    G->delay_c = NULL;
    G->delay_v = NULL;
    G->scale_observer = NULL;
    G->scale_observer_ctx = NULL;
    G->scale_observer_spin = NULL;
    G->rebuild_mode = GREEN_REBUILD_COMBINE;
    G->cur_l = 0;
    G->sigma = 0.0;
}

void green_set_rebuild_mode(Green *G, GreenRebuildMode mode)
{
    G->rebuild_mode = mode;
}

void green_build_B(const Green *G, int l, double *out)
{
    const int n = G->n;

    for (int j = 0; j < n; j++) {
        const signed char s = G->f->s[l * n + j];
        const double d = G->expv[sidx(s)];
        for (int i = 0; i < n; i++) {
            out[i + j * n] = G->m->expK[i + j * n] * d;
        }
    }
}

void green_build_Binv(const Green *G, int l, double *out)
{
    const int n = G->n;

    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            const signed char s = G->f->s[l * n + i];
            out[i + j * n] = G->m->expKinv[i + j * n] * G->expvinv[sidx(s)];
        }
    }
}

void green_build_Bblock(const Green *G, int l_begin, int len, double *out,
                        double *Btmp, double *tmp)
{
    const int n = G->n;
    if (len <= 0) {
        la_eye(n, out);
        return;
    }

    green_build_B(G, l_begin, out);
    for (int p = 1; p < len; p++) {
        green_build_B(G, l_begin + p, Btmp);
        la_matmul(n, Btmp, out, tmp);
        memcpy(out, tmp, sizeof(double) * (size_t)n * (size_t)n);
    }
}

void green_stack_alloc(GreenStack *st, int n, int L, int stab)
{
    st->n = n;
    st->L = L;
    st->stab = (stab > 0) ? stab : 8;
    st->M = (L + st->stab - 1) / st->stab;
    st->b = malloc(sizeof(int) * (size_t)(st->M + 1));
    st->S = malloc(sizeof(UDV) * (size_t)(st->M + 1));
    for (int j = 0; j <= st->M; j++) {
        int bj = j * st->stab;
        if (bj > L) {
            bj = L;
        }
        st->b[j] = bj;
        udv_init(&st->S[j], n);
    }
}

void green_stack_free(GreenStack *st)
{
    if (st->S != NULL) {
        for (int j = 0; j <= st->M; j++) {
            udv_free(&st->S[j]);
        }
    }
    free(st->S);
    free(st->b);
    st->n = 0;
    st->L = 0;
    st->stab = 0;
    st->M = 0;
    st->S = NULL;
    st->b = NULL;
}

void green_stack_build_suffix(Green *G, GreenStack *st)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_stack_build);
    udv_identity(&st->S[st->M]);
    udv_identity(&st->S[0]);
    for (int j = st->M - 1; j >= 1; j--) {
        const int begin = st->b[j];
        const int len = st->b[j + 1] - st->b[j];
        green_build_Bblock(G, begin, len, G->B, G->Binv, G->tmp);
        udv_copy(&st->S[j], &st->S[j + 1]);
        if (green_record_scale(G, "stack_suffix_pre_rmul", st->b[j], j,
                               &st->S[j]) != 0) {
            break;
        }
        green_udv_rmul(G, &st->S[j], G->B);
        if (G->work.failed) {
            (void)green_record_scale(G, "stack_suffix_failed_rmul",
                                     st->b[j], j, &st->S[j]);
            break;
        }
    }
    PROF_END(prof, PROF_GREEN_STACK_BUILD, t_green_stack_build);
}

void green_stack_build_prefix(Green *G, GreenStack *st)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_stack_build);
    udv_identity(&st->S[0]);
    for (int j = 1; j <= st->M; j++) {
        const int begin = st->b[j - 1];
        const int len = st->b[j] - st->b[j - 1];
        green_build_Bblock(G, begin, len, G->B, G->Binv, G->tmp);
        udv_copy(&st->S[j], &st->S[j - 1]);
        if (green_record_scale(G, "stack_prefix_pre_lmul", st->b[j], j,
                               &st->S[j]) != 0) {
            break;
        }
        green_udv_lmul(G, &st->S[j], G->B);
        if (G->work.failed) {
            (void)green_record_scale(G, "stack_prefix_failed_lmul",
                                     st->b[j], j, &st->S[j]);
            break;
        }
    }
    PROF_END(prof, PROF_GREEN_STACK_BUILD, t_green_stack_build);
}

void green_stack_build(Green *G, GreenStack *st)
{
    green_stack_build_suffix(G, st);
}

void green_stack_store(GreenStack *st, int j, const UDV *factor)
{
    udv_copy(&st->S[j], factor);
}

int green_from_scratch(Green *G, int l0)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_from_scratch);
    const int n = G->n;
    const int L = G->L;
    UDV udv;

    udv_init(&udv, n);
    int rc = 0;
    for (int k = 0; k < L; k++) {
        const int l = (l0 + k) % L;
        green_build_B(G, l, G->B);
        green_udv_lmul(G, &udv, G->B);
        if (G->work.failed) {
            (void)green_record_scale(G, "from_scratch_failed_lmul", l,
                                     k + 1, &udv);
            rc = 1;
            break;
        }
    }
    /* det_sign = sign(det(1 + B_{L-1}...B_0)), computed from the stabilized
       UDV factors (robust at low temperature). On failure det_sign is 0 and we
       never keep a stale sign. */
    int sgn = 0;
    if (rc == 0) {
        rc = udv_inv_one_plus_work(&udv, G->g, &sgn, &G->work);
        if (rc != 0) {
            (void)green_record_scale(G, "from_scratch_failed_solve", l0, 0,
                                     &udv);
        }
    }
    G->cur_l = l0;
    udv_free(&udv);
    G->delay_count = 0;

    if (rc != 0) {
        G->det_sign = 0;
        PROF_END(prof, PROF_GREEN_FROM_SCRATCH, t_green_from_scratch);
        return rc;
    }
    G->det_sign = sgn;
    PROF_END(prof, PROF_GREEN_FROM_SCRATCH, t_green_from_scratch);
    return 0;
}

int green_from_boundary_factors(Green *G, const UDV *prefix,
                                const UDV *suffix, UDV *combined,
                                int cur_l)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_from_stack);
    int sgn = 0;
    int rc = 1;
    if (G->rebuild_mode == GREEN_REBUILD_TWO_SIDED ||
        G->rebuild_mode == GREEN_REBUILD_CENTERED) {
        (void)combined;
        rc = udv_inv_one_plus_two_sided_work(prefix, suffix, G->g, &sgn,
                                             &G->work);
    } else if (G->rebuild_mode == GREEN_REBUILD_COMBINE) {
        udv_combine(prefix, suffix, combined, &G->work);
        rc = udv_inv_one_plus_work(combined, G->g, &sgn, &G->work);
    } else {
        G->work.failed = 1;
    }
    G->cur_l = cur_l % G->L;
    if (G->cur_l < 0) {
        G->cur_l += G->L;
    }
    G->delay_count = 0;
    if (rc != 0) {
        (void)green_record_scale(G, "boundary_failed_prefix", cur_l, cur_l,
                                 prefix);
        (void)green_record_scale(G, "boundary_failed_suffix", cur_l, cur_l,
                                 suffix);
        G->det_sign = 0;
        PROF_END(prof, PROF_GREEN_FROM_STACK, t_green_from_stack);
        return rc;
    }
    G->det_sign = sgn;
    PROF_END(prof, PROF_GREEN_FROM_STACK, t_green_from_stack);
    return 0;
}

int green_from_stack(Green *G, const GreenStack *st, const UDV *left,
                     UDV *combined, int j)
{
    return green_from_boundary_factors(G, left, &st->S[j], combined,
                                       st->b[j]);
}

int green_from_left_udv(Green *G, const UDV *left)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_from_stack);
    int sgn = 0;
    const int rc = udv_inv_one_plus_work(left, G->g, &sgn, &G->work);
    G->cur_l = 0;
    G->delay_count = 0;
    if (rc != 0) {
        (void)green_record_scale(G, "left_udv_failed_solve", 0, 0, left);
        G->det_sign = 0;
        PROF_END(prof, PROF_GREEN_FROM_STACK, t_green_from_stack);
        return rc;
    }
    G->det_sign = sgn;
    PROF_END(prof, PROF_GREEN_FROM_STACK, t_green_from_stack);
    return 0;
}

void green_build_ph_down(const Green *up, const int *bipart, double *g_down)
{
    const int n = up->n;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            const double dij = (i == j) ? 1.0 : 0.0;
            const double p = (double)(bipart[i] * bipart[j]);
            g_down[i + j * n] = dij - p * up->g[j + i * n];
        }
    }
}

static double green_delay_diag(const Green *G, int i)
{
    const int n = G->n;
    double gii = G->g[i + i * n];
    for (int p = 0; p < G->delay_count; p++) {
        gii -= G->delay_c[i + p * n] * G->delay_v[i + p * n];
    }
    return gii;
}

double green_flipN(const Green *G, int i)
{
    const int n = G->n;
    const signed char s_old = G->f->s[G->cur_l * n + i];
    return field_N(G->f, G->sigma, s_old);
}

double green_ratio_N(const Green *G, int i, double N)
{
    const int n = G->n;
    return 1.0 + (1.0 - G->g[i + i * n]) * N;
}

double green_ph_down_ratio_N(const Green *up, int i, double N)
{
    return 1.0 + green_delay_diag(up, i) * N;
}

double green_delay_ratio_N(const Green *G, int i, double N)
{
    const double gii = green_delay_diag(G, i);
    return 1.0 + (1.0 - gii) * N;
}

void green_delay_accept(Green *G, int i, double N)
{
    if (G->delay_count >= G->delay_capacity) {
        green_delay_flush(G);
    }

    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_update);
    const int n = G->n;
    double *g = G->g;
    double *u = G->u;
    double *v = G->v;
    const double denom = green_delay_ratio_N(G, i, N);
    const double fac = N / denom;

    for (int j = 0; j < n; j++) {
        double gij = g[j + i * n];
        for (int p = 0; p < G->delay_count; p++) {
            gij -= G->delay_c[j + p * n] * G->delay_v[i + p * n];
        }
        u[j] = ((i == j) ? 1.0 : 0.0) - gij;
    }
    for (int k = 0; k < n; k++) {
        double gik = g[i + k * n];
        for (int p = 0; p < G->delay_count; p++) {
            gik -= G->delay_c[i + p * n] * G->delay_v[k + p * n];
        }
        v[k] = gik;
    }

    const int p = G->delay_count++;
    for (int j = 0; j < n; j++) {
        G->delay_c[j + p * n] = fac * u[j];
    }
    for (int k = 0; k < n; k++) {
        G->delay_v[k + p * n] = v[k];
    }
    PROF_END(prof, PROF_GREEN_UPDATE, t_green_update);

    if (G->delay_count == G->delay_capacity) {
        green_delay_flush(G);
    }
}

void green_delay_flush(Green *G)
{
    if (G->delay_count == 0) {
        return;
    }

    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_update);
    const int n = G->n;
    la_gemm_nt_rect(n, G->delay_count, -1.0, G->delay_c, G->delay_v, 1.0,
                    G->g);
    G->delay_count = 0;
    PROF_END(prof, PROF_GREEN_UPDATE, t_green_update);
}

int green_delay_count(const Green *G)
{
    return G->delay_count;
}

void green_update(Green *G, int i, double N)
{
    green_delay_flush(G);

    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_update);
    const int n = G->n;
    double *g = G->g;
    double *u = G->u;
    double *v = G->v;
    const double denom = green_ratio_N(G, i, N);
    const double fac = N / denom;

    for (int j = 0; j < n; j++) {
        u[j] = ((i == j) ? 1.0 : 0.0) - g[j + i * n];
    }
    for (int k = 0; k < n; k++) {
        v[k] = g[i + k * n];
    }
    for (int k = 0; k < n; k++) {
        for (int j = 0; j < n; j++) {
            g[j + k * n] -= fac * u[j] * v[k];
        }
    }
    PROF_END(prof, PROF_GREEN_UPDATE, t_green_update);
}

void green_wrap(Green *G)
{
    green_delay_flush(G);

    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_wrap);
    const int n = G->n;
    double *B = G->B;
    double *Binv = G->Binv;
    double *tmp = G->tmp;

    green_build_B(G, G->cur_l, B);
    green_build_Binv(G, G->cur_l, Binv);
    la_matmul(n, B, G->g, tmp);
    la_matmul(n, tmp, Binv, G->g);
    G->cur_l = (G->cur_l + 1) % G->L;

    PROF_END(prof, PROF_GREEN_WRAP, t_green_wrap);
}

void green_wrap_backward(Green *G)
{
    green_delay_flush(G);

    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_wrap);
    const int n = G->n;
    double *B = G->B;
    double *Binv = G->Binv;
    double *tmp = G->tmp;
    const int lprev = (G->cur_l - 1 + G->L) % G->L;

    green_build_B(G, lprev, B);
    green_build_Binv(G, lprev, Binv);
    la_matmul(n, Binv, G->g, tmp);
    la_matmul(n, tmp, B, G->g);
    G->cur_l = lprev;

    PROF_END(prof, PROF_GREEN_WRAP, t_green_wrap);
}

int green_logdet_full(Green *G, int stab, int *det_sign, double *logabs)
{
    if (det_sign != NULL) {
        *det_sign = 0;
    }
    if (G == NULL || det_sign == NULL || logabs == NULL || stab <= 0) {
        return 1;
    }
    const int n = G->n;
    const int L = G->L;
    UDV udv;
    udv_init(&udv, n);
    udv_identity(&udv);
    int rc = 0;
    for (int begin = 0; begin < L; begin += stab) {
        const int len = (begin + stab <= L) ? stab : (L - begin);
        green_build_Bblock(G, begin, len, G->B, G->Binv, G->tmp);
        green_udv_lmul(G, &udv, G->B);
        if (G->work.failed) {
            rc = 1;
            break;
        }
    }
    if (rc == 0) {
        rc = udv_logdet_one_plus_work(&udv, det_sign, logabs, &G->work);
    }
    udv_free(&udv);
    return rc;
}
