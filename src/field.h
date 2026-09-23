#ifndef FIELD_H
#define FIELD_H

#include "rng.h"

typedef struct {
    int n;
    int L;
    double lambda;
    double N_cache[2][2];
    signed char *s;
} Field;

void field_init(Field *f, int n, int L, double U, double dtau, Rng *r);
void field_free(Field *f);
double field_N(const Field *f, double sigma, signed char s_il);

#endif
