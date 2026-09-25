#ifndef GLOBAL_SELECT_H
#define GLOBAL_SELECT_H

#define GLOBAL_SITE_DIAG_NBIN 50

/* Per-site indicators of the HS world-line configuration (spec 1.3):
   p[i] = |m[i]|/L, d[i] = -bipart[i]*m[i]*sign(M)/L, M = sum_j bipart[j]*m[j],
   sign(0) = +1. bipart == NULL gives d[i] = 0. */
int global_site_indicators(const int *m, const int *bipart, int n, int L,
                           double *p, double *d);

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
