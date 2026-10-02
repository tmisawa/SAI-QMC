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
/* Overwrites every s_{i,l} with value (an involution-free reset, not a flip).
   Used by field_init=uniform after the random draw in field_init. */
void field_set_uniform(Field *f, signed char value);
void field_free(Field *f);
double field_N(const Field *f, double sigma, signed char s_il);

/* Flips s_{i,l} for every time slice l of site i (an involution). */
void field_flip_site_worldline(Field *f, int i);
/* Returns sum_l s_{i,l}. */
int field_site_sum(const Field *f, int i);
/* out[i] = sum_l s_{i,l} for every site (out has f->n entries). */
void field_site_sums(const Field *f, int *out);

#endif
