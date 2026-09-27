#define _POSIX_C_SOURCE 199309L
#include "tempering_out.h"

#include <math.h>
#include <time.h>

double tempering_monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

int tempering_out_write(FILE *fp, const Params *p, const TemperingLadderOut *outs,
                        int nladder)
{
    if (fp == NULL || p == NULL || (outs == NULL && nladder > 0) || nladder < 0) {
        return 1;
    }
    const int K = p->nbeta;
    const int Ltr = p->tempering_ltr;
    if (K < 2 || K > TEMPERING_MAX_SLOT || Ltr < 1 || p->nbin < 1) {
        return 1;
    }
    for (int l = 0; l < nladder; l++) {
        if (outs[l].nslot != K || outs[l].nbin != p->nbin) {
            return 1;
        }
    }
    fprintf(fp,
            "# tempering=dtau_ladder nslot=%d Ltr=%d interval=%d nladder=%d "
            "nbin=%d nwarm=%d nmeas=%d seed=%llu\n",
            K, Ltr, p->tempering_interval, nladder, p->nbin, p->nwarm,
            p->nmeas, p->seed);
    for (int k = 0; k < K; k++) {
        const double dtau = p->beta_list[k] / (double)Ltr;
        fprintf(fp, "# slot=%d beta=%.17g dtau=%.17g lambda=%.17g\n", k,
                p->beta_list[k], dtau, acosh(exp(0.5 * dtau * p->U)));
    }
    fputs("# columns: kind\tladder\tbin\tindex\tv1\tv2\tv3\n"
          "# pair:   v1=attempts v2=accepted v3=0 (bin=-1: warmup total; "
          "index k = pair (k,k+1))\n"
          "# slot:   v1=walker_at_bin_end v2=n_up v3=n_down\n"
          "# walker: bin=-1 v1=round_trips(completed within measurement) "
          "v2=final_slot v3=0\n"
          "# ladder: bin=-1 index=-1 v1=swap_seed v2=failed v3=0\n"
          "# cost:   bin=-1 index=0..3 (warmup, meas_sweep, meas_exchange, "
          "meas_measure) v1=worker_seconds v2=0 v3=0\n"
          "#         worker seconds of one ladder; not job wall, not node-hours\n",
          fp);
    for (int l = 0; l < nladder; l++) {
        const TemperingLadderOut *o = &outs[l];
        const int id = o->ladder_id;
        for (int k = 0; k < K - 1; k++) {
            fprintf(fp, "pair\t%d\t-1\t%d\t%llu\t%llu\t0\n", id, k,
                    o->warm_attempts[k], o->warm_accepted[k]);
        }
        for (int b = 0; b < o->nbin; b++) {
            for (int k = 0; k < K - 1; k++) {
                const size_t i = (size_t)b * (size_t)(K - 1) + (size_t)k;
                fprintf(fp, "pair\t%d\t%d\t%d\t%llu\t%llu\t0\n", id, b, k,
                        o->bin_attempts[i], o->bin_accepted[i]);
            }
        }
        for (int b = 0; b < o->nbin; b++) {
            for (int k = 0; k < K; k++) {
                const size_t i = (size_t)b * (size_t)K + (size_t)k;
                fprintf(fp, "slot\t%d\t%d\t%d\t%d\t%llu\t%llu\n", id, b, k,
                        o->bin_walker_at[i], o->bin_n_up[i], o->bin_n_down[i]);
            }
        }
        /* final_walker_at maps slot -> walker; invert to walker -> slot */
        int final_slot[TEMPERING_MAX_SLOT];
        for (int w = 0; w < K; w++) {
            final_slot[w] = -1;
        }
        for (int k = 0; k < K; k++) {
            const int w = o->final_walker_at[k];
            if (w >= 0 && w < K) {
                final_slot[w] = k;
            }
        }
        for (int w = 0; w < K; w++) {
            fprintf(fp, "walker\t%d\t-1\t%d\t%llu\t%d\t0\n", id, w,
                    o->round_trips[w], final_slot[w]);
        }
        fprintf(fp, "ladder\t%d\t-1\t-1\t%llu\t%d\t0\n", id, o->swap_seed,
                o->failed != 0);
        const double sec[4] = {o->sec_warmup, o->sec_meas_sweep,
                               o->sec_meas_exchange, o->sec_meas_measure};
        for (int i = 0; i < 4; i++) {
            fprintf(fp, "cost\t%d\t-1\t%d\t%.6f\t0\t0\n", id, i, sec[i]);
        }
    }
    if (fflush(fp) != 0 || ferror(fp)) {
        return 1;
    }
    return 0;
}
