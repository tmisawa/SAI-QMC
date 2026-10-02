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

int tempering_ladder_init(TemperingLadder *T, Dqmc **slots, int nslot,
                          unsigned long long seed)
{
    memset(T, 0, sizeof *T);
    T->fail_pair = -1;
    if (slots == NULL || nslot < 2) {
        return 1;
    }
    for (int k = 0; k < nslot; k++) {
        if (slots[k] == NULL || slots[k]->status || !slots[k]->use_ph ||
            slots[k]->n != slots[0]->n || slots[k]->L != slots[0]->L) {
            return 1;
        }
    }
    if (tempering_stats_alloc(&T->stats, nslot) != 0) {
        return 1;
    }
    T->tmp = malloc((size_t)slots[0]->L * (size_t)slots[0]->n);
    if (T->tmp == NULL) {
        tempering_stats_free(&T->stats);
        return 1;
    }
    T->nslot = nslot;
    T->slots = slots;
    T->seed = seed;
    rng_seed(&T->rng, seed);
    tempering_stats_update_ends(&T->stats);
    return 0;
}

void tempering_ladder_free(TemperingLadder *T)
{
    free(T->tmp);
    tempering_stats_free(&T->stats);
    memset(T, 0, sizeof *T);
    T->fail_pair = -1;
}

int tempering_ladder_try_pair(TemperingLadder *T, int k, int *accepted)
{
    *accepted = 0;
    Dqmc *a = T->slots[k], *b = T->slots[k + 1];
    const double u = rng_double(&T->rng);   /* always exactly one draw */
    double waa, wbb, wab, wba;
    int sg;
    if (a->status || b->status ||
        dqmc_log_weight(a, &waa, &sg) != 0 ||
        dqmc_log_weight(b, &wbb, &sg) != 0 ||
        dqmc_log_weight_of(a, b->f->s, &wab, &sg) != 0 ||
        dqmc_log_weight_of(b, a->f->s, &wba, &sg) != 0) {
        T->fail_pair = k;
        return 1;
    }
    const int acc = tempering_accept(tempering_log_ratio(waa, wbb, wab, wba), u);
    if (acc < 0) {
        T->fail_pair = k;
        return 1;
    }
    if (acc) {
        const size_t len = (size_t)a->L * (size_t)a->n;
        memcpy(T->tmp, a->f->s, len);
        if (dqmc_replace_field(a, b->f->s) != 0 ||
            dqmc_replace_field(b, T->tmp) != 0) {
            T->fail_pair = k;
            return 1;
        }
    }
    tempering_stats_record(&T->stats, k, acc);
    *accepted = acc;
    return 0;
}

int tempering_ladder_round(TemperingLadder *T)
{
    for (int k = tempering_first_pair(T->round); k + 1 < T->nslot; k += 2) {
        int acc = 0;
        if (tempering_ladder_try_pair(T, k, &acc) != 0) {
            return 1;
        }
    }
    tempering_stats_update_ends(&T->stats);
    T->round++;
    return 0;
}
