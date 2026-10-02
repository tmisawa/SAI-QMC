#include "global_select.h"

#include <float.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

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

double global_site_weight_scale(double lambda)
{
    const double p0 = tanh(lambda);
    return (p0 > 0.0) ? p0 : 1.0;
}

int global_site_weights_p(const double *p, int n, double p0, double alpha,
                          double *w, double *cum)
{
    if (p == NULL || w == NULL || cum == NULL || n <= 0 || !(p0 > 0.0) ||
        !isfinite(alpha) || alpha < 0.0) {
        return 1;
    }
    const double floor_w = 1.0 / (double)n;
    double running = 0.0;
    for (int i = 0; i < n; i++) {
        w[i] = pow(p[i] / p0, alpha) + floor_w;
        const double previous = running;
        running += w[i];
        cum[i] = running;
        if (!isfinite(w[i]) || !isfinite(cum[i]) || !(cum[i] > previous)) {
            return 1;   /* non-finite, or the increment vanished in the rounding */
        }
    }
    for (int i = 0; i < n; i++) {
        if (w[i] / cum[n - 1] < DBL_EPSILON) {
            return 1;   /* conservative relative-weight limit; not a reachability proof */
        }
    }
    /* rng_double returns k * 2^-53, 0 <= k < 2^53. For site i > 0, find the
       first k whose rounded target exceeds cum[i-1], then test the upper edge.
       This uses exactly the same double multiplication as the selector. It
       consumes no RNG and validates all intervals before any proposal. Site 0
       always has the witness k = 0. grid_size is an exclusive sentinel. */
    const uint64_t grid_size = UINT64_C(1) << 53;
    for (int i = 1; i < n; i++) {
        uint64_t lo = 0, hi = grid_size;
        while (lo < hi) {
            const uint64_t mid = lo + (hi - lo) / 2;
            const double u = (double)mid * 0x1p-53;
            const double target = u * cum[n - 1];
            if (target <= cum[i - 1]) lo = mid + 1;
            else hi = mid;
        }
        if (lo == grid_size) return 1;
        const double u = (double)lo * 0x1p-53;
        const double target = u * cum[n - 1];
        if (!(target > cum[i - 1] && target <= cum[i])) return 1;
    }
    return 0;
}

int global_site_select_index(const double *cum, int n, double u)
{
    if (cum == NULL || n <= 0 || !isfinite(cum[n - 1]) || !(cum[n - 1] > 0.0)) {
        return -1;
    }
    const double target = u * cum[n - 1];
    for (int i = 0; i < n; i++) {
        if (cum[i] >= target) {
            return i;
        }
    }
    return n - 1;
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
