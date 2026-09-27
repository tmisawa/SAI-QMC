#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "lattice.h"
#include "model.h"

#include <stdlib.h>
#include <string.h>

enum { NS = 4, LT = 8, STAB = 4 };

/* log|det(1 + B_{L-1}...B_0)| for spin sigma, built naively.
   B_l[i+j*n] = expK[i+j*n] * exp(sigma*lambda*s[l*n+j] - dtau*U/2). */
static double direct_logdet(const Model *m, double lambda, const signed char *s,
                            double sigma)
{
    const int n = NS;
    double A[NS * NS], B[NS * NS], T[NS * NS];
    memset(A, 0, sizeof A);
    for (int i = 0; i < n; i++) {
        A[i + i * n] = 1.0;
    }
    for (int l = 0; l < LT; l++) {
        for (int j = 0; j < n; j++) {
            const double d = exp(sigma * lambda * s[l * n + j] - m->dtau * m->U / 2.0);
            for (int i = 0; i < n; i++) {
                B[i + j * n] = m->expK[i + j * n] * d;
            }
        }
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                double acc = 0.0;
                for (int k = 0; k < n; k++) {
                    acc += B[i + k * n] * A[k + j * n];
                }
                T[i + j * n] = acc;
            }
        }
        memcpy(A, T, sizeof A);
    }
    for (int i = 0; i < n; i++) {
        A[i + i * n] += 1.0;
    }
    /* Gaussian elimination with partial pivoting */
    double logabs = 0.0;
    for (int c = 0; c < n; c++) {
        int piv = c;
        for (int r = c + 1; r < n; r++) {
            if (fabs(A[r + c * n]) > fabs(A[piv + c * n])) {
                piv = r;
            }
        }
        if (piv != c) {
            for (int k = 0; k < n; k++) {
                const double t = A[c + k * n];
                A[c + k * n] = A[piv + k * n];
                A[piv + k * n] = t;
            }
        }
        logabs += log(fabs(A[c + c * n]));
        for (int r = c + 1; r < n; r++) {
            const double f = A[r + c * n] / A[c + c * n];
            for (int k = c; k < n; k++) {
                A[r + k * n] -= f * A[c + k * n];
            }
        }
    }
    return logabs;
}

static double direct_logw(const Model *m, const Field *f, const signed char *s)
{
    return direct_logdet(m, f->lambda, s, 1.0) + direct_logdet(m, f->lambda, s, -1.0);
}

typedef struct {
    Model m;
    Rng r;
    Field f;
    Dqmc D;
} Slot;

static void slot_init(Slot *x, const Lattice *L, double dtau, uint64_t seed)
{
    model_init(&x->m, L, 4.0, dtau, 1, 0.0);
    rng_seed(&x->r, seed);
    field_init(&x->f, NS, LT, 4.0, dtau, &x->r);
    CHECK(dqmc_init_modes(&x->D, &x->m, &x->f, &x->r, STAB, NULL,
                          DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE) == 0);
    CHECK(x->D.use_ph == 1);
}

static void slot_free(Slot *x)
{
    dqmc_free(&x->D);
    field_free(&x->f);
    model_free(&x->m);
}

int main(void)
{
    Lattice L;
    lattice_chain(&L, NS, -1.0, 1);
    Slot a, b;
    slot_init(&a, &L, 0.1, 11);
    slot_init(&b, &L, 0.125, 23);
    for (int k = 0; k < 3; k++) {
        dqmc_sweep(&a.D);
        dqmc_sweep(&b.D);
    }

    /* the naive B matches green_build_B (anchors the direct convention) */
    {
        double Bref[NS * NS], Bg[NS * NS];
        green_build_B(&a.D.Gu, 2, Bg);
        for (int j = 0; j < NS; j++) {
            const double d = exp(a.f.lambda * a.f.s[2 * NS + j] - a.m.dtau * a.m.U / 2.0);
            for (int i = 0; i < NS; i++) {
                Bref[i + j * NS] = a.m.expK[i + j * NS] * d;
            }
        }
        for (int k = 0; k < NS * NS; k++) {
            CHECK_CLOSE(Bg[k], Bref[k], 1e-14 * (1.0 + fabs(Bref[k])));
        }
    }

    const size_t len = (size_t)NS * LT;
    signed char *sa = malloc(len), *sb = malloc(len), *g0 = malloc(len);
    memcpy(sa, a.f.s, len);
    memcpy(sb, b.f.s, len);
    double *ga = malloc(sizeof(double) * NS * NS);
    memcpy(ga, a.D.Gu.g, sizeof(double) * NS * NS);
    const int cur_l = a.D.Gu.cur_l;

    double waa, wbb, wab, wba;
    int sg;
    CHECK(dqmc_log_weight(&a.D, &waa, &sg) == 0);
    CHECK(dqmc_log_weight(&b.D, &wbb, &sg) == 0);
    CHECK(dqmc_log_weight_of(&a.D, sb, &wab, &sg) == 0 && sg == 1);
    CHECK(dqmc_log_weight_of(&b.D, sa, &wba, &sg) == 0 && sg == 1);

    /* no mutation of the own field or the Green function */
    CHECK(memcmp(a.f.s, sa, len) == 0);
    CHECK(memcmp(a.D.Gu.g, ga, sizeof(double) * NS * NS) == 0);
    CHECK(a.D.Gu.cur_l == cur_l);
    CHECK(a.D.status == 0);

    /* exchange ratio against direct determinants of both spins (constants cancel) */
    const double logR = wab + wba - waa - wbb;
    const double logR_ref = direct_logw(&a.m, &a.f, sb) + direct_logw(&b.m, &b.f, sa) -
                            direct_logw(&a.m, &a.f, sa) - direct_logw(&b.m, &b.f, sb);
    CHECK_CLOSE(logR, logR_ref, 1e-9 * (1.0 + fabs(logR_ref)));
    /* the reverse move (a holds C_b, b holds C_a) has the opposite log ratio */
    CHECK_CLOSE(waa + wbb - wab - wba, -logR, 1e-12 * (1.0 + fabs(logR)));
    /* single-slot differences also agree (the -lambda*sum(s) term is config-dependent) */
    CHECK_CLOSE(wab - waa,
                direct_logw(&a.m, &a.f, sb) - direct_logw(&a.m, &a.f, sa), 1e-9);

    /* replace_field: the field is copied and G equals a from-scratch rebuild */
    CHECK(dqmc_replace_field(&a.D, sb) == 0);
    CHECK(memcmp(a.f.s, sb, len) == 0);
    CHECK(a.D.Gu.cur_l == 0);
    CHECK(a.D.carried_prefix_valid == 0 && a.D.carried_suffix_valid == 0);
    CHECK(a.D.sign == 1.0);
    {
        Slot c;
        slot_init(&c, &L, 0.1, 99);
        memcpy(c.f.s, sb, len);
        CHECK(green_from_scratch(&c.D.Gu, 0) == 0);
        for (int k = 0; k < NS * NS; k++) {
            CHECK_CLOSE(a.D.Gu.g[k], c.D.Gu.g[k], 1e-12);
        }
        slot_free(&c);
    }
    double w_after;
    CHECK(dqmc_log_weight(&a.D, &w_after, &sg) == 0);
    CHECK_CLOSE(w_after, wab, 1e-12 * (1.0 + fabs(wab)));
    /* the chain keeps running after a replacement */
    dqmc_sweep(&a.D);
    CHECK(a.D.status == 0);

    /* NULL / failed-state guards */
    memcpy(g0, a.f.s, len);
    CHECK(dqmc_log_weight_of(&a.D, NULL, &w_after, &sg) != 0);
    CHECK(dqmc_replace_field(&a.D, NULL) != 0);
    CHECK(memcmp(a.f.s, g0, len) == 0);

    free(sa);
    free(sb);
    free(g0);
    free(ga);
    slot_free(&a);
    slot_free(&b);
    lattice_free(&L);
    TEST_END();
}
