#ifndef LATTICE_H
#define LATTICE_H

typedef enum {
    LAT_NONE,
    LAT_CHAIN,
    LAT_SQUARE,
    LAT_FILE
} LatType;

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
int lattice_from_file(Lattice *L, const char *path);
int lattice_dump(const Lattice *L, const char *path);
void lattice_free(Lattice *L);

#endif
