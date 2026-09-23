#ifndef RNG_H
#define RNG_H

#include <stdint.h>

typedef struct {
    uint64_t s[4];
} Rng;

void rng_seed(Rng *r, uint64_t seed);
double rng_double(Rng *r);

#endif
