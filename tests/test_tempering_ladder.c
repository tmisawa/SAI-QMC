#include "test_util.h"
#include "dqmc.h"
#include "tempering.h"

#include <stdlib.h>
#include <string.h>

enum { NS = 4, LT = 8, K = 3 };

typedef struct { Model m; Rng r; Field f; Dqmc D; } Slot;

static void slot_init(Slot *x, const Lattice *L, double dtau, uint64_t seed)
{
    model_init(&x->m, L, 4.0, dtau, 1, 0.0);
    rng_seed(&x->r, seed);
    field_init(&x->f, NS, LT, 4.0, dtau, &x->r);
    CHECK(dqmc_init_modes(&x->D, &x->m, &x->f, &x->r, 4, NULL,
                          DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE) == 0);
}

typedef struct {
    Rng r;
    double gu[NS * NS], gd[NS * NS];
    double sign;
    int cur_u, cur_d, pre, suf, status, delay_u, det_u;
} SlotState;

static void snap(const Slot *x, SlotState *o)
{
    o->r = x->r;
    memcpy(o->gu, x->D.Gu.g, sizeof o->gu);
    memcpy(o->gd, x->D.Gd.g, sizeof o->gd);
    o->sign = x->D.sign;
    o->cur_u = x->D.Gu.cur_l;
    o->cur_d = x->D.Gd.cur_l;
    o->pre = x->D.carried_prefix_valid;
    o->suf = x->D.carried_suffix_valid;
    o->status = x->D.status;
    o->delay_u = x->D.Gu.delay_count;
    o->det_u = x->D.Gu.det_sign;
}

static int same_state(const Slot *x, const SlotState *o)
{
    SlotState now;
    snap(x, &now);
    return memcmp(&now.r, &o->r, sizeof o->r) == 0 &&
           memcmp(now.gu, o->gu, sizeof o->gu) == 0 &&
           memcmp(now.gd, o->gd, sizeof o->gd) == 0 &&
           now.sign == o->sign && now.cur_u == o->cur_u && now.cur_d == o->cur_d &&
           now.pre == o->pre && now.suf == o->suf && now.status == o->status &&
           now.delay_u == o->delay_u && now.det_u == o->det_u;
}

int main(void)
{
    Lattice L;
    lattice_chain(&L, NS, -1.0, 1);
    Slot s[K];
    Dqmc *ptr[K];
    const double dtau[K] = {0.05, 0.075, 0.1};   /* beta = 0.4, 0.6, 0.8 */
    for (int k = 0; k < K; k++) {
        slot_init(&s[k], &L, dtau[k], 100 + k);
        ptr[k] = &s[k].D;
    }
    TemperingLadder T;
    CHECK(tempering_ladder_init(&T, ptr, 1, 7) != 0);
    CHECK(tempering_ladder_init(&T, ptr, K, 7) == 0);

    const size_t len = (size_t)NS * LT;
    int n_acc = 0, n_rej = 0;
    for (int trial = 0; trial < 200 && (n_acc == 0 || n_rej == 0); trial++) {
        for (int k = 0; k < K; k++) {
            dqmc_sweep(ptr[k]);
        }
        signed char *before0 = malloc(len), *before1 = malloc(len);
        memcpy(before0, s[0].f.s, len);
        memcpy(before1, s[1].f.s, len);
        /* state that affects the next sweep, for both slots of the pair.
           Scratch (B, Binv, tmp, work) is legitimately overwritten by
           green_logdet_full and is not compared. */
        SlotState st0, st1;
        snap(&s[0], &st0);
        snap(&s[1], &st1);
        /* the exchange RNG advances by exactly one draw per attempt */
        Rng probe = T.rng;
        const double u_expected = rng_double(&probe);
        double w00, w11, w01, w10;
        int sg;
        CHECK(dqmc_log_weight(ptr[0], &w00, &sg) == 0);
        CHECK(dqmc_log_weight(ptr[1], &w11, &sg) == 0);
        CHECK(dqmc_log_weight_of(ptr[0], before1, &w01, &sg) == 0);
        CHECK(dqmc_log_weight_of(ptr[1], before0, &w10, &sg) == 0);
        const int expect = tempering_accept(tempering_log_ratio(w00, w11, w01, w10), u_expected);
        const Rng slot_rng0 = s[0].r;
        int acc = -1;
        CHECK(tempering_ladder_try_pair(&T, 0, &acc) == 0);
        CHECK(acc == expect);
        CHECK(memcmp(&T.rng, &probe, sizeof probe) == 0);
        CHECK(memcmp(&s[0].r, &slot_rng0, sizeof slot_rng0) == 0);  /* slot RNG untouched */
        if (acc) {
            n_acc++;
            CHECK(memcmp(s[0].f.s, before1, len) == 0);
            CHECK(memcmp(s[1].f.s, before0, len) == 0);
            for (int k = 0; k < 2; k++) {
                const Dqmc *D = ptr[k];
                CHECK(D->Gu.cur_l == 0 && D->Gd.cur_l == 0);
                CHECK(D->carried_prefix_valid == 0 && D->carried_suffix_valid == 0);
                CHECK(D->sign == 1.0 && D->status == 0 && D->Gu.delay_count == 0);
                /* rebuilt G equals an independent rebuild of the swapped field */
                Slot ref;
                slot_init(&ref, &L, dtau[k], 555);
                memcpy(ref.f.s, s[k].f.s, len);
                CHECK(green_from_scratch(&ref.D.Gu, 0) == 0);
                for (int q = 0; q < NS * NS; q++) {
                    CHECK_CLOSE(D->Gu.g[q], ref.D.Gu.g[q], 1e-12);
                }
                dqmc_free(&ref.D);
                field_free(&ref.f);
                model_free(&ref.m);
            }
            double w_new;
            CHECK(dqmc_log_weight(ptr[0], &w_new, &sg) == 0);
            CHECK_CLOSE(w_new, w01, 1e-12 * (1.0 + fabs(w01)));
        } else {
            n_rej++;
            CHECK(memcmp(s[0].f.s, before0, len) == 0);
            CHECK(memcmp(s[1].f.s, before1, len) == 0);
            CHECK(same_state(&s[0], &st0));   /* rejected: nothing that matters changed */
            CHECK(same_state(&s[1], &st1));
        }
        free(before0);
        free(before1);
    }
    CHECK(n_acc > 0);
    CHECK(n_rej > 0);

    /* a round uses pairs (0,1) on even rounds and (1,2) on odd rounds */
    const unsigned long long a01 = T.stats.attempts[0], a12 = T.stats.attempts[1];
    const unsigned long long r0 = T.round;
    CHECK(tempering_ladder_round(&T) == 0);
    CHECK(T.round == r0 + 1);
    CHECK(T.stats.attempts[tempering_first_pair(r0)] ==
          (tempering_first_pair(r0) == 0 ? a01 : a12) + 1);

    /* a failed slot makes the attempt fail, never a silent rejection */
    s[2].D.status = 1;
    int acc = -1;
    CHECK(tempering_ladder_try_pair(&T, 1, &acc) != 0);
    CHECK(T.fail_pair == 1);

    tempering_ladder_free(&T);
    for (int k = 0; k < K; k++) {
        dqmc_free(&s[k].D);
        field_free(&s[k].f);
        model_free(&s[k].m);
    }
    lattice_free(&L);
    TEST_END();
}
