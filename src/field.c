#include "field.h"

#include <math.h>
#include <stdlib.h>

void field_init(Field *f, int n, int L, double U, double dtau, Rng *r)
{
    f->n = n;
    f->L = L;
    f->lambda = acosh(exp(dtau * U / 2.0));
    for (int isigma = 0; isigma < 2; isigma++) {
        const double sigma = (isigma == 0) ? -1.0 : 1.0;
        for (int is = 0; is < 2; is++) {
            const double s = (is == 0) ? -1.0 : 1.0;
            f->N_cache[isigma][is] = exp(-2.0 * f->lambda * sigma * s) - 1.0;
        }
    }
    f->s = malloc((size_t)L * (size_t)n * sizeof(signed char));

    for (int k = 0; k < L * n; k++) {
        f->s[k] = (rng_double(r) < 0.5) ? -1 : 1;
    }
}

void field_free(Field *f)
{
    free(f->s);
    f->n = 0;
    f->L = 0;
    f->lambda = 0.0;
    for (int isigma = 0; isigma < 2; isigma++) {
        for (int is = 0; is < 2; is++) {
            f->N_cache[isigma][is] = 0.0;
        }
    }
    f->s = NULL;
}

double field_N(const Field *f, double sigma, signed char s_il)
{
    if (sigma == 1.0 || sigma == -1.0) {
        const int isigma = (sigma > 0.0) ? 1 : 0;
        const int is = (s_il > 0) ? 1 : 0;
        return f->N_cache[isigma][is];
    }
    return exp(-2.0 * f->lambda * sigma * (double)s_il) - 1.0;
}

void field_flip_site_worldline(Field *f, int i)
{
    for (int l = 0; l < f->L; l++) {
        f->s[l * f->n + i] = (signed char)(-f->s[l * f->n + i]);
    }
}

int field_site_sum(const Field *f, int i)
{
    int m = 0;
    for (int l = 0; l < f->L; l++) {
        m += f->s[l * f->n + i];
    }
    return m;
}
