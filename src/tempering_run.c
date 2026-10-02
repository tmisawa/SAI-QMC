#define _POSIX_C_SOURCE 199309L
#include "tempering_run.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dqmc.h"
#include "profiler.h"
#include "replica_chain.h"
#include "tempering.h"

int tempering_ladder_out_alloc(TemperingLadderOut *o, int nslot, int nbin)
{
    if (o == NULL) {
        return 1;
    }
    memset(o, 0, sizeof *o);
    if (nslot < 2 || nslot > TEMPERING_MAX_SLOT || nbin < 1) {
        return 1;
    }
    const size_t np = (size_t)nbin * (size_t)(nslot - 1);
    const size_t ns = (size_t)nbin * (size_t)nslot;
    o->bin_attempts = calloc(np, sizeof *o->bin_attempts);
    o->bin_accepted = calloc(np, sizeof *o->bin_accepted);
    o->bin_walker_at = calloc(ns, sizeof *o->bin_walker_at);
    o->bin_n_up = calloc(ns, sizeof *o->bin_n_up);
    o->bin_n_down = calloc(ns, sizeof *o->bin_n_down);
    if (o->bin_attempts == NULL || o->bin_accepted == NULL ||
        o->bin_walker_at == NULL || o->bin_n_up == NULL ||
        o->bin_n_down == NULL) {
        tempering_ladder_out_free(o);
        return 1;
    }
    o->nslot = nslot;
    o->nbin = nbin;
    for (int k = 0; k < nslot; k++) {
        o->final_walker_at[k] = k;
    }
    return 0;
}

void tempering_ladder_out_free(TemperingLadderOut *o)
{
    if (o == NULL) {
        return;
    }
    free(o->bin_attempts);
    free(o->bin_accepted);
    free(o->bin_walker_at);
    free(o->bin_n_up);
    free(o->bin_n_down);
    memset(o, 0, sizeof *o);
}

int tempering_ladder_out_width_u64(int nslot, int nbin)
{
    if (nslot < 2 || nslot > TEMPERING_MAX_SLOT || nbin < 1) {
        return -1;
    }
    /* nslot, nbin, ladder_id, swap_seed, warm_attempts, warm_accepted,
       bin_attempts, bin_accepted, bin_walker_at, bin_n_up, bin_n_down,
       round_trips, final_walker_at, failed */
    return 4 + 2 * (nslot - 1) + 2 * nbin * (nslot - 1) + 3 * nbin * nslot +
           2 * nslot + 1;
}

int tempering_ladder_out_width_f64(void)
{
    return 4;
}

/* A negative int cannot be a valid count or id; it is sent as UINT64_MAX so
   that unpack rejects it instead of wrapping it into a plausible value. */
static uint64_t int_to_u64(int v)
{
    return (v >= 0) ? (uint64_t)v : UINT64_MAX;
}

void tempering_ladder_out_pack(const TemperingLadderOut *o, uint64_t *ibuf, double *fbuf)
{
    const int K = o->nslot, nb = o->nbin;
    size_t w = 0;
    ibuf[w++] = int_to_u64(K);
    ibuf[w++] = int_to_u64(nb);
    ibuf[w++] = int_to_u64(o->ladder_id);
    ibuf[w++] = (uint64_t)o->swap_seed;
    for (int k = 0; k < K - 1; k++) {
        ibuf[w++] = (uint64_t)o->warm_attempts[k];
    }
    for (int k = 0; k < K - 1; k++) {
        ibuf[w++] = (uint64_t)o->warm_accepted[k];
    }
    for (int i = 0; i < nb * (K - 1); i++) {
        ibuf[w++] = (uint64_t)o->bin_attempts[i];
    }
    for (int i = 0; i < nb * (K - 1); i++) {
        ibuf[w++] = (uint64_t)o->bin_accepted[i];
    }
    for (int i = 0; i < nb * K; i++) {
        ibuf[w++] = int_to_u64(o->bin_walker_at[i]);
    }
    for (int i = 0; i < nb * K; i++) {
        ibuf[w++] = (uint64_t)o->bin_n_up[i];
    }
    for (int i = 0; i < nb * K; i++) {
        ibuf[w++] = (uint64_t)o->bin_n_down[i];
    }
    for (int k = 0; k < K; k++) {
        ibuf[w++] = (uint64_t)o->round_trips[k];
    }
    for (int k = 0; k < K; k++) {
        ibuf[w++] = int_to_u64(o->final_walker_at[k]);
    }
    ibuf[w++] = int_to_u64(o->failed);
    fbuf[0] = o->sec_warmup;
    fbuf[1] = o->sec_meas_sweep;
    fbuf[2] = o->sec_meas_exchange;
    fbuf[3] = o->sec_meas_measure;
}

int tempering_ladder_out_unpack(const uint64_t *ibuf, const double *fbuf,
                                int nslot, int nbin, TemperingLadderOut *o)
{
    if (ibuf == NULL || fbuf == NULL || o == NULL ||
        nslot < 2 || nslot > TEMPERING_MAX_SLOT || nbin < 1 ||
        o->nslot != nslot || o->nbin != nbin ||
        ibuf[0] != (uint64_t)nslot || ibuf[1] != (uint64_t)nbin ||
        ibuf[2] > (uint64_t)INT_MAX) {
        return 1;
    }
    const int K = nslot, nb = nbin;
    /* validate before writing so a rejected buffer leaves o unchanged */
    {
        size_t w = 4 + 2 * (size_t)(K - 1) + 2 * (size_t)nb * (size_t)(K - 1);
        for (int i = 0; i < nb * K; i++) {
            if (ibuf[w++] >= (uint64_t)K) {
                return 1;
            }
        }
        w += 2 * (size_t)nb * (size_t)K + (size_t)K;
        for (int k = 0; k < K; k++) {
            if (ibuf[w++] >= (uint64_t)K) {
                return 1;
            }
        }
        if (ibuf[w] > 1ULL) {
            return 1;
        }
    }
    size_t w = 2;
    o->ladder_id = (int)ibuf[w++];
    o->swap_seed = (unsigned long long)ibuf[w++];
    for (int k = 0; k < K - 1; k++) {
        o->warm_attempts[k] = (unsigned long long)ibuf[w++];
    }
    for (int k = 0; k < K - 1; k++) {
        o->warm_accepted[k] = (unsigned long long)ibuf[w++];
    }
    for (int i = 0; i < nb * (K - 1); i++) {
        o->bin_attempts[i] = (unsigned long long)ibuf[w++];
    }
    for (int i = 0; i < nb * (K - 1); i++) {
        o->bin_accepted[i] = (unsigned long long)ibuf[w++];
    }
    for (int i = 0; i < nb * K; i++) {
        o->bin_walker_at[i] = (int)ibuf[w++];
    }
    for (int i = 0; i < nb * K; i++) {
        o->bin_n_up[i] = (unsigned long long)ibuf[w++];
    }
    for (int i = 0; i < nb * K; i++) {
        o->bin_n_down[i] = (unsigned long long)ibuf[w++];
    }
    for (int k = 0; k < K; k++) {
        o->round_trips[k] = (unsigned long long)ibuf[w++];
    }
    for (int k = 0; k < K; k++) {
        o->final_walker_at[k] = (int)ibuf[w++];
    }
    o->failed = (int)ibuf[w++];
    o->sec_warmup = fbuf[0];
    o->sec_meas_sweep = fbuf[1];
    o->sec_meas_exchange = fbuf[2];
    o->sec_meas_measure = fbuf[3];
    return 0;
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

/* One slot's numerical breakdown, in the form of the warmup and measurement
   lines of dqmc_run_replica with the slot and ladder added. bin < 0 selects
   the warmup form. */
static void report_slot_breakdown(const ReplicaChain *c, int k, int ladder_id,
                                  int bin, int meas)
{
    const Params *p = c->p;
    const char *reason =
        linalg_failure_reason_string(replica_chain_failure_reason(c));
    if (bin < 0) {
        fprintf(stderr,
                "ERROR: dqmc warmup numerical breakdown "
                "(slot=%d ladder=%d beta_index=%d beta=%.17g T=%.17g "
                "replica=%d seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                "sweep_count=%llu status=%d failure_reason=%s)\n",
                k, ladder_id, k, c->beta, c->T, ladder_id, c->seed, p->U,
                c->dtau, c->Ltr, p->stab_interval, c->D.sweep_count,
                c->D.status, reason);
    } else {
        fprintf(stderr,
                "ERROR: dqmc measurement numerical breakdown "
                "(slot=%d ladder=%d beta_index=%d beta=%.17g T=%.17g "
                "replica=%d seed=%llu U=%g dtau=%g Ltr=%d stab=%d "
                "sweep_count=%llu bin=%d meas=%d status=%d "
                "failure_reason=%s)\n",
                k, ladder_id, k, c->beta, c->T, ladder_id, c->seed, p->U,
                c->dtau, c->Ltr, p->stab_interval, c->D.sweep_count, bin,
                meas, c->D.status, reason);
    }
}

static void report_exchange_failure(const TemperingLadder *T,
                                    const ReplicaChain *c, int ladder_id,
                                    unsigned long long sweep)
{
    const int k = T->fail_pair;
    const int sa = (k >= 0 && k < T->nslot) ? c[k].D.status : -1;
    const int sb = (k >= 0 && k + 1 < T->nslot) ? c[k + 1].D.status : -1;
    fprintf(stderr,
            "ERROR: tempering exchange failed (ladder=%d pair=%d round=%llu "
            "sweep=%llu slot_status=%d,%d)\n",
            ladder_id, k, T->round, sweep, sa, sb);
}

/* The exchange round after ladder sweep `sweep` (bin < 0: warmup). A slot
   whose own sweep or global pass has already failed is reported as that
   slot's breakdown and the round is not attempted, whether or not the round
   would include that slot. Only a failure of the round itself with every
   slot intact (a weight evaluation, a non-finite log R, or the rebuild after
   an accepted exchange) is reported as an exchange failure. */
static int exchange_round(TemperingLadder *T, const ReplicaChain *c,
                          int ladder_id, unsigned long long sweep, int bin,
                          int meas)
{
    int slot_failed = 0;
    for (int k = 0; k < T->nslot; k++) {
        if (c[k].D.status != 0) {
            report_slot_breakdown(&c[k], k, ladder_id, bin, meas);
            slot_failed = 1;
        }
    }
    if (slot_failed) {
        return 1;
    }
    if (tempering_ladder_round(T) != 0) {
        report_exchange_failure(T, c, ladder_id, sweep);
        return 1;
    }
    return 0;
}

#ifdef AFQMC_TEST_HOOKS
/* Failure injection for tests (hook builds only), after the slot sweeps of
   ladder sweep s (warmup included) and before that sweep's exchange round:
   AFQMC_TEST_TEMPERING_SLOT_FAIL_AT=s with AFQMC_TEST_TEMPERING_SLOT_FAIL_SLOT=k
   marks slot k as failed, as its own failed sweep or global pass would;
   AFQMC_TEST_TEMPERING_EXCHANGE_FAIL_AT=s makes the round's weight
   evaluations fail while every slot stays intact. */
typedef struct {
    unsigned long long slot_fail_at, exchange_fail_at;
    int slot_fail_slot;
} TemperingTestHooks;

static void tempering_test_hooks_read(TemperingTestHooks *h)
{
    const char *at = getenv("AFQMC_TEST_TEMPERING_SLOT_FAIL_AT");
    const char *slot = getenv("AFQMC_TEST_TEMPERING_SLOT_FAIL_SLOT");
    const char *xat = getenv("AFQMC_TEST_TEMPERING_EXCHANGE_FAIL_AT");
    h->slot_fail_at = (at != NULL) ? strtoull(at, NULL, 10) : 0ULL;
    h->slot_fail_slot = (slot != NULL) ? atoi(slot) : 0;
    h->exchange_fail_at = (xat != NULL) ? strtoull(xat, NULL, 10) : 0ULL;
}

static void tempering_test_hooks_apply(const TemperingTestHooks *h,
                                       ReplicaChain *c, int nslot,
                                       int ladder_id, unsigned long long s)
{
    if (h->slot_fail_at != 0ULL && s == h->slot_fail_at &&
        h->slot_fail_slot >= 0 && h->slot_fail_slot < nslot) {
        c[h->slot_fail_slot].D.status = 1;
        fprintf(stderr, "TEST_TEMPERING_SLOT_FAIL ladder=%d slot=%d sweep=%llu\n",
                ladder_id, h->slot_fail_slot, s);
    }
    if (h->exchange_fail_at != 0ULL && s == h->exchange_fail_at) {
        for (int k = 0; k < nslot; k++) {
            c[k].D.Gu.work.failed = 1;   /* sticky: log-det evaluations fail */
        }
        fprintf(stderr, "TEST_TEMPERING_EXCHANGE_FAIL ladder=%d sweep=%llu\n",
                ladder_id, s);
    }
}
#endif

int dqmc_run_ladder(const Params *p, const Lattice *L, double mu, int ladder_id,
                    const StructureFactorPlan *szz_plan,
                    const StructureFactorPlan *sperp_plan,
                    ReplicaResult *results, TemperingLadderOut *out)
{
    if (p == NULL || L == NULL || szz_plan == NULL || sperp_plan == NULL ||
        results == NULL || out == NULL) {
        return 1;
    }
    const int nslot = p->nbeta;
    const int Ltr = p->tempering_ltr;
    if (nslot < 2 || nslot > TEMPERING_MAX_SLOT || Ltr < 1 ||
        p->nbin < 1 || p->tempering_interval < 1 ||
        out->nslot != nslot || out->nbin != p->nbin) {
        out->failed = 1;
        return 1;
    }
    out->ladder_id = ladder_id;
    out->swap_seed = replica_seed(p->seed, p->nbeta, ladder_id);
    out->failed = 0;

    ReplicaChain *c = calloc((size_t)nslot, sizeof *c);  /* never moved */
    if (c == NULL) {
        out->failed = 1;
        return 1;
    }
    Profiler *old_prof = profiler_current();
    profiler_set_current(NULL);
    int ninit = 0, nbegun = 0, have_ladder = 0, rc = 1;
    TemperingLadder T;
    memset(&T, 0, sizeof T);
    Dqmc *ptr[TEMPERING_MAX_SLOT];

    for (int k = 0; k < nslot; k++) {
        const double dtau_k = p->beta_list[k] / (double)Ltr;
        if (replica_chain_init(&c[k], p, L, k, Ltr, dtau_k, ladder_id,
                               replica_seed(p->seed, k, ladder_id), NULL) != 0) {
            goto done;
        }
        ninit++;
        ptr[k] = &c[k].D;
    }
    for (int k = 0; k < nslot; k++) {
        if (!c[k].D.use_ph) {
            fprintf(stderr,
                    "ERROR: tempering=dtau_ladder requires a sign-free "
                    "PH-symmetric model (half filling, bipartite lattice)\n");
            goto done;
        }
    }
    if (tempering_ladder_init(&T, ptr, nslot, out->swap_seed) != 0) {
        fprintf(stderr, "ERROR: tempering ladder init failed (ladder=%d)\n",
                ladder_id);
        goto done;
    }
    have_ladder = 1;
#ifdef AFQMC_TEST_HOOKS
    {
        const char *fail_ladder = getenv("AFQMC_TEST_TEMPERING_FAIL_LADDER");
        if (fail_ladder != NULL && fail_ladder[0] != '\0' &&
            atoi(fail_ladder) == ladder_id) {
            fprintf(stderr, "TEST_TEMPERING_FAIL ladder=%d\n", ladder_id);
            goto done;
        }
    }
    TemperingTestHooks hooks;
    tempering_test_hooks_read(&hooks);
#endif

    const unsigned long long interval = (unsigned long long)p->tempering_interval;
    double t0 = now_seconds();
    for (int w = 0; w < p->nwarm; w++) {
        for (int k = 0; k < nslot; k++) {
            (void)replica_chain_step(&c[k]);
        }
        const unsigned long long s = (unsigned long long)w + 1ULL;
#ifdef AFQMC_TEST_HOOKS
        tempering_test_hooks_apply(&hooks, c, nslot, ladder_id, s);
#endif
        if (s % interval == 0ULL &&
            exchange_round(&T, c, ladder_id, s, -1, 0) != 0) {
            out->sec_warmup += now_seconds() - t0;
            goto done;
        }
    }
    out->sec_warmup += now_seconds() - t0;

    for (int k = 0; k < nslot; k++) {
        if (c[k].D.status != 0 || !replica_chain_state_is_finite(&c[k])) {
            report_slot_breakdown(&c[k], k, ladder_id, -1, 0);
            goto done;
        }
    }
    for (int k = 0; k < nslot - 1; k++) {
        out->warm_attempts[k] = T.stats.attempts[k];
        out->warm_accepted[k] = T.stats.accepted[k];
    }
    tempering_stats_begin_measurement(&T.stats);

    for (int k = 0; k < nslot; k++) {
        if (replica_chain_measure_begin(&c[k], szz_plan, sperp_plan,
                                        &results[k]) != 0) {
            goto done;
        }
        nbegun++;
    }

    const int per = p->nmeas / p->nbin;
    unsigned long long att0[TEMPERING_MAX_SLOT], acc0[TEMPERING_MAX_SLOT];
    unsigned long long gatt0[TEMPERING_MAX_SLOT], gacc0[TEMPERING_MAX_SLOT];
    unsigned long long bin_att0[TEMPERING_MAX_SLOT], bin_acc0[TEMPERING_MAX_SLOT];
    unsigned long long bin_up0[TEMPERING_MAX_SLOT], bin_dn0[TEMPERING_MAX_SLOT];
    for (int bi = 0; bi < p->nbin; bi++) {
        for (int k = 0; k < nslot; k++) {
            if (k < nslot - 1) {
                bin_att0[k] = T.stats.attempts[k];
                bin_acc0[k] = T.stats.accepted[k];
            }
            bin_up0[k] = T.stats.n_up[k];
            bin_dn0[k] = T.stats.n_down[k];
        }
        for (int kk = 0; kk < per; kk++) {
            t0 = now_seconds();
            for (int k = 0; k < nslot; k++) {
                att0[k] = c[k].D.accept_attempts;
                acc0[k] = c[k].D.accept_accepted;
                dqmc_sweep(&c[k].D);
                gatt0[k] = c[k].D.global_attempts;
                gacc0[k] = c[k].D.global_accepted;
                (void)replica_chain_global_after_sweep(&c[k]);
            }
            const double t1 = now_seconds();
            out->sec_meas_sweep += t1 - t0;

            const unsigned long long s = (unsigned long long)p->nwarm +
                                         (unsigned long long)bi * (unsigned long long)per +
                                         (unsigned long long)kk + 1ULL;
#ifdef AFQMC_TEST_HOOKS
            tempering_test_hooks_apply(&hooks, c, nslot, ladder_id, s);
#endif
            if (s % interval == 0ULL) {
                if (exchange_round(&T, c, ladder_id, s, bi, kk) != 0) {
                    out->sec_meas_exchange += now_seconds() - t1;
                    goto done;
                }
                tempering_stats_sample(&T.stats);
            }
            const double t2 = now_seconds();
            out->sec_meas_exchange += t2 - t1;

            for (int k = 0; k < nslot; k++) {
                if (replica_chain_measure(&c[k], &results[k], bi, kk, mu,
                                          att0[k], acc0[k], gatt0[k],
                                          gacc0[k]) != 0) {
                    out->sec_meas_measure += now_seconds() - t2;
                    goto done;
                }
            }
            out->sec_meas_measure += now_seconds() - t2;
        }
        for (int k = 0; k < nslot; k++) {
            const size_t is = (size_t)bi * (size_t)nslot + (size_t)k;
            if (k < nslot - 1) {
                const size_t ip = (size_t)bi * (size_t)(nslot - 1) + (size_t)k;
                out->bin_attempts[ip] = T.stats.attempts[k] - bin_att0[k];
                out->bin_accepted[ip] = T.stats.accepted[k] - bin_acc0[k];
            }
            out->bin_walker_at[is] = T.stats.walker_at[k];
            out->bin_n_up[is] = T.stats.n_up[k] - bin_up0[k];
            out->bin_n_down[is] = T.stats.n_down[k] - bin_dn0[k];
        }
    }

    for (int k = 0; k < nslot; k++) {
        if (c[k].use_global && p->global_site_diag_file[0] != '\0') {
            *results[k].site_diag = *c[k].D.site_diag;
        }
        results[k].status = 0;
        out->round_trips[k] = T.stats.round_trips[k];
        out->final_walker_at[k] = T.stats.walker_at[k];
    }
    rc = 0;

done:
    if (rc != 0) {
        for (int k = 0; k < nbegun; k++) {
            replica_result_free(&results[k]);
        }
        out->failed = 1;
    }
    if (have_ladder) {
        tempering_ladder_free(&T);
    }
    for (int k = 0; k < ninit; k++) {
        replica_chain_free(&c[k]);
    }
    free(c);
    profiler_set_current(old_prof);
    return rc;
}
