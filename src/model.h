#ifndef MODEL_H
#define MODEL_H

#include "lattice.h"

typedef struct {
    int n;
    double U;
    double mu;
    double dtau;
    double *K;
    double *expK;
    double *expKinv;
    int half_filling;
    int ph_symmetric;
    const int *bipart;
} Model;

void model_init(Model *m, const Lattice *L, double U, double dtau, int half,
                double mu_in);
/* The sign-free particle-hole symmetric path: half filling (half != 0) on a
   lattice whose hopping is bipartite. model_init sets ph_symmetric with it. */
int model_ph_symmetric(const Lattice *L, int half);
void model_free(Model *m);

#endif
