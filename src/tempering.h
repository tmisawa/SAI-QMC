#ifndef TEMPERING_H
#define TEMPERING_H

typedef struct {
    int nslot;
    unsigned long long *attempts;  /* [nslot-1], pair (k,k+1) */
    unsigned long long *accepted;  /* [nslot-1] */
    int *walker_at;                /* [nslot], walker id in slot k */
    int *direction;                /* [nslot] by walker: +1 last end = slot 0, -1 last end = slot nslot-1, 0 none */
    int *trip_state;               /* [nslot] by walker: 0 unarmed, 1 hot visited, 2 cold visited after hot */
    unsigned long long *round_trips; /* [nslot] by walker: completed hot -> cold -> hot */
    unsigned long long *n_up;      /* [nslot] by slot: samples whose walker has direction +1 */
    unsigned long long *n_down;    /* [nslot] by slot: samples whose walker has direction -1 */
} TemperingStats;

int tempering_stats_alloc(TemperingStats *st, int nslot);   /* walker_at[k]=k, direction=0, trip_state=0 */
void tempering_stats_free(TemperingStats *st);
/* Zero attempts/accepted/round_trips/n_up/n_down, keep walkers and direction
   labels, and re-arm trips: trip_state = 1 for the walker at slot 0, else 0.
   Only trips that lie entirely in the new interval are counted afterwards. */
void tempering_stats_begin_measurement(TemperingStats *st);
double tempering_log_ratio(double logw_a_ca, double logw_b_cb,
                           double logw_a_cb, double logw_b_ca);
int tempering_accept(double log_ratio, double u);           /* 1 accept, 0 reject, -1 non-finite */
int tempering_first_pair(unsigned long long round);         /* 0 for even rounds, 1 for odd */
void tempering_stats_record(TemperingStats *st, int k, int accepted); /* swaps walker_at[k],[k+1] on accept */
void tempering_stats_update_ends(TemperingStats *st);       /* after a round: end visits, round trips */
void tempering_stats_sample(TemperingStats *st);            /* add one n_up/n_down sample per slot */

#endif
