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
/* "open", "periodic", "antiperiodic"; NULL for a value outside LatBoundary. */
const char *lattice_boundary_name(LatBoundary bc);
/* Accepts exactly the three names above. Returns 0 on success. */
int lattice_boundary_parse(const char *name, LatBoundary *out);
int lattice_from_file(Lattice *L, const char *path);
int lattice_dump(const Lattice *L, const char *path);
void lattice_free(Lattice *L);

#endif
