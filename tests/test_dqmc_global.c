#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "global_select.h"
#include "green.h"
#include "lattice.h"
#include "model.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Lattice L;
    Model m;
    Field f;
    Rng r;
    Dqmc D;
} Sys;

static void sys_make(Sys *s, int nsite, int pbc, int Ltr, double U, int half,
                     DqmcSweepMode sm, GreenRebuildMode gm, uint64_t seed)
{
    lattice_chain(&s->L, nsite, -1.0, pbc);
    model_init(&s->m, &s->L, U, 0.1, half, half ? 0.0 : U / 2.0);
    rng_seed(&s->r, seed);
    field_init(&s->f, s->L.n, Ltr, U, 0.1, &s->r);
    CHECK(dqmc_init_modes(&s->D, &s->m, &s->f, &s->r, 4, NULL, sm, gm) == 0);
}

static void sys_free(Sys *s)
{
    dqmc_free(&s->D);
    field_free(&s->f);
    model_free(&s->m);
    lattice_free(&s->L);
}

/* spec 7.3: with L = 1 a world-line flip is one local flip. */
static void check_matches_local_ratio(void)
{
    Sys s;
    sys_make(&s, 4, 1, 1, 4.0, 1, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 11);
    for (int i = 0; i < 4; i++) {
        const double Nu = green_flipN(&s.D.Gu, i);
        const double Nd = field_N(&s.f, -1.0, s.f.s[i]);
        const double R = green_delay_ratio_N(&s.D.Gu, i, Nu) *
                         green_ph_down_ratio_N(&s.D.Gu, i, Nd);
        double w0 = 0.0, w1 = 0.0;
        int s0 = 0, s1 = 0;
        CHECK(dqmc_log_weight(&s.D, &w0, &s0) == 0);
        field_flip_site_worldline(&s.f, i);
        CHECK(dqmc_log_weight(&s.D, &w1, &s1) == 0);
        field_flip_site_worldline(&s.f, i);
        CHECK_CLOSE(exp(w1 - w0), fabs(R), 1e-9);
    }
    sys_free(&s);
}

/* spec 7.2: PH formula equals the two-spin evaluation; two-spin path works. */
static void check_weight_paths(void)
{
    Sys s;
    sys_make(&s, 4, 1, 8, 4.0, 1, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 12);
    CHECK(s.D.use_ph == 1);
    Green Gd;
    green_alloc(&Gd, &s.m, &s.f, -1.0);
    for (int i = 0; i < 4; i++) {
        double w = 0.0, lu = 0.0, ld = 0.0;
        int sg = 0, su = 0, sd = 0;
        CHECK(dqmc_log_weight(&s.D, &w, &sg) == 0);
        CHECK(green_logdet_full(&s.D.Gu, 4, &su, &lu) == 0);
        CHECK(green_logdet_full(&Gd, 4, &sd, &ld) == 0);
        CHECK_CLOSE(w, lu + ld, 1e-10);
        CHECK(sg == su * sd);
        /* delta form of spec 3.2 */
        const int mi = field_site_sum(&s.f, i);
        field_flip_site_worldline(&s.f, i);
        double w2 = 0.0, lu2 = 0.0;
        int sg2 = 0, su2 = 0;
        CHECK(dqmc_log_weight(&s.D, &w2, &sg2) == 0);
        CHECK(green_logdet_full(&s.D.Gu, 4, &su2, &lu2) == 0);
        CHECK_CLOSE(w2 - w, 2.0 * (lu2 - lu) + 2.0 * s.f.lambda * (double)mi,
                    1e-10);
    }
    green_free(&Gd);
    sys_free(&s);

    Sys t;
    sys_make(&t, 4, 1, 8, 4.0, 0, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 13);
    CHECK(t.D.use_ph == 0);
    double w = 0.0, lu = 0.0, ld = 0.0;
    int sg = 0, su = 0, sd = 0;
    CHECK(dqmc_log_weight(&t.D, &w, &sg) == 0);
    CHECK(green_logdet_full(&t.D.Gu, 4, &su, &lu) == 0);
    CHECK(green_logdet_full(&t.D.Gd, 4, &sd, &ld) == 0);
    CHECK_CLOSE(w, lu + ld, 1e-12);
    CHECK(dqmc_global_site_pass(&t.D) == 0);
    CHECK(t.D.global_attempts == 4ULL);
    CHECK(t.D.status == 0);
    sys_free(&t);
}

/* Dense, independent log|W| = log|det(1+A_up)| + log|det(1+A_down)| (no UDV, no PH identity). */
static double dense_logw(Sys *s, Green *Gd_ref)
{
    double total = 0.0;
    Green *G[2] = {&s->D.Gu, Gd_ref};
    for (int k = 0; k < 2; k++) {
        const int n = G[k]->n;
        double A[16], Bl[16], tmp[16];
        la_eye(n, A);
        for (int l = 0; l < G[k]->L; l++) {
            green_build_B(G[k], l, Bl);
            la_matmul(n, Bl, A, tmp);
            memcpy(A, tmp, sizeof(double) * (size_t)n * n);
        }
        for (int i = 0; i < n; i++) {
            A[i + i * n] += 1.0;
        }
        int sg = 0;
        double la = 0.0;
        la_logdet(n, A, &sg, &la);
        CHECK(sg != 0 && isfinite(la));
        total += la;
    }
    return total;
}

/* spec 7.4: drive the production 1-site step through every configuration using
   independent dense weights. Test both sides of the acceptance boundary away
   from unity, and valid acceptance inputs near unity. Unconditional acceptance,
   an inverted ratio or a missing rollback fail here. */
static void check_exact_kernels(int half)
{
    enum { NS = 2, LT = 4, NC = 256 };
    Sys s;
    sys_make(&s, NS, 0, LT, 4.0, half, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 14);
    Green Gd;
    green_alloc(&Gd, &s.m, &s.f, -1.0);
    static double logw[NC], pi[NC], P[2][NC][NC], v[NC], u1[NC];
    for (int c = 0; c < NC; c++) {
        for (int k = 0; k < NS * LT; k++) {
            s.f.s[k] = (c >> k & 1) ? 1 : -1;
        }
        logw[c] = dense_logw(&s, &Gd);
    }
    double wmax = logw[0], Z = 0.0;
    for (int c = 1; c < NC; c++) {
        wmax = logw[c] > wmax ? logw[c] : wmax;
    }
    for (int c = 0; c < NC; c++) {
        pi[c] = exp(logw[c] - wmax);
        Z += pi[c];
    }
    for (int c = 0; c < NC; c++) {
        pi[c] /= Z;
    }
    int rejected_cases = 0;
    for (int i = 0; i < NS; i++) {
        int mask = 0;
        for (int l = 0; l < LT; l++) {
            mask |= 1 << (l * NS + i);
        }
        memset(P[i], 0, sizeof P[i]);
        for (int c = 0; c < NC; c++) {
            const int d = c ^ mask;
            const double a = logw[d] >= logw[c] ? 1.0 : exp(logw[d] - logw[c]);
            P[i][c][d] = a;
            P[i][c][c] += 1.0 - a;
            /* Independent dense/UDV weights can differ by roundoff near a=1.
               Only test rejection when a valid interval above a remains. */
            const double unity_margin = 1e-8;
            const int test_rejection = a < 1.0 - unity_margin;
            const double us[2] = {
                a * (1.0 - 1e-9),
                test_rejection ? a * (1.0 + 1e-9) : 1.0 - 2.0 * unity_margin
            };
            const int want[2] = {1, test_rejection ? 0 : 1};
            CHECK(isfinite(a) && a > 0.0 && a <= 1.0);
            for (int t = 0; t < 2; t++) {
                CHECK(isfinite(us[t]) && us[t] >= 0.0 && us[t] < 1.0);
                for (int k = 0; k < NS * LT; k++) {
                    s.f.s[k] = (c >> k & 1) ? 1 : -1;
                }
                double lw = 0.0;
                int sg = 0, acc = -1;
                CHECK(dqmc_log_weight(&s.D, &lw, &sg) == 0);
                CHECK_CLOSE(lw - wmax, logw[c] - wmax, 1e-10);
                const unsigned long long att0 = s.D.global_attempts;
                const unsigned long long acc0 = s.D.global_accepted;
                CHECK(dqmc_global_site_step(&s.D, i, us[t], &lw, &sg, &acc) == 0);
                CHECK(acc == want[t]);
                rejected_cases += (acc == 0);
                int now = 0;
                for (int k = 0; k < NS * LT; k++) {
                    now |= (s.f.s[k] > 0) << k;
                }
                CHECK(now == (acc ? d : c));          /* flipped, or fully rolled back */
                CHECK_CLOSE(lw - wmax, logw[acc ? d : c] - wmax, 1e-10);
                CHECK(s.D.global_attempts == att0 + 1ULL);
                CHECK(s.D.global_accepted == acc0 + (unsigned long long)acc);
            }
        }
        for (int c = 0; c < NC; c++) {
            const int d = c ^ mask;
            CHECK_CLOSE(pi[c] * P[i][c][d], pi[d] * P[i][d][c], 1e-14);
        }
    }
    CHECK(rejected_cases > 100);                       /* the reject branch really ran */
    for (int d = 0; d < NC; d++) {
        u1[d] = 0.0;
        for (int c = 0; c < NC; c++) {
            u1[d] += pi[c] * P[0][c][d];
        }
    }
    for (int d = 0; d < NC; d++) {
        v[d] = 0.0;
        for (int c = 0; c < NC; c++) {
            v[d] += u1[c] * P[1][c][d];
        }
        CHECK_CLOSE(v[d], pi[d], 1e-14);               /* fixed-order pass keeps pi */
    }
    green_free(&Gd);
    sys_free(&s);
}

/* spec 7.6: after a pass the object equals a fresh one built from the same field. */
static int g_saw_none, g_saw_some, g_saw_all;

static void check_state_after_pass(int half, DqmcSweepMode sm, GreenRebuildMode gm,
                                   int presweeps, double U, uint64_t seed,
                                   int polarized)
{
    Sys s;
    sys_make(&s, 4, 1, 10, U, half, sm, gm, seed);
    if (polarized) {
        CHECK(dqmc_set_global_site_select(&s.D, 1, 2.0) == 0);
    }
    CHECK(s.D.use_ph == half);
    for (int k = 0; k < presweeps; k++) {
        dqmc_sweep(&s.D);
    }
    signed char before[40];
    memcpy(before, s.f.s, sizeof before);
    const unsigned long long acc0 = s.D.global_accepted;
    CHECK(dqmc_global_site_pass(&s.D) == 0);
    CHECK(s.D.status == 0);
    CHECK(s.D.global_attempts == 4ULL);
    CHECK(s.D.Gu.cur_l == 0);
    CHECK(green_delay_count(&s.D.Gu) == 0);
    if (!half) {
        CHECK(s.D.Gd.cur_l == 0);
        CHECK(green_delay_count(&s.D.Gd) == 0);
    }
    CHECK(s.D.carried_prefix_valid == 0 && s.D.carried_suffix_valid == 0);
    /* every site is either fully flipped or untouched; polarized selection may
       re-flip a site within the pass. */
    int flipped_sites = 0;
    for (int i = 0; i < 4; i++) {
        const int same = s.f.s[i] == before[i];
        for (int l = 0; l < 10; l++) {
            CHECK((s.f.s[l * 4 + i] == before[l * 4 + i]) == same);
        }
        flipped_sites += !same;
    }
    const unsigned long long acc = s.D.global_accepted - acc0;
    if (polarized) {
        CHECK((unsigned long long)flipped_sites <= acc);
        CHECK(((unsigned long long)flipped_sites % 2ULL) == (acc % 2ULL));
    } else {
        CHECK((unsigned long long)flipped_sites == acc);
    }
    g_saw_none |= (flipped_sites == 0);
    g_saw_some |= (flipped_sites > 0 && flipped_sites < 4);
    g_saw_all |= (flipped_sites == 4);

    Sys ref;
    lattice_chain(&ref.L, 4, -1.0, 1);
    model_init(&ref.m, &ref.L, U, 0.1, half, U / 2.0);
    rng_seed(&ref.r, 1);
    field_init(&ref.f, 4, 10, U, 0.1, &ref.r);
    memcpy(ref.f.s, s.f.s, 40);
    CHECK(dqmc_init_modes(&ref.D, &ref.m, &ref.f, &ref.r, 4, NULL, sm, gm) == 0);
    ref.r = s.r;                       /* same RNG state */
    ref.D.sweep_dir = s.D.sweep_dir;   /* same direction */
    for (int k = 0; k < 16; k++) {
        CHECK_CLOSE(s.D.Gu.g[k], ref.D.Gu.g[k], 1e-10);
        CHECK_CLOSE(s.D.Gd.g[k], ref.D.Gd.g[k], 1e-10);
    }
    CHECK_CLOSE(s.D.sign, ref.D.sign, 0.0);
    dqmc_sweep(&s.D);
    dqmc_sweep(&ref.D);
    CHECK(memcmp(s.f.s, ref.f.s, 40) == 0);
    CHECK(memcmp(&s.r, &ref.r, sizeof(Rng)) == 0);
    for (int k = 0; k < 16; k++) {
        CHECK_CLOSE(s.D.Gu.g[k], ref.D.Gu.g[k], 1e-9);
        CHECK_CLOSE(s.D.Gd.g[k], ref.D.Gd.g[k], 1e-9);
    }
    sys_free(&ref);
    sys_free(&s);
}

/* spec 3.3 step 5 / 7.11: a numerical failure is not a rejection. */
static void check_failure_sets_status(void)
{
    Sys s;
    sys_make(&s, 4, 1, 8, 4.0, 1, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 31);
    signed char before[32];
    memcpy(before, s.f.s, sizeof before);
    s.D.Gu.work.failed = 1;            /* injected linear-algebra failure */
    CHECK(dqmc_global_site_pass(&s.D) != 0);
    CHECK(s.D.status == 1);
    CHECK(s.D.global_accepted == 0ULL);
    CHECK(memcmp(before, s.f.s, sizeof before) == 0);
    CHECK(dqmc_global_site_pass(&s.D) != 0);   /* no-op once failed */
    sys_free(&s);

    /* failure while evaluating the candidate: current weight is fine, the flipped one fails */
    Sys c;
    sys_make(&c, 4, 1, 8, 4.0, 1, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 32);
    memcpy(before, c.f.s, sizeof before);
    double lw = 0.0;
    int sg = 0, acc = -1;
    CHECK(dqmc_log_weight(&c.D, &lw, &sg) == 0);
    const double lw0 = lw;
    c.D.Gu.work.failed = 1;
    CHECK(dqmc_global_site_step(&c.D, 2, 0.0, &lw, &sg, &acc) != 0);  /* u=0 would accept anything */
    CHECK(acc == 0);
    CHECK(c.D.status == 1);
    CHECK(memcmp(before, c.f.s, sizeof before) == 0);                  /* rolled back, not accepted */
    CHECK_CLOSE(lw, lw0, 0.0);
    const unsigned long long sweeps = c.D.sweep_count;
    dqmc_sweep(&c.D);                                                  /* later updates stop */
    CHECK(c.D.sweep_count == sweeps);
    CHECK(memcmp(before, c.f.s, sizeof before) == 0);
    sys_free(&c);
}

/* Stage A diagnostic: one entry per attempt, accepted count matches the pass counters,
   and the random stream is unchanged by the diagnostic. */
static void check_site_diag_records(int half)
{
    Sys a, b;
    sys_make(&a, 4, 1, 8, 4.0, half, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 21);
    sys_make(&b, 4, 1, 8, 4.0, half, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 21);
    CHECK(dqmc_enable_global_site_diag(&b.D, 1) == 0);
    CHECK(b.D.site_diag != NULL);
    for (int pass = 0; pass < 3; pass++) {
        dqmc_sweep(&a.D);
        dqmc_sweep(&b.D);
        CHECK(dqmc_global_site_pass(&a.D) == 0);
        CHECK(dqmc_global_site_pass(&b.D) == 0);
    }
    /* same field and same counters with and without the diagnostic */
    CHECK(memcmp(a.f.s, b.f.s, (size_t)a.f.n * (size_t)a.f.L) == 0);
    CHECK(a.D.global_attempts == b.D.global_attempts);
    CHECK(a.D.global_accepted == b.D.global_accepted);
    CHECK(rng_double(&a.r) == rng_double(&b.r));
    unsigned long long att[2] = {0ULL, 0ULL}, acc[2] = {0ULL, 0ULL};
    for (int r = 0; r < 2; r++) {
        for (int k = 0; k < GLOBAL_SITE_DIAG_NBIN; k++) {
            att[r] += b.D.site_diag->attempts[r][k];
            acc[r] += b.D.site_diag->accepted[r][k];
        }
    }
    CHECK(att[0] == 3ULL * 4ULL && att[1] == 3ULL * 4ULL);
    CHECK(acc[0] == b.D.global_accepted && acc[1] == b.D.global_accepted);
    /* disabling frees; enabling twice is idempotent */
    CHECK(dqmc_enable_global_site_diag(&b.D, 1) == 0);
    CHECK(dqmc_enable_global_site_diag(&b.D, 0) == 0);
    CHECK(b.D.site_diag == NULL);
    sys_free(&a);
    sys_free(&b);
}

/* Stage B (spec 3.2): the polarized pass equals a manual replay that uses the
   selector with the weights of the pass-initial field and two draws per attempt
   (site, then acceptance). System b is only a field + log-weight helper: it is
   never swept, its field is copied from a before every round, and
   dqmc_log_weight/dqmc_global_site_step work from the field alone (as in the
   dense oracle). Three rounds so that a same-site re-selection is exercised. */
static void check_polarized_replay(int half)
{
    Sys a, b;
    sys_make(&a, 4, 1, 8, 4.0, half, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 31);
    sys_make(&b, 4, 1, 8, 4.0, half, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 31);
    CHECK(dqmc_set_global_site_select(&a.D, 1, 2.0) == 0);
    CHECK(a.D.site_select_polarized == 1);
    int repeats = 0;
    for (int round = 0; round < 3; round++) {
        dqmc_sweep(&a.D);
        memcpy(b.f.s, a.f.s, 32);
        int sums[4];
        double p[4], d[4], w[4], cum[4];
        field_site_sums(&b.f, sums);
        CHECK(global_site_indicators(sums, b.m.bipart, 4, 8, p, d) == 0);
        CHECK(global_site_weights_p(p, 4, global_site_weight_scale(b.f.lambda), 2.0, w, cum) == 0);
        Rng replay = a.r;                       /* the stream the pass will use */
        const unsigned long long acc0 = a.D.global_accepted;
        CHECK(dqmc_global_site_pass(&a.D) == 0);
        CHECK(a.D.global_attempts == 4ULL * (unsigned long long)(round + 1));
        double lw = 0.0;
        int sg = 0;
        CHECK(dqmc_log_weight(&b.D, &lw, &sg) == 0);
        unsigned long long accepted = 0ULL;
        int chosen[4];
        for (int k = 0; k < 4; k++) {
            const double us = rng_double(&replay);
            chosen[k] = global_site_select_index(cum, 4, us);
            CHECK(chosen[k] >= 0 && chosen[k] < 4);
            const double ua = rng_double(&replay);
            int acc = 0;
            CHECK(dqmc_global_site_step(&b.D, chosen[k], ua, &lw, &sg, &acc) == 0);
            accepted += (unsigned long long)acc;
        }
        for (int k = 1; k < 4; k++) {
            for (int j = 0; j < k; j++) {
                repeats += chosen[k] == chosen[j];
            }
        }
        CHECK(memcmp(a.f.s, b.f.s, 32) == 0);           /* same proposals, same decisions */
        CHECK(a.D.global_accepted - acc0 == accepted);
        CHECK(memcmp(&a.r, &replay, sizeof(Rng)) == 0); /* exactly 2 draws per attempt */
    }
    CHECK(repeats > 0);   /* some site was drawn twice in a pass (seed-dependent; change seed 31 once if not) */
    /* switching the mode off restores the fixed order and frees the work arrays */
    CHECK(dqmc_set_global_site_select(&a.D, 0, 0.0) == 0);
    CHECK(a.D.site_select_polarized == 0 && a.D.site_select_cum == NULL);
    CHECK(dqmc_set_global_site_select(&a.D, 1, -1.0) == 1);
    sys_free(&a);
    sys_free(&b);
}

/* Stage B: unusable weights are a numerical failure before any draw. Site 0 is
   fully polarized (p = 1 > p_0 = tanh(lambda)), so alpha = 1e300 overflows. */
static void check_polarized_unusable_weights_fail(void)
{
    Sys s;
    sys_make(&s, 4, 1, 8, 4.0, 1, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 61);
    for (int l = 0; l < 8; l++) {
        s.f.s[l * 4 + 0] = 1;
    }
    CHECK(dqmc_set_global_site_select(&s.D, 1, 1e300) == 0);   /* a finite input */
    const Rng before = s.r;
    CHECK(dqmc_global_site_pass(&s.D) == 1);
    CHECK(s.D.status == 1);
    CHECK(s.D.global_attempts == 0ULL);                          /* failed before the first draw */
    CHECK(memcmp(&s.r, &before, sizeof(Rng)) == 0);
    sys_free(&s);
    /* Use the actual Field.lambda from U=4, dtau=0.1 (p0 ~= 0.574), not
       Task 1's p0=0.5. alpha=80 gives finite but unusable weights here. */
    Sys t;
    sys_make(&t, 2, 0, 8, 4.0, 1, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 62);
    for (int l = 0; l < 8; l++) {
        t.f.s[l * 2 + 0] = 1;
        t.f.s[l * 2 + 1] = (l % 2) ? 1 : -1;                     /* m_1 = 0 */
    }
    int sums[2];
    double p[2], d[2], w[2], cum[2];
    field_site_sums(&t.f, sums);
    CHECK(global_site_indicators(sums, t.m.bipart, 2, 8, p, d) == 0);
    CHECK(p[0] == 1.0 && p[1] == 0.0);
    const double p0 = global_site_weight_scale(t.f.lambda);
    CHECK(isfinite(pow(p[0] / p0, 80.0) + 0.5));
    CHECK(isfinite(pow(p[1] / p0, 80.0) + 0.5));
    CHECK(global_site_weights_p(p, 2, p0, 80.0, w, cum) == 1);
    CHECK(dqmc_set_global_site_select(&t.D, 1, 80.0) == 0);
    const Rng rng_before = t.r;
    signed char field_before[16];
    memcpy(field_before, t.f.s, sizeof field_before);
    CHECK(dqmc_global_site_pass(&t.D) == 1);
    CHECK(t.D.status == 1 && t.D.global_attempts == 0ULL);
    CHECK(t.D.global_accepted == 0ULL);
    CHECK(memcmp(&t.r, &rng_before, sizeof(Rng)) == 0);
    CHECK(memcmp(t.f.s, field_before, sizeof field_before) == 0);
    sys_free(&t);
}

/* Stage B (spec 8): dense oracle. One polarized attempt is the kernel
   P(c -> c^mask_i) = T_i(c) a_i(c) with T_i = w_i/W from the p-weights of c and
   Metropolis a_i; T_i(c) equals T_i(c^mask_i), so detailed balance and pi P = pi
   hold, and so does the n-attempt composition. */
static void check_exact_kernels_polarized(int half)
{
    enum { NS = 2, LT = 4, NC = 256 };
    Sys s;
    sys_make(&s, NS, 0, LT, 4.0, half, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 15);
    Green Gd;
    green_alloc(&Gd, &s.m, &s.f, -1.0);
    static double logw[NC], pi[NC], P[NC][NC], T[NC][NS], v[NC], v2[NC];
    const double p0 = global_site_weight_scale(s.f.lambda);
    for (int c = 0; c < NC; c++) {
        for (int k = 0; k < NS * LT; k++) {
            s.f.s[k] = (c >> k & 1) ? 1 : -1;
        }
        logw[c] = dense_logw(&s, &Gd);
        int sums[NS];
        double p[NS], d[NS], w[NS], cum[NS];
        field_site_sums(&s.f, sums);
        CHECK(global_site_indicators(sums, s.m.bipart, NS, LT, p, d) == 0);
        CHECK(global_site_weights_p(p, NS, p0, 2.0, w, cum) == 0);
        for (int i = 0; i < NS; i++) {
            T[c][i] = w[i] / cum[NS - 1];
        }
        /* the selector reproduces T: u below T_0 picks site 0, otherwise site 1 */
        CHECK(global_site_select_index(cum, NS, 0.5 * T[c][0]) == 0);
        CHECK(global_site_select_index(cum, NS, T[c][0] + 0.5 * T[c][1]) == 1);
    }
    double wmax = logw[0], Z = 0.0;
    for (int c = 1; c < NC; c++) {
        wmax = logw[c] > wmax ? logw[c] : wmax;
    }
    for (int c = 0; c < NC; c++) {
        pi[c] = exp(logw[c] - wmax);
        Z += pi[c];
    }
    for (int c = 0; c < NC; c++) {
        pi[c] /= Z;
    }
    memset(P, 0, sizeof P);
    for (int c = 0; c < NC; c++) {
        double stay = 1.0;
        for (int i = 0; i < NS; i++) {
            int mask = 0;
            for (int l = 0; l < LT; l++) {
                mask |= 1 << (l * NS + i);
            }
            const int d = c ^ mask;
            CHECK_CLOSE(T[c][i], T[d][i], 1e-15);        /* weight invariance -> Hastings factor 1 */
            const double a = logw[d] >= logw[c] ? 1.0 : exp(logw[d] - logw[c]);
            P[c][d] += T[c][i] * a;
            stay -= T[c][i] * a;
        }
        P[c][c] += stay;
        CHECK(stay >= -1e-15);
    }
    for (int c = 0; c < NC; c++) {
        for (int d = 0; d < NC; d++) {
            CHECK_CLOSE(pi[c] * P[c][d], pi[d] * P[d][c], 1e-14);   /* detailed balance */
        }
    }
    for (int d = 0; d < NC; d++) {
        v[d] = 0.0;
        for (int c = 0; c < NC; c++) {
            v[d] += pi[c] * P[c][d];
        }
        CHECK_CLOSE(v[d], pi[d], 1e-14);                /* one attempt keeps pi */
    }
    for (int d = 0; d < NC; d++) {
        v2[d] = 0.0;
        for (int c = 0; c < NC; c++) {
            v2[d] += v[c] * P[c][d];
        }
        CHECK_CLOSE(v2[d], pi[d], 1e-14);               /* n = 2 attempts keep pi */
    }
    green_free(&Gd);
    sys_free(&s);
}

int main(void)
{
    check_matches_local_ratio();
    check_weight_paths();
    check_state_after_pass(0, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 2, 4.0, 41, 0);
    check_state_after_pass(0, DQMC_SWEEP_FORWARD, GREEN_REBUILD_CENTERED, 2, 4.0, 42, 0);
    check_state_after_pass(0, DQMC_SWEEP_FORWARD, GREEN_REBUILD_TWO_SIDED, 2, 4.0, 43, 0);
    check_state_after_pass(0, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 1, 0.0, 44, 0);
    check_failure_sets_status();
    check_exact_kernels(1);
    check_exact_kernels(0);
    check_state_after_pass(1, DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE, 0, 4.0, 21, 0);
    check_state_after_pass(1, DQMC_SWEEP_ALTERNATING, GREEN_REBUILD_COMBINE, 1, 4.0, 22, 0); /* next: backward */
    check_state_after_pass(1, DQMC_SWEEP_ALTERNATING, GREEN_REBUILD_COMBINE, 2, 4.0, 23, 0); /* next: forward */
    check_state_after_pass(1, DQMC_SWEEP_ALTERNATING, GREEN_REBUILD_CENTERED, 3, 4.0, 24, 0);
    check_state_after_pass(1, DQMC_SWEEP_FORWARD, GREEN_REBUILD_TWO_SIDED, 3, 4.0, 25, 0);
    check_state_after_pass(1, DQMC_SWEEP_ALTERNATING, GREEN_REBUILD_COMBINE, 1, 0.0, 26, 0); /* U=0: delta=0, all accepted */
    for (uint64_t seed = 40; seed < 60 && !(g_saw_none && g_saw_some); seed++) {
        check_state_after_pass(1, DQMC_SWEEP_ALTERNATING, GREEN_REBUILD_COMBINE, 2, 8.0, seed, 0);
    }
    CHECK(g_saw_none);   /* an all-rejected pass was exercised */
    CHECK(g_saw_some);   /* a mixed pass was exercised */
    CHECK(g_saw_all);    /* an all-accepted pass was exercised */
    check_site_diag_records(1);
    check_site_diag_records(0);
    check_state_after_pass(1, DQMC_SWEEP_ALTERNATING, GREEN_REBUILD_COMBINE, 2, 4.0, 51, 1);
    check_state_after_pass(0, DQMC_SWEEP_FORWARD, GREEN_REBUILD_TWO_SIDED, 2, 4.0, 52, 1);
    check_state_after_pass(1, DQMC_SWEEP_ALTERNATING, GREEN_REBUILD_COMBINE, 1, 0.0, 53, 1); /* U=0: p0 = 1 rule, all accepted */
    check_polarized_replay(1);
    check_polarized_replay(0);
    check_exact_kernels_polarized(1);
    check_exact_kernels_polarized(0);
    check_polarized_unusable_weights_fail();
    TEST_END();
}
