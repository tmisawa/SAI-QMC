#include "replica_bin_out.h"

#include <math.h>

static double pick(const double *values, int nq, int flat, int index)
{
    if (values == NULL || index < 0 || index >= nq) {
        return NAN;
    }
    return values[(size_t)flat * (size_t)nq + (size_t)index];
}

int replica_bin_write(FILE *fp, const ReplicaBinView *v,
                      const ReplicaBinMeta *m, int write_header)
{
    if (fp == NULL || v == NULL || m == NULL || v->bins == NULL ||
        v->replica_ids == NULL || v->seeds == NULL || v->nbin <= 0 ||
        m->nbin != v->nbin) {
        return 1;
    }
    if (write_header) {
        fprintf(fp,
                "# replica-bin sums; observable mean = sum_sign_O / sum_sign\n"
                "# sum_sign_Ehub is the total energy (not per site); "
                "sum_sign_D is per site\n"
                "# Szz/Sperp: N^-1 sum_ij exp[-iq.(ri-rj)] <..>; Q is the "
                "staggered momentum, 0 is q=0; nan = q not measured\n"
                "# lattice=%s Lx=%d Ly=%d n=%d pbc=%d U=%.17g dtau=%.17g "
                "nwarm=%d nmeas=%d nbin=%d global_update=%s global_interval=%d\n"
                "# szz_Q_index=%d szz_0_index=%d sperp_Q_index=%d\n"
                "# columns: beta_index\tbeta_requested\tbeta_effective\tLtr\t"
                "replica_id\tseed\tbin_id\tsweep_begin\tsweep_end\tcount\t"
                "sum_sign\tsum_sign_Ehub\tsum_sign_D\tlocal_accepted\t"
                "local_attempts\tglobal_accepted\tglobal_attempts\t"
                "sum_sign_Szz_Q\tsum_sign_Sperp_Q\tsum_sign_Szz_0\n",
                m->lattice, m->Lx, m->Ly, m->nsite, m->pbc, m->U, m->dtau,
                m->nwarm, m->nmeas, m->nbin, m->global_update,
                m->global_interval, m->szz_Q_index, m->szz_0_index,
                m->sperp_Q_index);
    }
    const int per = m->nmeas / m->nbin;
    for (int r = 0; r < v->nrep; r++) {
        for (int b = 0; b < v->nbin; b++) {
            const int flat = r * v->nbin + b;
            const ReplicaBin *bin = &v->bins[flat];
            fprintf(fp,
                    "%d\t%.17g\t%.17g\t%d\t%d\t%llu\t%d\t%d\t%d\t%d\t%.17g\t"
                    "%.17g\t%.17g\t%llu\t%llu\t%llu\t%llu\t%.17g\t%.17g\t%.17g\n",
                    m->beta_index, m->beta_requested, m->beta_effective,
                    m->Ltr, v->replica_ids[r], v->seeds[r], b, b * per + 1,
                    (b + 1) * per, bin->count, bin->sum_sign,
                    bin->sum_sign_Ehub, bin->sum_sign_D, bin->accept_accepted,
                    bin->accept_attempts, bin->global_accepted,
                    bin->global_attempts,
                    pick(v->szz, v->szz_nq, flat, m->szz_Q_index),
                    pick(v->sperp, v->sperp_nq, flat, m->sperp_Q_index),
                    pick(v->szz, v->szz_nq, flat, m->szz_0_index));
        }
    }
    if (fflush(fp) != 0 || ferror(fp)) {
        return 1;
    }
    return 0;
}
