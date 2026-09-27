#include "tempering.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int tempering_stats_alloc(TemperingStats *st, int nslot)
{
    memset(st, 0, sizeof *st);
    if (nslot < 2) {
        return 1;
    }
    st->nslot = nslot;
    st->attempts = calloc((size_t)nslot - 1, sizeof *st->attempts);
    st->accepted = calloc((size_t)nslot - 1, sizeof *st->accepted);
    st->walker_at = calloc((size_t)nslot, sizeof *st->walker_at);
    st->direction = calloc((size_t)nslot, sizeof *st->direction);
    st->trip_state = calloc((size_t)nslot, sizeof *st->trip_state);
    st->round_trips = calloc((size_t)nslot, sizeof *st->round_trips);
    st->n_up = calloc((size_t)nslot, sizeof *st->n_up);
    st->n_down = calloc((size_t)nslot, sizeof *st->n_down);
    if (!st->attempts || !st->accepted || !st->walker_at || !st->direction ||
        !st->trip_state || !st->round_trips || !st->n_up || !st->n_down) {
        tempering_stats_free(st);
        return 1;
    }
    for (int k = 0; k < nslot; k++) {
        st->walker_at[k] = k;
    }
    return 0;
}

void tempering_stats_free(TemperingStats *st)
{
    free(st->attempts);
    free(st->accepted);
    free(st->walker_at);
    free(st->direction);
    free(st->trip_state);
    free(st->round_trips);
    free(st->n_up);
    free(st->n_down);
    memset(st, 0, sizeof *st);
}

void tempering_stats_begin_measurement(TemperingStats *st)
{
    const size_t np = (size_t)st->nslot - 1, ns = (size_t)st->nslot;
    memset(st->attempts, 0, np * sizeof *st->attempts);
    memset(st->accepted, 0, np * sizeof *st->accepted);
    memset(st->round_trips, 0, ns * sizeof *st->round_trips);
    memset(st->n_up, 0, ns * sizeof *st->n_up);
    memset(st->n_down, 0, ns * sizeof *st->n_down);
    for (int w = 0; w < st->nslot; w++) {
        st->trip_state[w] = (w == st->walker_at[0]) ? 1 : 0;
    }
}

double tempering_log_ratio(double logw_a_ca, double logw_b_cb,
                           double logw_a_cb, double logw_b_ca)
{
    return logw_a_cb + logw_b_ca - logw_a_ca - logw_b_cb;
}

int tempering_accept(double log_ratio, double u)
{
    if (!isfinite(log_ratio)) {
        return -1;
    }
    return (log_ratio >= 0.0 || u < exp(log_ratio)) ? 1 : 0;
}

int tempering_first_pair(unsigned long long round)
{
    return (int)(round & 1ULL);
}

void tempering_stats_record(TemperingStats *st, int k, int accepted)
{
    st->attempts[k]++;
    if (accepted) {
        st->accepted[k]++;
        const int w = st->walker_at[k];
        st->walker_at[k] = st->walker_at[k + 1];
        st->walker_at[k + 1] = w;
    }
}

void tempering_stats_update_ends(TemperingStats *st)
{
    const int hot = st->walker_at[0];
    const int cold = st->walker_at[st->nslot - 1];
    if (st->trip_state[hot] == 2) {
        st->round_trips[hot]++;          /* hot -> cold -> hot completed */
    }
    st->trip_state[hot] = 1;
    st->direction[hot] = 1;
    if (st->trip_state[cold] == 1) {
        st->trip_state[cold] = 2;
    }
    st->direction[cold] = -1;
}

void tempering_stats_sample(TemperingStats *st)
{
    for (int k = 0; k < st->nslot; k++) {
        const int d = st->direction[st->walker_at[k]];
        if (d > 0) {
            st->n_up[k]++;
        } else if (d < 0) {
            st->n_down[k]++;
        }
    }
}
