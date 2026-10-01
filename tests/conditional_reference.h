#ifndef CONDITIONAL_REFERENCE_H
#define CONDITIONAL_REFERENCE_H
#include <math.h>
#include <string.h>

/* Independent two-site, four-slice products. No solver Green, PH shortcut,
   rank-one update, UDV or determinant helper is used for the reference. */
static double direct(int config, int cut, double U, double dt, int spin,
                      double *g)
{
    const double lambda = acosh(exp(dt * U / 2.0));
    double a[4] = {1, 0, 0, 1};
    for (int step = 0; step < 4; step++) {
        const int l = (cut + step) % 4;
        double b[4], next[4] = {0};
        for (int j = 0; j < 2; j++) {
            const int s = config & (1 << (2 * l + j)) ? 1 : -1;
            for (int i = 0; i < 2; i++) {
                b[i + 2*j] = (i == j ? cosh(dt) : sinh(dt)) * exp(spin*lambda*s);
            }
        }
        for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) {
            for (int k = 0; k < 2; k++) next[i+2*j] += b[i+2*k]*a[k+2*j];
        }
        memcpy(a, next, sizeof a);
    }
    a[0] += 1; a[3] += 1;
    const double determinant = a[0]*a[3] - a[1]*a[2];
    g[0] = a[3]/determinant; g[1] = -a[1]/determinant;
    g[2] = -a[2]/determinant; g[3] = a[0]/determinant;
    return determinant;
}

#endif
