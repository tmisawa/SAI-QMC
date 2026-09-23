#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "linalg.h"
#include "model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void udv_product(const UDV *s, double *out)
{
    const int n = s->n;
    double *UD = malloc(sizeof(double) * (size_t)n * (size_t)n);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            UD[i + j * n] =
                s->U[i + j * n] * s->D[j] * exp(s->log_offset);
        }
    }
    la_matmul(n, UD, s->T, out);
    free(UD);
}

static void check_close_mat(int n, const double *a, const double *b, double tol)
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

static int brute_det_sign(Green *G, int l0)
{
    const int n = G->n;
    const int L = G->L;
    double *B = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *P = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *tmp = malloc(sizeof(double) * (size_t)n * (size_t)n);
    la_eye(n, P);
    for (int k = 0; k < L; k++) {
        const int l = (l0 + k) % L;
        green_build_B(G, l, B);
        la_matmul(n, B, P, tmp);
        memcpy(P, tmp, sizeof(double) * (size_t)n * (size_t)n);
    }
    for (int i = 0; i < n; i++) {
        P[i + i * n] += 1.0;
    }
    int sign = 0;
    double logabs = 0.0;
    CHECK(la_logdet(n, P, &sign, &logabs) == 0);
    free(tmp);
    free(P);
    free(B);
    return sign;
}

static void dense_product_range(Green *G, int begin, int end, double *out,
                                double *B, double *tmp)
{
    const int n = G->n;
    la_eye(n, out);
    for (int l = begin; l < end; l++) {
        green_build_B(G, l, B);
        la_matmul(n, B, out, tmp);
        memcpy(out, tmp, sizeof(double) * (size_t)n * (size_t)n);
    }
}

static void check_suffix_stack_products(Green *G, GreenStack *st)
{
    const int n = G->n;
    double *B = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *P = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *tmp = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *got = malloc(sizeof(double) * (size_t)n * (size_t)n);

    for (int j = 1; j <= st->M; j++) {
        dense_product_range(G, st->b[j], G->L, P, B, tmp);
        udv_product(&st->S[j], got);
        check_close_mat(n, got, P, 1e-10);
    }

    free(got);
    free(tmp);
    free(P);
    free(B);
}

static void check_prefix_stack_products(Green *G, GreenStack *st)
{
    const int n = G->n;
    double *B = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *P = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *tmp = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *got = malloc(sizeof(double) * (size_t)n * (size_t)n);

    for (int j = 0; j <= st->M; j++) {
        dense_product_range(G, 0, st->b[j], P, B, tmp);
        udv_product(&st->S[j], got);
        check_close_mat(n, got, P, 1e-10);
    }

    free(got);
    free(tmp);
    free(P);
    free(B);
}

static void check_stack_entries_close(const GreenStack *a, const GreenStack *b,
                                      double tol)
{
    const int n = a->n;
    double *pa = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *pb = malloc(sizeof(double) * (size_t)n * (size_t)n);

    CHECK(a->M == b->M);
    for (int j = 0; j <= a->M; j++) {
        CHECK(a->b[j] == b->b[j]);
        udv_product(&a->S[j], pa);
        udv_product(&b->S[j], pb);
        check_close_mat(n, pa, pb, tol);
    }

    free(pb);
    free(pa);
}

static void check_suffix_entries_close(const GreenStack *a,
                                       const GreenStack *b, double tol)
{
    const int n = a->n;
    double *pa = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *pb = malloc(sizeof(double) * (size_t)n * (size_t)n);

    CHECK(a->M == b->M);
    for (int j = 1; j <= a->M; j++) {
        CHECK(a->b[j] == b->b[j]);
        udv_product(&a->S[j], pa);
        udv_product(&b->S[j], pb);
        check_close_mat(n, pa, pb, tol);
    }

    free(pb);
    free(pa);
}

static void check_boundary_reconstruction(Green *G, Green *Ref,
                                          GreenStack *prefix,
                                          GreenStack *suffix)
{
    UDV combined;
    udv_init(&combined, G->n);
    for (int j = 1; j <= prefix->M; j++) {
        const int tau = prefix->b[j] % G->L;
        CHECK(green_from_boundary_factors(G, &prefix->S[j], &suffix->S[j],
                                          &combined, prefix->b[j]) == 0);
        CHECK(G->cur_l == tau);
        CHECK(green_from_scratch(Ref, tau) == 0);
        check_close_mat(G->n, G->g, Ref->g, 1e-10);
        CHECK(G->det_sign == Ref->det_sign);
        CHECK(G->det_sign == brute_det_sign(G, tau));
    }
    udv_free(&combined);
}

static void flip_block(Field *f, int begin, int end, int salt)
{
    for (int l = begin; l < end; l++) {
        const int i = (3 * l + salt) % f->n;
        f->s[l * f->n + i] *= -1;
        if (f->n > 1 && ((l + salt) % 2) == 0) {
            const int k = (i + 1) % f->n;
            f->s[l * f->n + k] *= -1;
        }
    }
}

static void check_carried_store_matches_fresh(Green *G, Field *f,
                                              const GreenStack *base)
{
    GreenStack carried;
    GreenStack fresh;
    UDV acc;

    green_stack_alloc(&carried, G->n, G->L, base->stab);
    green_stack_alloc(&fresh, G->n, G->L, base->stab);
    udv_init(&acc, G->n);

    udv_identity(&acc);
    green_stack_store(&carried, 0, &acc);
    for (int j = 0; j < base->M; j++) {
        const int begin = base->b[j];
        const int len = base->b[j + 1] - base->b[j];
        flip_block(f, begin, base->b[j + 1], 11 + j);
        green_build_Bblock(G, begin, len, G->B, G->Binv, G->tmp);
        udv_lmul_work(&acc, G->B, &G->work);
        green_stack_store(&carried, j + 1, &acc);
    }
    green_stack_build_prefix(G, &fresh);
    check_stack_entries_close(&carried, &fresh, 1e-10);

    udv_identity(&acc);
    green_stack_store(&carried, base->M, &acc);
    for (int j = base->M - 1; j >= 1; j--) {
        const int begin = base->b[j];
        const int len = base->b[j + 1] - base->b[j];
        flip_block(f, begin, base->b[j + 1], 29 + j);
        green_build_Bblock(G, begin, len, G->B, G->Binv, G->tmp);
        udv_rmul(&acc, G->B, &G->work);
        green_stack_store(&carried, j, &acc);
    }
    green_stack_build_suffix(G, &fresh);
    check_suffix_entries_close(&carried, &fresh, 1e-10);

    udv_free(&acc);
    green_stack_free(&fresh);
    green_stack_free(&carried);
}

static void run_case(int Ltr, int stab, double U)
{
    const int Lx = 4;
    const double dtau = 0.1;
    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 1234 + Ltr * 17 + stab * 31 + (int)U);
    Field f;
    field_init(&f, Lx, Ltr, U, dtau, &r);

    Green G;
    Green Ref;
    green_alloc(&G, &m, &f, 1.0);
    green_alloc(&Ref, &m, &f, 1.0);
    GreenStack st;
    GreenStack prefix;
    green_stack_alloc(&st, Lx, Ltr, stab);
    green_stack_alloc(&prefix, Lx, Ltr, stab);
    green_stack_build(&G, &st);
    green_stack_build_prefix(&G, &prefix);
    check_suffix_stack_products(&G, &st);
    check_prefix_stack_products(&G, &prefix);
    green_set_rebuild_mode(&G, GREEN_REBUILD_COMBINE);
    check_boundary_reconstruction(&G, &Ref, &prefix, &st);
    green_set_rebuild_mode(&G, GREEN_REBUILD_TWO_SIDED);
    check_boundary_reconstruction(&G, &Ref, &prefix, &st);
    green_set_rebuild_mode(&G, GREEN_REBUILD_CENTERED);
    green_stack_build(&G, &st);
    green_stack_build_prefix(&G, &prefix);
    CHECK(!G.work.failed);
    check_suffix_stack_products(&G, &st);
    check_prefix_stack_products(&G, &prefix);
    check_boundary_reconstruction(&G, &Ref, &prefix, &st);

    UDV left;
    UDV combined;
    udv_init(&left, Lx);
    udv_init(&combined, Lx);
    udv_identity(&left);
    for (int j = 0; j < st.M; j++) {
        const int begin = st.b[j];
        const int len = st.b[j + 1] - st.b[j];
        green_build_Bblock(&G, begin, len, G.B, G.Binv, G.tmp);
        udv_lmul_centered_work(&left, G.B, &G.work);

        if (j + 1 < st.M) {
            const int tau = st.b[j + 1];
            CHECK(green_from_stack(&G, &st, &left, &combined, j + 1) == 0);
            CHECK(green_from_scratch(&Ref, tau) == 0);
            check_close_mat(Lx, G.g, Ref.g, 1e-10);
            CHECK(G.det_sign == Ref.det_sign);
            CHECK(G.det_sign == brute_det_sign(&G, tau));
        }
    }
    CHECK(green_from_left_udv(&G, &left) == 0);
    CHECK(green_from_scratch(&Ref, 0) == 0);
    check_close_mat(Lx, G.g, Ref.g, 1e-10);
    CHECK(G.det_sign == Ref.det_sign);
    CHECK(G.det_sign == brute_det_sign(&G, 0));

    check_carried_store_matches_fresh(&G, &f, &st);

    udv_free(&combined);
    udv_free(&left);
    green_stack_free(&prefix);
    green_stack_free(&st);
    green_free(&Ref);
    green_free(&G);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
}

int main(void)
{
    run_case(8, 2, 4.0);
    run_case(8, 4, 4.0);
    run_case(10, 4, 4.0);
    run_case(5, 8, 4.0);
    run_case(8, 8, 4.0);
    run_case(6, 1, 4.0);
    run_case(10, 4, 12.0);
    TEST_END();
}
