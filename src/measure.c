#include "measure.h"

#include <math.h>
#include <stddef.h>

MeasSample measure_sample(int n, const double *thop, double U,
                          const double *g_up, const double *g_dn)
{
    MeasSample r;
    double ekin = 0.0;
    double eint = 0.0;
    double doublon = 0.0;
    double ntot = 0.0;

    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            const double t = thop[i + j * n];
            const double dij = (i == j) ? 1.0 : 0.0;
            ekin += t * (dij - g_up[j + i * n]);
            ekin += t * (dij - g_dn[j + i * n]);
        }
    }

    for (int i = 0; i < n; i++) {
        const double nu = 1.0 - g_up[i + i * n];
        const double nd = 1.0 - g_dn[i + i * n];
        const double d = nu * nd;
        eint += d;
        doublon += d;
        ntot += nu + nd;
    }

    r.ekin = ekin;
    r.eint = U * eint;
    r.ntot = ntot;
    r.doublon = doublon / (double)n;
    r.E = r.ekin + r.eint;
    return r;
}

int measure_sample_is_finite(const MeasSample *s)
{
    return s != NULL && isfinite(s->E) && isfinite(s->ekin) &&
           isfinite(s->eint) && isfinite(s->ntot) &&
           isfinite(s->doublon);
}

void jackknife(const double *x, int N, double *mean, double *err)
{
    double sum = 0.0;
    for (int i = 0; i < N; i++) {
        sum += x[i];
    }

    const double mu = (N > 0) ? sum / (double)N : 0.0;
    *mean = mu;
    if (N < 2) {
        *err = 0.0;
        return;
    }

    double var = 0.0;
    for (int i = 0; i < N; i++) {
        const double ji = (sum - x[i]) / (double)(N - 1);
        const double d = ji - mu;
        var += d * d;
    }
    *err = sqrt(var * (double)(N - 1) / (double)N);
}
