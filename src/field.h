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

/* Flips s_{i,l} for every time slice l of site i (an involution). */
void field_flip_site_worldline(Field *f, int i);
/* Returns sum_l s_{i,l}. */
int field_site_sum(const Field *f, int i);

#endif
