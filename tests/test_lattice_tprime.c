/* Square lattice with next-nearest hopping tprime (plan T2): hand-written
   4x2 tables, an independent image-sum construction for 4x4 under every
   boundary pair, spectral moments against the product dispersion, the
   tprime = 0 identity with lattice_square_bc, bipartiteness taken from the
   exact nonzero pattern, and the exact test for hopping files. */
#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "lattice.h"
#include "model.h"
#include "rng.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const LatBoundary P = LAT_BC_PERIODIC;
static const LatBoundary A = LAT_BC_ANTIPERIODIC;
static const LatBoundary O = LAT_BC_OPEN;

/* Element (i,j) from the sum over images of the eight displacements:
   unwrapped targets beyond an open edge are dropped, an antiperiodic
   direction contributes (-1)^winding. Written independently of add_bond. */
static double image_sum(int Lx, int Ly, double t, double tp, LatBoundary bx,
                        LatBoundary by, int i, int j)
{
    static const int d[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const int xi = i % Lx, yi = i / Lx;
    double sum = 0.0;
    for (int k = 0; k < 8; k++) {
        const int ux = xi + d[k][0];
        const int uy = yi + d[k][1];
        const int wx = (ux >= 0) ? ux / Lx : -((-ux + Lx - 1) / Lx);
        const int wy = (uy >= 0) ? uy / Ly : -((-uy + Ly - 1) / Ly);
        if ((wx != 0 && (bx == O || Lx == 1)) || (wy != 0 && (by == O || Ly == 1))) {
            continue;
        }
        const int x = ux - wx * Lx;
        const int y = uy - wy * Ly;
        if (x + y * Lx != j) {
            continue;
        }
        double amp = (k < 4) ? t : tp;
        if (bx == A && wx % 2 != 0) {
            amp = -amp;
        }
        if (by == A && wy % 2 != 0) {
            amp = -amp;
        }
        sum += amp;
    }
    return sum;
}

static int labels_fit(const Lattice *L)
{
    for (int i = 0; i < L->n; i++) {
        if (L->bipart[i] != 1 && L->bipart[i] != -1) {
            return 0;
        }
        for (int j = 0; j < L->n; j++) {
            if (i != j && L->t[i + j * L->n] != 0.0 &&
                L->bipart[i] * L->bipart[j] != -1) {
                return 0;
            }
        }
    }
    return 1;
}

/* Tr(T^p) for p = 1..6 equals the power sums of
   eps = t (a_x + a_y) + tp a_x a_y, a = eigenvalues of the 1D adjacency. */
static void ring_eigs(int L, LatBoundary bc, double *a)
{
    const double pi = acos(-1.0);
    for (int m = 0; m < L; m++) {
        if (bc == O || L == 1) {
            a[m] = L == 1 ? 0.0 : 2.0 * cos(pi * (m + 1) / (L + 1));
        } else if (bc == P) {
            a[m] = 2.0 * cos(2.0 * pi * m / L);
        } else {
            a[m] = 2.0 * cos(pi * (2 * m + 1) / L);
        }
    }
}

static void check_moments(const Lattice *L, int Lx, int Ly, double t, double tp,
                          LatBoundary bx, LatBoundary by)
{
    const int n = L->n;
    double ax[8], ay[8];
    ring_eigs(Lx, bx, ax);
    ring_eigs(Ly, by, ay);
    double *pw = malloc(sizeof(double) * n * n);
    double *tmp = malloc(sizeof(double) * n * n);
    memcpy(pw, L->t, sizeof(double) * n * n);
    for (int p = 1; p <= 6; p++) {
        double tr = 0.0, ref = 0.0, scale = 0.0;
        for (int i = 0; i < n; i++) {
            tr += pw[i + i * n];
        }
        for (int mx = 0; mx < Lx; mx++) {
            for (int my = 0; my < Ly; my++) {
                const double e = t * (ax[mx] + ay[my]) + tp * ax[mx] * ay[my];
                ref += pow(e, p);
                scale += pow(fabs(e), p);
            }
        }
        CHECK_CLOSE(tr, ref, 1e-12 * (1.0 + scale));
        /* pw <- pw * T */
        for (int j = 0; j < n; j++) {
            for (int i = 0; i < n; i++) {
                double s = 0.0;
                for (int k = 0; k < n; k++) {
                    s += pw[i + k * n] * L->t[k + j * n];
                }
                tmp[i + j * n] = s;
            }
        }
        memcpy(pw, tmp, sizeof(double) * n * n);
    }
    free(pw);
    free(tmp);
}

static void check_against_images(int Lx, int Ly, double t, double tp,
                                 LatBoundary bx, LatBoundary by)
{
    Lattice L;
    CHECK(lattice_square_tprime_bc(&L, Lx, Ly, t, tp, bx, by) == 0);
    CHECK(L.n == Lx * Ly && L.type == LAT_SQUARE && L.has_coordinates);
    for (int i = 0; i < L.n; i++) {
        for (int j = 0; j < L.n; j++) {
            const double ref = (i == j) ? 0.0 : image_sum(Lx, Ly, t, tp, bx, by, i, j);
            CHECK_CLOSE(L.t[i + j * L.n], ref, 1e-15);
        }
    }
    check_moments(&L, Lx, Ly, t, tp, bx, by);
    lattice_free(&L);
}

/* 4x2, t = -1, tp = -0.2, x = i % 4, y = i / 4. The length-2 periodic y
   direction doubles every vertical and every diagonal element. */
static void check_4x2_tables(void)
{
    const double t = -1.0, tp = -0.2;
    for (int ap = 0; ap < 2; ap++) {
        double ref[64] = {0};
#define SET(i, j, v) (ref[(i) + 8 * (j)] = (v), ref[(j) + 8 * (i)] = (v))
        for (int y = 0; y < 2; y++) {
            for (int x = 0; x < 3; x++) {
                SET(x + 4 * y, x + 1 + 4 * y, t);      /* row bonds */
            }
            SET(3 + 4 * y, 0 + 4 * y, ap ? -t : t);    /* closing bond */
        }
        for (int x = 0; x < 4; x++) {
            SET(x, x + 4, 2.0 * t);                    /* vertical, doubled */
        }
        for (int x = 0; x < 3; x++) {
            SET(x, x + 1 + 4, 2.0 * tp);               /* (x,0)-(x+1,1) */
            SET(x + 4, x + 1, 2.0 * tp);               /* (x,1)-(x+1,0) */
        }
        SET(3, 0 + 4, ap ? -2.0 * tp : 2.0 * tp);      /* across x */
        SET(3 + 4, 0, ap ? -2.0 * tp : 2.0 * tp);
#undef SET
        Lattice L;
        CHECK(lattice_square_tprime_bc(&L, 4, 2, t, tp, ap ? A : P, P) == 0);
        for (int k = 0; k < 64; k++) {
            CHECK(L.t[k] == ref[k]);
        }
        CHECK(L.is_bipartite == 0);
        lattice_free(&L);
    }
}

static void check_tprime_zero_identity(void)
{
    const LatBoundary bcs[3] = {O, P, A};
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            for (int z = 0; z < 2; z++) {
                Lattice old, now;
                CHECK(lattice_square_bc(&old, 4, 4, -1.0, bcs[a], bcs[b]) == 0);
                CHECK(lattice_square_tprime_bc(&now, 4, 4, -1.0, z ? -0.0 : 0.0,
                                               bcs[a], bcs[b]) == 0);
                CHECK(memcmp(old.t, now.t, sizeof(double) * 16 * 16) == 0);
                CHECK(memcmp(old.bipart, now.bipart, sizeof(int) * 16) == 0);
                CHECK(old.is_bipartite == now.is_bipartite);
                lattice_free(&old);
                lattice_free(&now);
            }
        }
    }
    /* the legacy degenerate shapes keep their tprime = 0 behaviour */
    Lattice old, now;
    CHECK(lattice_square_bc(&old, 4, 1, -1.0, P, P) == 0);
    CHECK(lattice_square_tprime_bc(&now, 4, 1, -1.0, 0.0, P, P) == 0);
    CHECK(memcmp(old.t, now.t, sizeof(double) * 4 * 4) == 0);
    lattice_free(&old);
    lattice_free(&now);
}

static void check_bipartite(void)
{
    Lattice L;
    /* t = 0: only diagonals, two components, eta = (-1)^x */
    CHECK(lattice_square_tprime_bc(&L, 4, 4, 0.0, -0.2, P, P) == 0);
    CHECK(L.is_bipartite == 1);
    CHECK(labels_fit(&L));
    for (int i = 0; i < 16; i++) {
        CHECK(L.bipart[i] == (((i % 4) % 2 == 0) ? 1 : -1));
    }
    CHECK(model_ph_symmetric(&L, 1) == 1);
    CHECK(model_ph_symmetric(&L, 0) == 0);
    lattice_free(&L);

    /* nearest plus next-nearest: triangles, not bipartite; labels kept */
    CHECK(lattice_square_tprime_bc(&L, 4, 4, -1.0, -0.2, P, P) == 0);
    CHECK(L.is_bipartite == 0);
    CHECK(model_ph_symmetric(&L, 1) == 0);
    for (int i = 0; i < 16; i++) {
        CHECK(L.bipart[i] == (((i % 4 + i / 4) % 2 == 0) ? 1 : -1));
    }
    lattice_free(&L);

    /* a tiny but nonzero tprime is not ignored */
    CHECK(lattice_square_tprime_bc(&L, 4, 4, -1.0, 1e-14, P, P) == 0);
    CHECK(L.is_bipartite == 0);
    CHECK(L.t[0 + 5 * 16] == 1e-14);
    lattice_free(&L);

    /* open 4x2 with t = 0: still bipartite */
    CHECK(lattice_square_tprime_bc(&L, 4, 2, 0.0, -0.2, O, O) == 0);
    CHECK(L.is_bipartite == 1 && labels_fit(&L));
    lattice_free(&L);

    /* length-1 directions and invalid boundaries are rejected */
    CHECK(lattice_square_tprime_bc(&L, 4, 1, -1.0, -0.2, P, P) != 0);
    CHECK(L.t == NULL && L.n == 0);
    CHECK(lattice_square_tprime_bc(&L, 1, 4, -1.0, -0.2, P, P) != 0);
    CHECK(lattice_square_tprime_bc(&L, 2, 4, -1.0, -0.2, A, P) != 0);
    CHECK(lattice_square_tprime_bc(&L, 4, 3, -1.0, -0.2, P, A) != 0);
}

/* The PH fast path built from the derived labels gives the same weight as
   the explicit two-spin evaluation for the t = 0, tprime != 0 lattice. */
static void check_ph_weight(void)
{
    Lattice L;
    CHECK(lattice_square_tprime_bc(&L, 4, 4, 0.0, -0.2, P, P) == 0);
    Model ph, two;
    model_init(&ph, &L, 4.0, 0.1, 1, 0.0);
    model_init(&two, &L, 4.0, 0.1, 0, 2.0);
    CHECK(ph.ph_symmetric == 1 && two.ph_symmetric == 0);
    Rng r1, r2;
    rng_seed(&r1, 7);
    rng_seed(&r2, 7);
    Field f1, f2;
    field_init(&f1, 16, 10, 4.0, 0.1, &r1);
    field_init(&f2, 16, 10, 4.0, 0.1, &r2);
    Dqmc D1, D2;
    CHECK(dqmc_init_modes(&D1, &ph, &f1, &r1, 5, NULL, DQMC_SWEEP_FORWARD,
                          GREEN_REBUILD_COMBINE) == 0);
    CHECK(dqmc_init_modes(&D2, &two, &f2, &r2, 5, NULL, DQMC_SWEEP_FORWARD,
                          GREEN_REBUILD_COMBINE) == 0);
    CHECK(D1.use_ph == 1 && D2.use_ph == 0);
    double w1 = 0.0, w2 = 0.0;
    int s1 = 0, s2 = 0;
    CHECK(dqmc_log_weight(&D1, &w1, &s1) == 0);
    CHECK(dqmc_log_weight(&D2, &w2, &s2) == 0);
    CHECK_CLOSE(w1, w2, 1e-9 * (1.0 + fabs(w2)));
    CHECK(s1 == 1 && s2 == 1);
    for (int k = 0; k < 16 * 16; k++) {
        CHECK_CLOSE(D1.Gd.g[k], D2.Gd.g[k], 1e-10);
    }
    dqmc_free(&D1);
    dqmc_free(&D2);
    field_free(&f1);
    field_free(&f2);
    model_free(&ph);
    model_free(&two);
    lattice_free(&L);
}

/* Hopping files: the legacy reader ignores elements up to 1e-12 in its
   bipartite test, while lattice_apply_exact_bipartite (used for inputs with
   the newer keys) decides from the matrix that is propagated (review R1). */
static int file_lattice(const char *text, Lattice *L)
{
    const char *path = "tests/tmp_lattice_exact.txt";
    FILE *fp = fopen(path, "w");
    fputs(text, fp);
    fclose(fp);
    const int rc = lattice_from_file(L, path);
    remove(path);
    return rc;
}

static void check_file_exact(void)
{
    Lattice L;
    /* triangle with tiny elements: legacy says bipartite, exact does not */
    CHECK(file_lattice("3\n0 -1e-13 -1e-13\n-1e-13 0 -1e-13\n-1e-13 -1e-13 0\n", &L) == 0);
    CHECK(L.is_bipartite == 1);
    CHECK(lattice_apply_exact_bipartite(&L) == 0);
    CHECK(L.is_bipartite == 0 && L.bipart[0] == 0 && L.bipart[2] == 0);
    CHECK(model_ph_symmetric(&L, 1) == 0);
    lattice_free(&L);
    /* a 4-ring is bipartite under both tests */
    const char *ring = "4\n0 -1 0 -1\n-1 0 -1 0\n0 -1 0 -1\n-1 0 -1 0\n";
    CHECK(file_lattice(ring, &L) == 0);
    CHECK(lattice_apply_exact_bipartite(&L) == 0);
    CHECK(L.is_bipartite == 1 && labels_fit(&L));
    CHECK(L.bipart[0] == 1 && L.bipart[1] == -1 && L.bipart[2] == 1);
    lattice_free(&L);
    /* a tiny diagonal element is accepted by the reader but breaks PH */
    CHECK(file_lattice("4\n1e-13 -1 0 -1\n-1 0 -1 0\n0 -1 0 -1\n-1 0 -1 0\n", &L) == 0);
    CHECK(L.is_bipartite == 1);
    CHECK(lattice_apply_exact_bipartite(&L) == 0);
    CHECK(L.is_bipartite == 0);
    lattice_free(&L);
    /* a tiny asymmetry is accepted by the reader but is not exact symmetry */
    CHECK(file_lattice("2\n0 -1\n-1.0000000000001 0\n", &L) == 0);
    CHECK(lattice_is_exactly_symmetric(&L) == 0);
    CHECK(lattice_apply_exact_bipartite(&L) == 0);
    CHECK(L.is_bipartite == 0);
    lattice_free(&L);
    /* no bonds at all: trivially bipartite */
    CHECK(file_lattice("2\n0 0\n0 0\n", &L) == 0);
    CHECK(lattice_is_exactly_symmetric(&L) == 1);
    CHECK(lattice_apply_exact_bipartite(&L) == 0);
    CHECK(L.is_bipartite == 1 && L.bipart[0] == 1 && L.bipart[1] == 1);
    lattice_free(&L);
}

int main(void)
{
    check_file_exact();
    check_4x2_tables();
    const LatBoundary bcs[3] = {O, P, A};
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            check_against_images(4, 4, -1.0, -0.2, bcs[a], bcs[b]);
            check_against_images(4, 4, 0.0, 0.3, bcs[a], bcs[b]);
        }
        check_against_images(4, 2, -1.0, -0.2, bcs[a], a == 2 ? P : bcs[a]);
        check_against_images(2, 4, -1.0, 0.25, P, bcs[a]);
    }
    check_against_images(2, 2, -1.0, -0.2, P, P);
    check_against_images(4, 2, -1.0, -0.2, O, P);
    check_tprime_zero_identity();
    check_bipartite();
    check_ph_weight();
    TEST_END();
}
