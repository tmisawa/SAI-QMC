#include "test_util.h"
#include "lattice.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

extern void dsyev_(const char *, const char *, const int *, double *,
                   const int *, double *, double *, const int *, int *);

typedef struct {
    int i, j;
    double t;
} Bond;

/* Compares the whole matrix with a hand-written bond list (both orders). */
static void check_bonds(const Lattice *L, int n, const Bond *bonds, int nb)
{
    double *want = calloc((size_t)n * (size_t)n, sizeof(double));
    CHECK(want != NULL && L->n == n && L->t != NULL);
    if (want == NULL || L->n != n || L->t == NULL) {
        free(want);
        return;
    }
    for (int k = 0; k < nb; k++) {
        want[bonds[k].i + bonds[k].j * n] = bonds[k].t;
        want[bonds[k].j + bonds[k].i * n] = bonds[k].t;
    }
    for (int k = 0; k < n * n; k++) {
        if (L->t[k] != want[k]) {
            printf("FAIL matrix entry (%d,%d): got %.17g want %.17g\n", k % n,
                   k / n, L->t[k], want[k]);
            g_fail++;
        }
    }
    free(want);
}

static void check_empty(const Lattice *L)
{
    CHECK(L->n == 0 && L->t == NULL && L->bipart == NULL);
    CHECK(L->type == LAT_NONE && L->Lx == 0 && L->Ly == 0);
    CHECK(L->has_coordinates == 0 && L->is_bipartite == 0);
}

static void poison(Lattice *L, double *dummy)
{
    L->n = 99;
    L->t = dummy;
    L->bipart = (int *)dummy;
    L->is_bipartite = 1;
    L->type = LAT_SQUARE;
    L->Lx = 7;
    L->Ly = 7;
    L->has_coordinates = 1;
}

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a;
    const double y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Eigenvalues of the hopping matrix versus 2t cos k with k = 2 pi (m + s)/L,
   s = 1/2 for an antiperiodic and 0 for a periodic direction. A periodic
   length-2 direction carries the doubled bond, whose eigenvalues +-2t equal
   2t cos k at k = 0, pi. */
static void check_spectrum(const Lattice *L, double sx, double sy, double thop)
{
    const int n = L->n;
    double *a = malloc(sizeof(double) * (size_t)n * (size_t)n);
    double *eig = malloc(sizeof(double) * (size_t)n);
    double *want = malloc(sizeof(double) * (size_t)n);
    const int lwork = 64 * n;
    double *work = malloc(sizeof(double) * (size_t)lwork);
    CHECK(a != NULL && eig != NULL && want != NULL && work != NULL);
    if (a != NULL && eig != NULL && want != NULL && work != NULL) {
        memcpy(a, L->t, sizeof(double) * (size_t)n * (size_t)n);
        int info = 0;
        dsyev_("N", "U", &n, a, &n, eig, work, &lwork, &info);
        CHECK(info == 0);
        const double two_pi = 2.0 * acos(-1.0);
        int k = 0;
        for (int my = 0; my < L->Ly; my++) {
            for (int mx = 0; mx < L->Lx; mx++) {
                double e = 2.0 * thop * cos(two_pi * ((double)mx + sx) / L->Lx);
                if (L->Ly > 1) {
                    e += 2.0 * thop * cos(two_pi * ((double)my + sy) / L->Ly);
                }
                want[k++] = e;
            }
        }
        qsort(want, (size_t)n, sizeof(double), cmp_double);
        for (int q = 0; q < n; q++) {
            CHECK_CLOSE(eig[q], want[q], 1e-12);
        }
    }
    free(a);
    free(eig);
    free(want);
    free(work);
}

static void check_same(const Lattice *A, const Lattice *B)
{
    CHECK(A->n == B->n && A->type == B->type && A->Lx == B->Lx &&
          A->Ly == B->Ly && A->has_coordinates == B->has_coordinates &&
          A->is_bipartite == B->is_bipartite);
    if (A->n == B->n && A->n > 0) {
        const size_t nn = (size_t)A->n * (size_t)A->n;
        CHECK(memcmp(A->t, B->t, nn * sizeof(double)) == 0);
        CHECK(memcmp(A->bipart, B->bipart, (size_t)A->n * sizeof(int)) == 0);
    }
}

int main(void)
{
    Lattice L;

    /* hand-written expected matrices (t = -1) */
    CHECK(lattice_chain_bc(&L, 1, -1.0, LAT_BC_PERIODIC) == 0);
    check_bonds(&L, 1, NULL, 0);
    lattice_free(&L);

    const Bond chain2_p[] = {{0, 1, -2.0}};
    CHECK(lattice_chain_bc(&L, 2, -1.0, LAT_BC_PERIODIC) == 0);
    check_bonds(&L, 2, chain2_p, 1);
    lattice_free(&L);

    const Bond chain4_p[] = {{0, 1, -1.0}, {1, 2, -1.0}, {2, 3, -1.0},
                             {3, 0, -1.0}};
    const Bond chain4_o[] = {{0, 1, -1.0}, {1, 2, -1.0}, {2, 3, -1.0}};
    const Bond chain4_ap[] = {{0, 1, -1.0}, {1, 2, -1.0}, {2, 3, -1.0},
                              {3, 0, 1.0}};
    CHECK(lattice_chain_bc(&L, 4, -1.0, LAT_BC_PERIODIC) == 0);
    check_bonds(&L, 4, chain4_p, 4);
    lattice_free(&L);
    CHECK(lattice_chain_bc(&L, 4, -1.0, LAT_BC_OPEN) == 0);
    check_bonds(&L, 4, chain4_o, 3);
    lattice_free(&L);
    CHECK(lattice_chain_bc(&L, 4, -1.0, LAT_BC_ANTIPERIODIC) == 0);
    check_bonds(&L, 4, chain4_ap, 4);
    CHECK(L.type == LAT_CHAIN && L.Lx == 4 && L.Ly == 1);
    CHECK(L.has_coordinates == 1 && L.is_bipartite == 1);
    CHECK(L.bipart[0] == 1 && L.bipart[3] == -1);
    lattice_free(&L);

    /* 2x2 periodic: every bond is doubled */
    const Bond sq22_p[] = {{0, 1, -2.0}, {2, 3, -2.0}, {0, 2, -2.0},
                           {1, 3, -2.0}};
    CHECK(lattice_square_bc(&L, 2, 2, -1.0, LAT_BC_PERIODIC,
                            LAT_BC_PERIODIC) == 0);
    check_bonds(&L, 4, sq22_p, 4);
    lattice_free(&L);

    /* 4x2 AP/P: x closes with +1, the doubled y bond is -2 */
    const Bond sq42_app[] = {{0, 1, -1.0}, {1, 2, -1.0}, {2, 3, -1.0},
                             {3, 0, 1.0},  {4, 5, -1.0}, {5, 6, -1.0},
                             {6, 7, -1.0}, {7, 4, 1.0},  {0, 4, -2.0},
                             {1, 5, -2.0}, {2, 6, -2.0}, {3, 7, -2.0}};
    CHECK(lattice_square_bc(&L, 4, 2, -1.0, LAT_BC_ANTIPERIODIC,
                            LAT_BC_PERIODIC) == 0);
    check_bonds(&L, 8, sq42_app, 12);
    CHECK(L.type == LAT_SQUARE && L.Lx == 4 && L.Ly == 2);
    CHECK(L.has_coordinates == 1 && L.is_bipartite == 1);
    lattice_free(&L);

    /* allowed mixed boundaries; site (x, y) = x + 4 y unless noted */
    CHECK(lattice_square_bc(&L, 4, 4, -1.0, LAT_BC_PERIODIC,
                            LAT_BC_ANTIPERIODIC) == 0);
    CHECK(L.t[12 + 0 * 16] == 1.0 && L.t[3 + 0 * 16] == -1.0);
    CHECK(L.is_bipartite == 1);
    lattice_free(&L);
    CHECK(lattice_square_bc(&L, 4, 4, -1.0, LAT_BC_PERIODIC, LAT_BC_OPEN) ==
          0);
    CHECK(L.t[12 + 0 * 16] == 0.0 && L.t[3 + 0 * 16] == -1.0);
    CHECK(L.is_bipartite == 1);
    lattice_free(&L);
    CHECK(lattice_square_bc(&L, 4, 4, -1.0, LAT_BC_OPEN, LAT_BC_PERIODIC) ==
          0);
    CHECK(L.t[12 + 0 * 16] == -1.0 && L.t[3 + 0 * 16] == 0.0);
    lattice_free(&L);
    /* odd open x with antiperiodic y: site (x, y) = x + 3 y */
    CHECK(lattice_square_bc(&L, 3, 4, -1.0, LAT_BC_OPEN,
                            LAT_BC_ANTIPERIODIC) == 0);
    CHECK(L.t[9 + 0 * 12] == 1.0 && L.t[2 + 0 * 12] == 0.0);
    CHECK(L.is_bipartite == 1);
    lattice_free(&L);
    /* antiperiodic x with an odd periodic y is built but not bipartite */
    CHECK(lattice_square_bc(&L, 4, 3, -1.0, LAT_BC_ANTIPERIODIC,
                            LAT_BC_PERIODIC) == 0);
    CHECK(L.is_bipartite == 0);
    lattice_free(&L);

    /* rejected: antiperiodic length odd or below 4, values outside the enum */
    double dummy[1] = {0.0};
    const int bad_chain[] = {1, 2, 3, 5};
    for (size_t k = 0; k < sizeof bad_chain / sizeof bad_chain[0]; k++) {
        poison(&L, dummy);
        CHECK(lattice_chain_bc(&L, bad_chain[k], -1.0, LAT_BC_ANTIPERIODIC) ==
              1);
        check_empty(&L);
        lattice_free(&L);
    }
    const int bad_sq[][2] = {{2, 4}, {3, 4}};
    for (size_t k = 0; k < sizeof bad_sq / sizeof bad_sq[0]; k++) {
        poison(&L, dummy);
        CHECK(lattice_square_bc(&L, bad_sq[k][0], bad_sq[k][1], -1.0,
                                LAT_BC_ANTIPERIODIC, LAT_BC_PERIODIC) == 1);
        check_empty(&L);
    }
    const int bad_y[] = {1, 2, 5};
    for (size_t k = 0; k < sizeof bad_y / sizeof bad_y[0]; k++) {
        poison(&L, dummy);
        CHECK(lattice_square_bc(&L, 4, bad_y[k], -1.0, LAT_BC_PERIODIC,
                                LAT_BC_ANTIPERIODIC) == 1);
        check_empty(&L);
    }
    poison(&L, dummy);
    CHECK(lattice_chain_bc(&L, 4, -1.0, (LatBoundary)3) == 1);
    check_empty(&L);
    poison(&L, dummy);
    CHECK(lattice_square_bc(&L, 4, 4, -1.0, LAT_BC_PERIODIC,
                            (LatBoundary)-1) == 1);
    check_empty(&L);

    /* the legacy pbc wrappers map to periodic/open (same implementation) */
    const int chain_len[] = {1, 2, 3, 4, 6};
    for (size_t k = 0; k < sizeof chain_len / sizeof chain_len[0]; k++) {
        for (int pbc = 0; pbc <= 1; pbc++) {
            Lattice A, B;
            lattice_chain(&A, chain_len[k], -1.0, pbc);
            CHECK(lattice_chain_bc(&B, chain_len[k], -1.0,
                                   pbc ? LAT_BC_PERIODIC : LAT_BC_OPEN) == 0);
            check_same(&A, &B);
            lattice_free(&A);
            lattice_free(&B);
        }
    }
    const int sq_len[][2] = {{4, 4}, {2, 2}, {3, 4}, {1, 4}, {4, 2}, {4, 1}};
    for (size_t k = 0; k < sizeof sq_len / sizeof sq_len[0]; k++) {
        for (int pbc = 0; pbc <= 1; pbc++) {
            Lattice A, B;
            const LatBoundary bc = pbc ? LAT_BC_PERIODIC : LAT_BC_OPEN;
            lattice_square(&A, sq_len[k][0], sq_len[k][1], -1.0, pbc);
            CHECK(lattice_square_bc(&B, sq_len[k][0], sq_len[k][1], -1.0, bc,
                                    bc) == 0);
            check_same(&A, &B);
            lattice_free(&A);
            lattice_free(&B);
        }
    }

    /* spectra: half-integer momenta along antiperiodic directions */
    CHECK(lattice_chain_bc(&L, 6, -1.0, LAT_BC_ANTIPERIODIC) == 0);
    check_spectrum(&L, 0.5, 0.0, -1.0);
    lattice_free(&L);
    CHECK(lattice_square_bc(&L, 4, 4, -1.0, LAT_BC_ANTIPERIODIC,
                            LAT_BC_PERIODIC) == 0);
    check_spectrum(&L, 0.5, 0.0, -1.0);
    lattice_free(&L);
    CHECK(lattice_square_bc(&L, 4, 4, -1.0, LAT_BC_ANTIPERIODIC,
                            LAT_BC_ANTIPERIODIC) == 0);
    check_spectrum(&L, 0.5, 0.5, -1.0);
    lattice_free(&L);
    CHECK(lattice_square_bc(&L, 4, 2, -1.0, LAT_BC_ANTIPERIODIC,
                            LAT_BC_PERIODIC) == 0);
    check_spectrum(&L, 0.5, 0.0, -1.0);
    lattice_free(&L);

    /* names */
    CHECK(strcmp(lattice_boundary_name(LAT_BC_OPEN), "open") == 0);
    CHECK(strcmp(lattice_boundary_name(LAT_BC_PERIODIC), "periodic") == 0);
    CHECK(strcmp(lattice_boundary_name(LAT_BC_ANTIPERIODIC), "antiperiodic") ==
          0);
    CHECK(lattice_boundary_name((LatBoundary)3) == NULL);
    const LatBoundary all[] = {LAT_BC_OPEN, LAT_BC_PERIODIC,
                               LAT_BC_ANTIPERIODIC};
    for (size_t k = 0; k < 3; k++) {
        LatBoundary got = LAT_BC_OPEN;
        CHECK(lattice_boundary_parse(lattice_boundary_name(all[k]), &got) == 0);
        CHECK(got == all[k]);
    }
    LatBoundary unused = LAT_BC_OPEN;
    CHECK(lattice_boundary_parse("Periodic", &unused) != 0);
    CHECK(lattice_boundary_parse("apbc", &unused) != 0);
    CHECK(lattice_boundary_parse("", &unused) != 0);
    CHECK(lattice_boundary_parse(NULL, &unused) != 0);
    TEST_END();
}

