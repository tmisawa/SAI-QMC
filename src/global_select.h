#ifndef GLOBAL_SELECT_H
#define GLOBAL_SELECT_H

#define GLOBAL_SITE_DIAG_NBIN 50

/* Per-site indicators of the HS world-line configuration (spec 1.3):
   p[i] = |m[i]|/L, d[i] = -bipart[i]*m[i]*sign(M)/L, M = sum_j bipart[j]*m[j],
   sign(0) = +1. bipart == NULL gives d[i] = 0. */
int global_site_indicators(const int *m, const int *bipart, int n, int L,
                           double *p, double *d);

/* Stage B (spec 3.1). p_0 = tanh(lambda) when lambda > 0, else 1.0 (U = 0). */
double global_site_weight_scale(double lambda);
/* w[i] = pow(p[i]/p0, alpha) + 1/n, cum[i] = sum_{j<=i} w[j]. Returns 1 on bad
   arguments (NULL, n <= 0, p0 <= 0, alpha < 0 or non-finite), non-finite output,
   a cumulative increment that is not strictly positive, or a relative weight
   w[i]/cum[n-1] below DBL_EPSILON, or a site interval with no reachable 53-bit
   RNG grid point after target multiplication. Callers treat 1 as a numerical failure. */
int global_site_weights_p(const double *p, int n, double p0, double alpha,
                          double *w, double *cum);
/* Smallest i with cum[i] >= u * cum[n-1] (u in [0,1)); n-1 if none. Returns -1
   on NULL cum, n <= 0, or cum[n-1] <= 0 or non-finite. */
int global_site_select_index(const double *cum, int n, double u);

/* Histogram of attempts/acceptances of site world-line flips (spec 3.3).
   Row 0: p in [0,1], 50 bins of width 0.02. Row 1: d in [-1,1], 50 bins of 0.04. */
typedef struct {
    unsigned long long attempts[2][GLOBAL_SITE_DIAG_NBIN];
    unsigned long long accepted[2][GLOBAL_SITE_DIAG_NBIN];
} GlobalSiteDiag;

int global_site_diag_bin_p(double p);
int global_site_diag_bin_d(double d);
void global_site_diag_clear(GlobalSiteDiag *h);
void global_site_diag_add(GlobalSiteDiag *h, double p, double d, int accepted);
void global_site_diag_merge(GlobalSiteDiag *dst, const GlobalSiteDiag *src);

#endif
