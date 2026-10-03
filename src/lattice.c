#include "lattice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lattice_alloc(Lattice *L, int n)
{
    L->n = n;
    L->t = calloc((size_t)n * (size_t)n, sizeof(double));
    L->bipart = calloc((size_t)n, sizeof(int));
    L->is_bipartite = 1;
    L->type = LAT_NONE;
    L->Lx = 0;
    L->Ly = 0;
    L->has_coordinates = 0;
}

static void add_bond(Lattice *L, int i, int j, double thop)
{
    L->t[i + j * L->n] += thop;
    L->t[j + i * L->n] += thop;
}

static void lattice_set_empty(Lattice *L)
{
    L->n = 0;
    L->t = NULL;
    L->bipart = NULL;
    L->is_bipartite = 0;
    L->type = LAT_NONE;
    L->Lx = 0;
    L->Ly = 0;
    L->has_coordinates = 0;
}

static int boundary_is_valid(LatBoundary bc)
{
    return bc == LAT_BC_OPEN || bc == LAT_BC_PERIODIC ||
           bc == LAT_BC_ANTIPERIODIC;
}

/* Length 2 would cancel the antiperiodic bond against the ordinary one, and
   odd lengths are not bipartite. */
static int boundary_length_ok(LatBoundary bc, int len)
{
    return bc != LAT_BC_ANTIPERIODIC || (len >= 4 && len % 2 == 0);
}

static int boundary_closes(LatBoundary bc, int len)
{
    return bc != LAT_BC_OPEN && len > 1;
}

static double closing_amplitude(LatBoundary bc, double thop)
{
    return bc == LAT_BC_ANTIPERIODIC ? -thop : thop;
}

static int closed_odd(LatBoundary bc, int len)
{
    return boundary_closes(bc, len) && len % 2 != 0;
}

const char *lattice_boundary_name(LatBoundary bc)
{
    switch (bc) {
    case LAT_BC_OPEN:
        return "open";
    case LAT_BC_PERIODIC:
        return "periodic";
    case LAT_BC_ANTIPERIODIC:
        return "antiperiodic";
    }
    return NULL;
}

int lattice_boundary_parse(const char *name, LatBoundary *out)
{
    static const LatBoundary all[] = {LAT_BC_OPEN, LAT_BC_PERIODIC,
                                      LAT_BC_ANTIPERIODIC};
    if (name == NULL || out == NULL) {
        return 1;
    }
    for (size_t k = 0; k < sizeof all / sizeof all[0]; k++) {
        if (strcmp(name, lattice_boundary_name(all[k])) == 0) {
            *out = all[k];
            return 0;
        }
    }
    return 1;
}

int lattice_chain_bc(Lattice *L, int Lx, double thop, LatBoundary bc_x)
{
    if (!boundary_is_valid(bc_x) || !boundary_length_ok(bc_x, Lx)) {
        lattice_set_empty(L);
        return 1;
    }
    lattice_alloc(L, Lx);
    L->type = LAT_CHAIN;
    L->Lx = Lx;
    L->Ly = 1;
    L->has_coordinates = 1;
    for (int x = 0; x < Lx; x++) {
        const int xr = x + 1;
        if (xr < Lx) {
            add_bond(L, x, xr, thop);
        } else if (boundary_closes(bc_x, Lx)) {
            add_bond(L, x, 0, closing_amplitude(bc_x, thop));
        }
        L->bipart[x] = (x % 2 == 0) ? 1 : -1;
    }

    L->is_bipartite = closed_odd(bc_x, Lx) ? 0 : 1;
    return 0;
}

void lattice_chain(Lattice *L, int Lx, double thop, int pbc)
{
    (void)lattice_chain_bc(L, Lx, thop, pbc ? LAT_BC_PERIODIC : LAT_BC_OPEN);
}

int lattice_square_bc(Lattice *L, int Lx, int Ly, double thop,
                      LatBoundary bc_x, LatBoundary bc_y)
{
    if (!boundary_is_valid(bc_x) || !boundary_is_valid(bc_y) ||
        !boundary_length_ok(bc_x, Lx) || !boundary_length_ok(bc_y, Ly)) {
        lattice_set_empty(L);
        return 1;
    }
    const int n = Lx * Ly;
    lattice_alloc(L, n);
    L->type = LAT_SQUARE;
    L->Lx = Lx;
    L->Ly = Ly;
    L->has_coordinates = 1;

#define IDX(x, y) ((x) + (y)*Lx)
    for (int y = 0; y < Ly; y++) {
        for (int x = 0; x < Lx; x++) {
            const int i = IDX(x, y);
            const int xr = x + 1;
            const int yr = y + 1;

            if (xr < Lx) {
                add_bond(L, i, IDX(xr, y), thop);
            } else if (boundary_closes(bc_x, Lx)) {
                add_bond(L, i, IDX(0, y), closing_amplitude(bc_x, thop));
            }

            if (yr < Ly) {
                add_bond(L, i, IDX(x, yr), thop);
            } else if (boundary_closes(bc_y, Ly)) {
                add_bond(L, i, IDX(x, 0), closing_amplitude(bc_y, thop));
            }

            L->bipart[i] = ((x + y) % 2 == 0) ? 1 : -1;
        }
    }
#undef IDX

    L->is_bipartite = (closed_odd(bc_x, Lx) || closed_odd(bc_y, Ly)) ? 0 : 1;
    return 0;
}

void lattice_square(Lattice *L, int Lx, int Ly, double thop, int pbc)
{
    const LatBoundary bc = pbc ? LAT_BC_PERIODIC : LAT_BC_OPEN;
    (void)lattice_square_bc(L, Lx, Ly, thop, bc, bc);
}

static void detect_bipartite_from_hopping(Lattice *L)
{
    const int n = L->n;
    int *q = malloc(sizeof(int) * (size_t)n);

    for (int i = 0; i < n; i++) {
        L->bipart[i] = 0;
    }
    L->is_bipartite = 1;

    for (int s = 0; s < n && L->is_bipartite; s++) {
        if (L->bipart[s] != 0) {
            continue;
        }
        L->bipart[s] = 1;
        int head = 0;
        int tail = 0;
        q[tail++] = s;

        while (head < tail && L->is_bipartite) {
            const int i = q[head++];
            for (int j = 0; j < n; j++) {
                if (i == j || fabs(L->t[i + j * n]) <= 1e-12) {
                    continue;
                }
                const int want = -L->bipart[i];
                if (L->bipart[j] == 0) {
                    L->bipart[j] = want;
                    q[tail++] = j;
                } else if (L->bipart[j] != want) {
                    L->is_bipartite = 0;
                    break;
                }
            }
        }
    }

    if (!L->is_bipartite) {
        for (int i = 0; i < n; i++) {
            L->bipart[i] = 0;
        }
    }
    free(q);
}

int lattice_from_file(Lattice *L, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror(path);
        return 1;
    }

    int n = 0;
    if (fscanf(fp, "%d", &n) != 1 || n <= 0) {
        fprintf(stderr, "ERROR: hopping matrix size must be positive\n");
        fclose(fp);
        return 1;
    }

    lattice_alloc(L, n);
    L->type = LAT_FILE;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double v = 0.0;
            if (fscanf(fp, "%lf", &v) != 1) {
                fclose(fp);
                lattice_free(L);
                return 1;
            }
            L->t[i + j * n] = v;
        }
    }
    fclose(fp);

    for (int i = 0; i < n; i++) {
        if (fabs(L->t[i + i * n]) > 1e-12) {
            fprintf(stderr,
                    "ERROR: hopping matrix diagonal must be zero at (%d,%d)\n",
                    i, i);
            lattice_free(L);
            return 1;
        }
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (fabs(L->t[i + j * n] - L->t[j + i * n]) > 1e-12) {
                fprintf(stderr,
                        "ERROR: hopping matrix not symmetric at (%d,%d)\n", i,
                        j);
                lattice_free(L);
                return 1;
            }
        }
    }

    detect_bipartite_from_hopping(L);
    return 0;
}

int lattice_dump(const Lattice *L, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror(path);
        return 1;
    }

    const int n = L->n;
    fprintf(fp, "%d\n", n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            fprintf(fp, "%.17g%c", L->t[i + j * n], (j == n - 1) ? '\n' : ' ');
        }
    }
    fclose(fp);
    return 0;
}

void lattice_free(Lattice *L)
{
    free(L->t);
    free(L->bipart);
    L->n = 0;
    L->t = NULL;
    L->bipart = NULL;
    L->is_bipartite = 0;
    L->type = LAT_NONE;
    L->Lx = 0;
    L->Ly = 0;
    L->has_coordinates = 0;
}
