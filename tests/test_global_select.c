#include "test_util.h"
#include "global_select.h"

#include <stdint.h>
#include <string.h>

static void check_every_site_reachable(const double *cum, int n)
{
    const uint64_t grid_size = UINT64_C(1) << 53;
    for (int site = 0; site < n; site++) {
        uint64_t lo = 0, hi = grid_size;
        while (lo < hi) {
            const uint64_t mid = lo + (hi - lo) / 2;
            const double u = (double)mid * 0x1p-53;
            if (global_site_select_index(cum, n, u) < site) lo = mid + 1;
            else hi = mid;
        }
        CHECK(lo < grid_size);
        if (lo < grid_size) {
            CHECK(global_site_select_index(cum, n, (double)lo * 0x1p-53) == site);
        }
    }
}

int main(void)
{
    /* 4-site chain, L = 8: m = (8, -6, 0, 2), bipart = (+1,-1,+1,-1) */
    const int m[4] = {8, -6, 0, 2};
    const int bip[4] = {1, -1, 1, -1};
    double p[4], d[4];
    CHECK(global_site_indicators(m, bip, 4, 8, p, d) == 0);
    CHECK_CLOSE(p[0], 1.0, 1e-15);
    CHECK_CLOSE(p[1], 0.75, 1e-15);
    CHECK_CLOSE(p[2], 0.0, 1e-15);
    CHECK_CLOSE(p[3], 0.25, 1e-15);
    /* M = 8 + 6 + 0 - 2 = 12 > 0 -> sign +1; d_i = -eps_i m_i / L */
    CHECK_CLOSE(d[0], -1.0, 1e-15);
    CHECK_CLOSE(d[1], -0.75, 1e-15);
    CHECK_CLOSE(d[2], 0.0, 1e-15);
    CHECK_CLOSE(d[3], 0.25, 1e-15);
    /* flipping site 0 negates m0: M = -8 + 6 + 0 - 2 = -4 -> sign -1 */
    const int m2[4] = {-8, -6, 0, 2};
    CHECK(global_site_indicators(m2, bip, 4, 8, p, d) == 0);
    CHECK_CLOSE(p[0], 1.0, 1e-15);
    CHECK_CLOSE(d[0], -1.0, 1e-15);   /* -(+1)(-8)(-1)/8 */
    CHECK_CLOSE(d[1], 0.75, 1e-15);   /* -(-1)(-6)(-1)/8 */
    /* M = 0 counts as sign +1 */
    const int m3[2] = {4, 4};
    const int bip2[2] = {1, -1};
    CHECK(global_site_indicators(m3, bip2, 2, 4, p, d) == 0);
    CHECK_CLOSE(d[0], -1.0, 1e-15);
    CHECK_CLOSE(d[1], 1.0, 1e-15);
    /* NULL bipart -> d = 0 */
    CHECK(global_site_indicators(m3, NULL, 2, 4, p, d) == 0);
    CHECK_CLOSE(d[0], 0.0, 1e-15);
    CHECK(global_site_indicators(NULL, bip2, 2, 4, p, d) == 1);
    CHECK(global_site_indicators(m3, bip2, 0, 4, p, d) == 1);
    CHECK(global_site_indicators(m3, bip2, 2, 0, p, d) == 1);

    /* bins */
    CHECK(global_site_diag_bin_p(0.0) == 0);
    CHECK(global_site_diag_bin_p(0.019) == 0);
    CHECK(global_site_diag_bin_p(0.02) == 1);
    CHECK(global_site_diag_bin_p(0.999) == 49);
    CHECK(global_site_diag_bin_p(1.0) == 49);
    CHECK(global_site_diag_bin_p(-0.5) == 0);
    CHECK(global_site_diag_bin_p(2.0) == 49);
    CHECK(global_site_diag_bin_d(-1.0) == 0);
    CHECK(global_site_diag_bin_d(-0.97) == 0);
    CHECK(global_site_diag_bin_d(-0.96) == 1);
    CHECK(global_site_diag_bin_d(0.0) == 25);
    CHECK(global_site_diag_bin_d(0.999) == 49);
    CHECK(global_site_diag_bin_d(1.0) == 49);
    CHECK(global_site_diag_bin_d(-3.0) == 0);
    CHECK(global_site_diag_bin_d(3.0) == 49);

    /* histogram */
    GlobalSiteDiag h, g;
    global_site_diag_clear(&h);
    global_site_diag_clear(&g);
    global_site_diag_add(&h, 0.31, -0.2, 0);
    global_site_diag_add(&h, 0.31, -0.2, 1);
    global_site_diag_add(&h, 1.0, 1.0, 1);
    CHECK(h.attempts[0][15] == 2ULL && h.accepted[0][15] == 1ULL);
    CHECK(h.attempts[1][20] == 2ULL && h.accepted[1][20] == 1ULL);
    CHECK(h.attempts[0][49] == 1ULL && h.accepted[0][49] == 1ULL);
    CHECK(h.attempts[1][49] == 1ULL && h.accepted[1][49] == 1ULL);
    unsigned long long total = 0ULL;
    for (int k = 0; k < GLOBAL_SITE_DIAG_NBIN; k++) {
        total += h.attempts[0][k];
    }
    CHECK(total == 3ULL);
    global_site_diag_add(&g, 0.31, -0.2, 0);
    global_site_diag_merge(&g, &h);
    CHECK(g.attempts[0][15] == 3ULL && g.accepted[0][15] == 1ULL);
    CHECK(g.attempts[0][49] == 1ULL);

    /* Stage B (spec 3.1): weight scale, weights, cumulative sums, selection */
    CHECK_CLOSE(global_site_weight_scale(0.31887), tanh(0.31887), 1e-15);
    CHECK_CLOSE(global_site_weight_scale(0.0), 1.0, 0.0);
    CHECK_CLOSE(global_site_weight_scale(-1.0), 1.0, 0.0);   /* never negative or zero */
    {
        const double pw[4] = {0.0, 0.25, 0.5, 1.0};
        double w[4], cum[4];
        /* alpha = 2, p0 = 0.5, n = 4: w = (0, 0.25, 1, 4) + 0.25 */
        CHECK(global_site_weights_p(pw, 4, 0.5, 2.0, w, cum) == 0);
        CHECK_CLOSE(w[0], 0.25, 1e-15);
        CHECK_CLOSE(w[1], 0.5, 1e-15);
        CHECK_CLOSE(w[2], 1.25, 1e-15);
        CHECK_CLOSE(w[3], 4.25, 1e-15);
        CHECK_CLOSE(cum[0], 0.25, 1e-15);
        CHECK_CLOSE(cum[1], 0.75, 1e-15);
        CHECK_CLOSE(cum[2], 2.0, 1e-15);
        CHECK_CLOSE(cum[3], 6.25, 1e-15);
        /* alpha = 0: uniform 1 + 1/n, including p = 0 */
        CHECK(global_site_weights_p(pw, 4, 0.5, 0.0, w, cum) == 0);
        for (int i = 0; i < 4; i++) {
            CHECK_CLOSE(w[i], 1.25, 1e-15);
        }
        /* selection: u = 0 -> first site; u just below 1 -> last site. On an exact
           boundary (u*C_n == C_i) the rule C_i >= u*C_n picks the lower index; the
           exact cases use a cumulative array whose ratios are representable. */
        CHECK(global_site_weights_p(pw, 4, 0.5, 2.0, w, cum) == 0);
        CHECK(global_site_select_index(cum, 4, 0.0) == 0);
        CHECK(global_site_select_index(cum, 4, 0.25 / 6.25 - 1e-12) == 0);
        CHECK(global_site_select_index(cum, 4, 0.25 / 6.25 + 1e-12) == 1);
        CHECK(global_site_select_index(cum, 4, 0.75 / 6.25 + 1e-12) == 2);
        CHECK(global_site_select_index(cum, 4, 2.0 / 6.25 + 1e-12) == 3);
        CHECK(global_site_select_index(cum, 4, 1.0 - 1e-16) == 3);
        CHECK(global_site_select_index(cum, 1, 0.999) == 0);
        const double exact[4] = {1.0, 2.0, 4.0, 8.0};   /* u = 1/8, 2/8, 4/8 are exact */
        CHECK(global_site_select_index(exact, 4, 0.125) == 0);   /* boundary -> lower index */
        CHECK(global_site_select_index(exact, 4, 0.25) == 1);
        CHECK(global_site_select_index(exact, 4, 0.5) == 2);
        CHECK(global_site_select_index(exact, 4, 0.5 + 1e-12) == 3);
        /* invariance (spec 3.1): |m_i| does not change when site i is flipped,
           so the weight vector built from p is the same after any flip */
        const int m1[4] = {8, -6, 0, 2};      /* distinct names: m/bip exist at function scope */
        const int m1flip[4] = {8, 6, 0, -2};
        const int bip1[4] = {1, -1, 1, -1};
        double pa[4], da[4], pb[4], db[4], wa[4], ca[4], wb[4], cb[4];
        CHECK(global_site_indicators(m1, bip1, 4, 8, pa, da) == 0);
        CHECK(global_site_indicators(m1flip, bip1, 4, 8, pb, db) == 0);
        CHECK(global_site_weights_p(pa, 4, 0.3, 2.0, wa, ca) == 0);
        CHECK(global_site_weights_p(pb, 4, 0.3, 2.0, wb, cb) == 0);
        CHECK(memcmp(wa, wb, sizeof wa) == 0 && memcmp(ca, cb, sizeof ca) == 0);
        /* bad arguments and overflow are errors, never clamped */
        CHECK(global_site_weights_p(NULL, 4, 0.5, 2.0, w, cum) == 1);
        CHECK(global_site_weights_p(pw, 0, 0.5, 2.0, w, cum) == 1);
        CHECK(global_site_weights_p(pw, 4, 0.0, 2.0, w, cum) == 1);
        CHECK(global_site_weights_p(pw, 4, 0.5, -1.0, w, cum) == 1);
        CHECK(global_site_weights_p(pw, 4, 0.5, NAN, w, cum) == 1);
        CHECK(global_site_weights_p(pw, 4, 1e-300, 4.0, w, cum) == 1);   /* inf */
        /* finite but unusable: 2^60 + 0.5 == 2^60 erases the floor of site 1 */
        const double ptwo[2] = {1.0, 0.0};
        CHECK(global_site_weights_p(ptwo, 2, 0.5, 60.0, w, cum) == 1);
        CHECK(global_site_weights_p(ptwo, 2, 0.5, 52.0, w, cum) == 1);   /* w1/W = 1.1e-16 < 2^-52 */
        CHECK(global_site_weights_p(ptwo, 2, 0.5, 40.0, w, cum) == 0);   /* w1/W = 4.5e-13: usable */
        CHECK_CLOSE(cum[1] - cum[0], 0.5, 1e-15);
        check_every_site_reachable(cum, 2);
        /* Positive increments and w/W >= 2^-52 are insufficient: the largest
           RNG value's rounded target equals cum[0], so site 1 is unreachable. */
        const double edge[2] = {1.0, 0.48};
        CHECK(global_site_weights_p(edge, 2, 0.5, 51.0, w, cum) == 1);
        CHECK(global_site_select_index(cum, 2, 1.0 - 0x1p-53) == 0);
        /* Both orders, including the narrow interval at the lower endpoint. */
        const double edge_reverse[2] = {0.48, 1.0};
        CHECK(global_site_weights_p(edge_reverse, 2, 0.5, 51.0, w, cum) == 0);
        check_every_site_reachable(cum, 2);
        CHECK(global_site_select_index(cum, 2, 0.0) == 0);
        CHECK(global_site_select_index(cum, 2, 1.0 - 0x1p-53) == 1);
        /* All 24 orders: any successful weight vector must have a concrete
           RNG-grid witness for every site, including both endpoint sites. */
        const double perm_p[4] = {1.0, 0.48, 0.0, 0.25};
        const double powers[5] = {0.0, 2.0, 40.0, 51.0, 60.0};
        for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++)
        for (int c = 0; c < 4; c++) for (int e = 0; e < 4; e++) {
            if (a == b || a == c || a == e || b == c || b == e || c == e) continue;
            const double pp[4] = {perm_p[a], perm_p[b], perm_p[c], perm_p[e]};
            for (int k = 0; k < 5; k++) {
                const int rc = global_site_weights_p(pp, 4, 0.5, powers[k], w, cum);
                if (k < 2) CHECK(rc == 0);
                if (rc == 0) {
                    check_every_site_reachable(cum, 4);
                    CHECK(global_site_select_index(cum, 4, 0.0) == 0);
                    CHECK(global_site_select_index(cum, 4, 1.0 - 0x1p-53) == 3);
                }
            }
        }
        CHECK(global_site_select_index(NULL, 4, 0.5) == -1);
        CHECK(global_site_select_index(cum, 0, 0.5) == -1);
        const double zero[2] = {0.0, 0.0};
        CHECK(global_site_select_index(zero, 2, 0.5) == -1);
    }
    TEST_END();
}
