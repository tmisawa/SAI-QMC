#include "model.h"

#include "linalg.h"

#include <stdlib.h>
#include <string.h>

void model_init(Model *m, const Lattice *L, double U, double dtau, int half,
                double mu_in)
{
    const int n = L->n;
    m->n = n;
    m->U = U;
    m->dtau = dtau;
    m->mu = half ? U / 2.0 : mu_in;
    m->half_filling = half ? 1 : 0;
    m->ph_symmetric = (half && L->is_bipartite) ? 1 : 0;
    m->bipart = L->bipart;

    m->K = malloc(sizeof(double) * (size_t)n * (size_t)n);
    m->expK = malloc(sizeof(double) * (size_t)n * (size_t)n);
    m->expKinv = malloc(sizeof(double) * (size_t)n * (size_t)n);

    memcpy(m->K, L->t, sizeof(double) * (size_t)n * (size_t)n);
    for (int i = 0; i < n; i++) {
        m->K[i + i * n] -= m->mu;
    }

    la_expm_sym(n, m->K, -dtau, m->expK);
    la_expm_sym(n, m->K, dtau, m->expKinv);
}

void model_free(Model *m)
{
    free(m->K);
    free(m->expK);
    free(m->expKinv);
    m->n = 0;
    m->U = 0.0;
    m->mu = 0.0;
    m->dtau = 0.0;
    m->K = NULL;
    m->expK = NULL;
    m->expKinv = NULL;
    m->half_filling = 0;
    m->ph_symmetric = 0;
    m->bipart = NULL;
}
