#ifndef LATTICE_H
#define LATTICE_H

typedef enum {
    LAT_NONE,
    LAT_CHAIN,
    LAT_SQUARE,
    LAT_FILE
} LatType;

/* Boundary of one built-in direction. OPEN and PERIODIC equal the legacy
   pbc=0 and pbc=1. */
typedef enum {
    LAT_BC_OPEN = 0,
    LAT_BC_PERIODIC = 1,
    LAT_BC_ANTIPERIODIC = 2
} LatBoundary;

typedef struct {
    int n;
    double *t;
    int *bipart;
    int is_bipartite;
    LatType type;
    int Lx;
    int Ly;
    int has_coordinates;
} Lattice;

void lattice_chain(Lattice *L, int Lx, double thop, int pbc);
void lattice_square(Lattice *L, int Lx, int Ly, double thop, int pbc);
/* The closing bond of a direction is +thop (periodic), -thop (antiperiodic)
   or absent (open). Returns 0 on success. Returns 1 and leaves *L empty for a
   value outside LatBoundary or an antiperiodic direction whose length is odd
   or below 4. */
int lattice_chain_bc(Lattice *L, int Lx, double thop, LatBoundary bc_x);
int lattice_square_bc(Lattice *L, int Lx, int Ly, double thop,
                      LatBoundary bc_x, LatBoundary bc_y);
/* lattice_square_bc plus the two diagonals (x+1,y+1) and (x+1,y-1) with
   element tprime. A diagonal is absent when it crosses an open direction and
   changes sign once per antiperiodic direction it crosses; a length-2
   periodic direction accumulates both images, as for the nearest neighbours.
   tprime == 0 returns exactly lattice_square_bc. With tprime != 0 both
   lengths must be >= 2, and the sublattice labels come from the nonzero
   elements of the matrix itself (exact comparison with 0, no tolerance):
   checkerboard labels when they fit, otherwise a breadth-first two-colouring
   seeded with the checkerboard label of each component. When no colouring
   exists is_bipartite = 0 and the checkerboard labels are kept. Returns 1 and
   leaves *L empty for an invalid boundary or length. */
int lattice_square_tprime_bc(Lattice *L, int Lx, int Ly, double thop,
                             double tprime, LatBoundary bc_x,
                             LatBoundary bc_y);
/* "open", "periodic", "antiperiodic"; NULL for a value outside LatBoundary. */
const char *lattice_boundary_name(LatBoundary bc);
/* Accepts exactly the three names above. Returns 0 on success. */
int lattice_boundary_parse(const char *name, LatBoundary *out);
int lattice_from_file(Lattice *L, const char *path);
/* 1 when t_ij == t_ji for every pair (no tolerance). */
int lattice_is_exactly_symmetric(const Lattice *L);
/* Replaces is_bipartite and bipart by an exact test of the matrix itself:
   bipartite only when every diagonal element is exactly 0, the matrix is
   exactly symmetric and the exactly nonzero off-diagonal elements admit a
   two-colouring (each component starts from +1); otherwise is_bipartite = 0
   and the labels are 0. lattice_from_file keeps its 1e-12 tolerances for
   legacy inputs; inputs with the newer keys use this test. Returns 1 on
   allocation failure. */
int lattice_apply_exact_bipartite(Lattice *L);
int lattice_dump(const Lattice *L, const char *path);
void lattice_free(Lattice *L);

#endif
