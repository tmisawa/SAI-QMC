#include "global_select.h"

#include <math.h>
#include <string.h>

int global_site_indicators(const int *m, const int *bipart, int n, int L,
                           double *p, double *d)
{
    if (m == NULL || p == NULL || d == NULL || n <= 0 || L <= 0) {
        return 1;
    }
    long long M = 0;
    if (bipart != NULL) {
        for (int i = 0; i < n; i++) {
            M += (long long)bipart[i] * (long long)m[i];
        }
    }
    const double sgn = (M >= 0) ? 1.0 : -1.0;
    for (int i = 0; i < n; i++) {
        p[i] = fabs((double)m[i]) / (double)L;
        d[i] = (bipart != NULL)
                   ? -(double)bipart[i] * (double)m[i] * sgn / (double)L
                   : 0.0;
    }
    return 0;
}

static int clamp_bin(double x)
{
    if (!(x >= 0.0)) {           /* also catches NaN */
        return 0;
    }
    if (x >= (double)GLOBAL_SITE_DIAG_NBIN) {
        return GLOBAL_SITE_DIAG_NBIN - 1;
    }
    return (int)x;
}

int global_site_diag_bin_p(double p)
{
    return clamp_bin(floor(50.0 * p));
}

int global_site_diag_bin_d(double d)
{
    return clamp_bin(floor(25.0 * (d + 1.0)));
}

void global_site_diag_clear(GlobalSiteDiag *h)
{
    if (h != NULL) {
        memset(h, 0, sizeof(*h));
    }
}

void global_site_diag_add(GlobalSiteDiag *h, double p, double d, int accepted)
{
    if (h == NULL) {
        return;
    }
    const int kp = global_site_diag_bin_p(p);
    const int kd = global_site_diag_bin_d(d);
    h->attempts[0][kp]++;
    h->attempts[1][kd]++;
    if (accepted) {
        h->accepted[0][kp]++;
        h->accepted[1][kd]++;
    }
}

void global_site_diag_merge(GlobalSiteDiag *dst, const GlobalSiteDiag *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    for (int r = 0; r < 2; r++) {
        for (int k = 0; k < GLOBAL_SITE_DIAG_NBIN; k++) {
            dst->attempts[r][k] += src->attempts[r][k];
            dst->accepted[r][k] += src->accepted[r][k];
        }
    }
}
