#include "test_util.h"
#include "global_select.h"

#include <string.h>

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
    TEST_END();
}
