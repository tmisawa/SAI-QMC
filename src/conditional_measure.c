#include "conditional_measure.h"

#include <math.h>
#include <stddef.h>

/* Read the effective delayed Green without flushing or changing any state. */
static double element(const Green *g, int row, int col)
{
    double value = g->g[row + col * g->n];
    for (int k = 0; k < g->delay_count; k++) {
        value -= g->delay_c[row + k * g->n] * g->delay_v[col + k * g->n];
    }
    return value;
}

int conditional_measure_local(const Model *m, const Green *g, int site,
                               double Ru, double Rd, double *D, double *K)
{
    if (m == NULL || g == NULL || D == NULL || K == NULL ||
        !m->ph_symmetric || !m->half_filling || g->n != m->n ||
        site < 0 || site >= m->n || !isfinite(Ru) || !isfinite(Rd) ||
        (Ru < 0.0 && Rd > 0.0) || (Ru > 0.0 && Rd < 0.0)) {
        return 1;
    }
    /* Divide numerator and denominator by h^2. In particular, neither
       g_ii^2 nor Ru*Rd needs to be formed at large finite Green values. */
    const double h = fmax(1.0, fmax(fabs(Ru), fabs(Rd)));
    const double invh = 1.0 / h, u = Ru / h, d = Rd / h;
    const double den = invh * invh + u * d;
    if (!(den > 0.0) || !isfinite(den)) {
        return 1;
    }
    const double diag = element(g, site, site);
    const double doublon = 2.0 * (diag / h) * ((1.0 - diag) / h) / den;
    double kinetic = 0.0;
    for (int j = 0; j < m->n; j++) {
        if (j == site || m->K[site + j * m->n] == 0.0) {
            continue;
        }
        const double ji = element(g, j, site) / h;
        const double ij = element(g, site, j) / h;
        kinetic -= m->K[site + j * m->n] *
                   ((invh + u) * ji + (invh + d) * ij) / den;
    }
    if (!isfinite(doublon) || !isfinite(kinetic)) {
        return 1;
    }
    *D = doublon;
    *K = kinetic;
    return 0;
}
