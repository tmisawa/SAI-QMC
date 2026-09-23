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
void model_free(Model *m);

#endif
