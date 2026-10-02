#include "global_site_diag_out.h"

#include <math.h>

int global_site_diag_write(FILE *fp, const GlobalSiteDiag *diags, int nrep,
                           const int *replica_ids,
                           const unsigned long long *seeds,
                           const GlobalSiteDiagMeta *m, int write_header)
{
    if (fp == NULL || diags == NULL || replica_ids == NULL || seeds == NULL ||
        m == NULL || nrep <= 0) {
        return 1;
    }
    if (write_header) {
        fprintf(fp,
                "# site world-line flip attempts and acceptances by indicator "
                "(measurement sweeps only)\n"
                "# p = |m_i|/L with m_i = sum over time slices of the HS field of site i; "
                "50 bins of width 0.02 on [0,1]\n"
                "# d = -eps_i m_i sign(M)/L with eps_i the sublattice sign and "
                "M = sum_j eps_j m_j; 50 bins of width 0.04 on [-1,1]\n"
                "# lattice=%s Lx=%d Ly=%d n=%d pbc=%d U=%.17g dtau=%.17g "
                "lambda=%.17g tanh_lambda=%.17g nwarm=%d nmeas=%d nbin=%d "
                "global_update=site global_interval=%d global_site_select=%s\n"
                "# columns: beta_index\tbeta_requested\tLtr\treplica_id\tseed\t"
                "indicator\tbin\tbin_lower\tbin_upper\tattempts\taccepted\n",
                m->lattice, m->Lx, m->Ly, m->nsite, m->pbc, m->U, m->dtau,
                m->lambda, tanh(m->lambda), m->nwarm, m->nmeas, m->nbin,
                m->global_interval, m->global_site_select);
    }
    for (int r = 0; r < nrep; r++) {
        for (int ind = 0; ind < 2; ind++) {
            for (int k = 0; k < GLOBAL_SITE_DIAG_NBIN; k++) {
                const double lower = ind == 0 ? (double)k / 50.0 : -1.0 + (double)k / 25.0;
                const double upper = ind == 0 ? (double)(k + 1) / 50.0 : -1.0 + (double)(k + 1) / 25.0;
                fprintf(fp, "%d\t%.17g\t%d\t%d\t%llu\t%c\t%d\t%.17g\t%.17g\t%llu\t%llu\n",
                        m->beta_index, m->beta_requested, m->Ltr, replica_ids[r],
                        seeds[r], ind == 0 ? 'p' : 'd', k, lower, upper,
                        diags[r].attempts[ind][k], diags[r].accepted[ind][k]);
            }
        }
    }
    if (fflush(fp) != 0 || ferror(fp)) {
        return 1;
    }
    return 0;
}
