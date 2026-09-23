#include "test_util.h"
#include "lattice.h"
#include "structure_factor.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static double direct_czz(const double *gu, const double *gd, int n, int i,
                         int j)
{
    const double su = (1.0 - gu[i + i * n]) - (1.0 - gd[i + i * n]);
    const double sv = (1.0 - gu[j + j * n]) - (1.0 - gd[j + j * n]);
    const double delta = i == j ? 1.0 : 0.0;
    return 0.25 * (su * sv +
                   (delta - gu[j + i * n]) * gu[i + j * n] +
                   (delta - gd[j + i * n]) * gd[i + j * n]);
}

static double direct_cperp(const double *gu, const double *gd, int n, int i,
                           int j)
{
    const double delta = i == j ? 1.0 : 0.0;
    return 0.5 * ((delta - gu[j + i * n]) * gd[i + j * n] +
                  (delta - gd[j + i * n]) * gu[i + j * n]);
}

static void direct_szz(const SzzPlan *plan, const double *gu,
                       const double *gd, double *out, double *imag)
{
    const double two_pi = 2.0 * acos(-1.0);
    for (int q = 0; q < plan->nq; q++) {
        out[q] = 0.0;
        imag[q] = 0.0;
        for (int i = 0; i < plan->n; i++) {
            const int xi = i % plan->Lx;
            const int yi = i / plan->Lx;
            for (int j = 0; j < plan->n; j++) {
                const int xj = j % plan->Lx;
                const int yj = j / plan->Lx;
                const double angle =
                    two_pi * ((double)plan->mx[q] * (double)(xi - xj) /
                                  (double)plan->Lx +
                              (double)plan->my[q] * (double)(yi - yj) /
                                  (double)plan->Ly);
                const double czz = direct_czz(gu, gd, plan->n, i, j);
                out[q] += cos(angle) * czz / (double)plan->n;
                imag[q] -= sin(angle) * czz / (double)plan->n;
            }
        }
    }
}

static void direct_sperp(const SzzPlan *plan, const double *gu,
                         const double *gd, double *out, double *imag)
{
    const double two_pi = 2.0 * acos(-1.0);
    for (int q = 0; q < plan->nq; q++) {
        out[q] = 0.0;
        imag[q] = 0.0;
        for (int i = 0; i < plan->n; i++) {
            const int xi = i % plan->Lx;
            const int yi = i / plan->Lx;
            for (int j = 0; j < plan->n; j++) {
                const int xj = j % plan->Lx;
                const int yj = j / plan->Lx;
                const double angle =
                    two_pi * ((double)plan->mx[q] * (double)(xi - xj) /
                                  (double)plan->Lx +
                              (double)plan->my[q] * (double)(yi - yj) /
                                  (double)plan->Ly);
                const double cperp = direct_cperp(gu, gd, plan->n, i, j);
                out[q] += cos(angle) * cperp / (double)plan->n;
                imag[q] -= sin(angle) * cperp / (double)plan->n;
            }
        }
    }
}

static void set_diagonal_state(double *gu, double *gd, int n,
                               const int *nup, const int *ndn)
{
    memset(gu, 0, (size_t)n * (size_t)n * sizeof(double));
    memset(gd, 0, (size_t)n * (size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) {
        gu[i + i * n] = 1.0 - (double)nup[i];
        gd[i + i * n] = 1.0 - (double)ndn[i];
    }
}

int main(void)
{
    Lattice chain;
    lattice_chain(&chain, 4, -1.0, 1);

    SzzPlan disabled;
    CHECK(szz_plan_init(&disabled, &chain, "none") == 0);
    CHECK(disabled.enabled == 0);
    CHECK(disabled.nq == 0);
    CHECK(disabled.phase_cos == NULL);
    szz_plan_free(&disabled);

    SzzPlan af;
    CHECK(szz_plan_init(&af, &chain, "af") == 0);
    CHECK(af.enabled == 1);
    CHECK(af.nq == 1);
    CHECK(af.mx[0] == 2);
    CHECK(af.my[0] == 0);
    for (int dx = 0; dx < 4; dx++) {
        CHECK_CLOSE(af.phase_cos[dx], dx % 2 == 0 ? 1.0 : -1.0, 1e-12);
    }

    SzzPlan all;
    CHECK(szz_plan_init(&all, &chain, "all") == 0);
    CHECK(all.is_all == 1);
    CHECK(all.nq == 4);
    for (int q = 0; q < 4; q++) {
        CHECK(all.mx[q] == q);
        CHECK(all.my[q] == 0);
        CHECK_CLOSE(all.phase_cos[0 + q * 4], 1.0, 1e-12);
    }

    SzzPlan selected;
    CHECK(szz_plan_init(&selected, &chain, "3:0,0:0,2:0") == 0);
    CHECK(selected.nq == 3);
    CHECK(selected.mx[0] == 3);
    CHECK(selected.mx[1] == 0);
    CHECK(selected.mx[2] == 2);
    szz_plan_free(&selected);

    const char *bad_chain[] = {"",       "foo",   "1",     "1:",
                               ":0",     "-1:0",  "4:0",   "1:1",
                               "0:0,",   "0:0,0:0", "0:0 x", "0:0junk"};
    for (size_t k = 0; k < sizeof(bad_chain) / sizeof(bad_chain[0]); k++) {
        SzzPlan bad;
        CHECK(szz_plan_init(&bad, &chain, bad_chain[k]) != 0);
        szz_plan_free(&bad);
    }

    Lattice odd;
    lattice_chain(&odd, 3, -1.0, 1);
    SzzPlan bad_af;
    CHECK(szz_plan_init(&bad_af, &odd, "af") != 0);
    szz_plan_free(&bad_af);
    lattice_free(&odd);

    Lattice one;
    lattice_square(&one, 1, 4, -1.0, 1);
    SzzPlan one_af;
    CHECK(szz_plan_init(&one_af, &one, "af") == 0);
    CHECK(one_af.mx[0] == 0);
    CHECK(one_af.my[0] == 2);
    szz_plan_free(&one_af);
    lattice_free(&one);

    Lattice square;
    lattice_square(&square, 2, 2, -1.0, 0);
    SzzPlan square_all;
    CHECK(szz_plan_init(&square_all, &square, "all") == 0);
    CHECK(square_all.nq == 4);
    CHECK(square_all.mx[0] == 0 && square_all.my[0] == 0);
    CHECK(square_all.mx[1] == 1 && square_all.my[1] == 0);
    CHECK(square_all.mx[2] == 0 && square_all.my[2] == 1);
    CHECK(square_all.mx[3] == 1 && square_all.my[3] == 1);

    Lattice file_lattice = {0};
    file_lattice.n = 4;
    file_lattice.type = LAT_FILE;
    SzzPlan file_plan;
    CHECK(szz_plan_init(&file_plan, &file_lattice, "all") != 0);
    szz_plan_free(&file_plan);

    const int n = 4;
    double gu[16] = {0};
    double gd[16] = {0};
    double got[4] = {0};
    double sperp_got[4] = {0};
    SzzWorkspace work;
    SzzWorkspace sperp_work;
    CHECK(szz_workspace_init(&work, &square_all) == 0);
    CHECK(szz_workspace_init(&sperp_work, &square_all) == 0);

    for (int i = 0; i < n; i++) {
        gu[i + i * n] = 0.5;
        gd[i + i * n] = 0.5;
    }
    CHECK(measure_szz_sample(&square_all, &work, gu, gd, got) == 0);
    for (int q = 0; q < 4; q++) {
        CHECK_CLOSE(got[q], 0.125, 1e-12);
    }
    CHECK(measure_sperp_sample(&square_all, &sperp_work, gu, gd,
                               sperp_got) == 0);
    for (int q = 0; q < 4; q++) {
        CHECK_CLOSE(sperp_got[q], 0.25, 1e-12);
        /* This SU(2) wiring check cannot detect an up/down pairing swap. */
        CHECK_CLOSE(sperp_got[q], 2.0 * got[q], 1e-12);
    }

    const int up_all[4] = {1, 1, 1, 1};
    const int down_none[4] = {0, 0, 0, 0};
    set_diagonal_state(gu, gd, n, up_all, down_none);
    CHECK(measure_szz_sample(&square_all, &work, gu, gd, got) == 0);
    CHECK_CLOSE(got[0], 1.0, 1e-12);
    for (int q = 1; q < 4; q++) {
        CHECK_CLOSE(got[q], 0.0, 1e-12);
    }
    CHECK(measure_sperp_sample(&square_all, &sperp_work, gu, gd,
                               sperp_got) == 0);
    for (int q = 0; q < 4; q++) {
        CHECK_CLOSE(sperp_got[q], 0.5, 1e-12);
    }

    const int neel_up[4] = {1, 0, 0, 1};
    const int neel_dn[4] = {0, 1, 1, 0};
    set_diagonal_state(gu, gd, n, neel_up, neel_dn);
    CHECK(measure_szz_sample(&square_all, &work, gu, gd, got) == 0);
    CHECK_CLOSE(got[0], 0.0, 1e-12);
    CHECK_CLOSE(got[3], 1.0, 1e-12);
    CHECK(measure_sperp_sample(&square_all, &sperp_work, gu, gd,
                               sperp_got) == 0);
    for (int q = 0; q < 4; q++) {
        CHECK_CLOSE(sperp_got[q], 0.5, 1e-12);
    }

    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            gu[i + j * n] = (i == j ? 0.41 + 0.03 * i
                                     : 0.01 * (1 + i + 2 * j));
            gd[i + j * n] = (i == j ? 0.57 - 0.02 * i
                                     : -0.008 * (2 + 2 * i + j));
        }
    }
    double want[4] = {0};
    double imag[4] = {0};
    double sperp_want[4] = {0};
    double sperp_imag[4] = {0};
    /*
     * Primary bug-detection oracle: asymmetric dense Green matrices make
     * spin pairing, delta, coefficient, and G_ij/G_ji mistakes observable.
     */
    direct_szz(&square_all, gu, gd, want, imag);
    direct_sperp(&square_all, gu, gd, sperp_want, sperp_imag);
    CHECK(measure_szz_sample(&square_all, &work, gu, gd, got) == 0);
    CHECK(measure_szz_sperp_sample(&square_all, &work, got, &square_all,
                                   &sperp_work, sperp_got, gu, gd) == 0);
    double onsite = 0.0;
    double sperp_onsite = 0.0;
    for (int i = 0; i < n; i++) {
        onsite += direct_czz(gu, gd, n, i, i);
        sperp_onsite += direct_cperp(gu, gd, n, i, i);
        for (int j = 0; j < n; j++) {
            double pair_czz = 0.0;
            double pair_cperp = 0.0;
            CHECK(spin_pair_correlations(n, gu, gd, i, j, &pair_czz,
                                         &pair_cperp) == 0);
            CHECK_CLOSE(pair_czz, direct_czz(gu, gd, n, i, j), 1e-15);
            CHECK_CLOSE(pair_cperp, direct_cperp(gu, gd, n, i, j), 1e-15);
            CHECK_CLOSE(direct_czz(gu, gd, n, i, j),
                        direct_czz(gu, gd, n, j, i), 1e-14);
            CHECK_CLOSE(direct_cperp(gu, gd, n, i, j),
                        direct_cperp(gu, gd, n, j, i), 1e-14);
        }
    }
    double sum_q = 0.0;
    double sperp_sum_q = 0.0;
    for (int q = 0; q < 4; q++) {
        CHECK_CLOSE(got[q], want[q], 1e-13);
        CHECK_CLOSE(imag[q], 0.0, 1e-13);
        CHECK_CLOSE(sperp_got[q], sperp_want[q], 1e-13);
        CHECK_CLOSE(sperp_imag[q], 0.0, 1e-13);
        sum_q += got[q];
        sperp_sum_q += sperp_got[q];
    }
    CHECK_CLOSE(sum_q, onsite, 1e-12);
    CHECK_CLOSE(sperp_sum_q, sperp_onsite, 1e-12);
    CHECK_CLOSE(sperp_onsite, 2.0 * onsite, 1e-12);

    SzzSumRuleDiagnostics sum_rule;
    const double sum_rule_doublon = (4.0 - 4.0 * onsite) / (2.0 * n);
    CHECK(szz_sum_rule_check(&square_all, got, 4.0, sum_rule_doublon,
                             &sum_rule) == 0);
    CHECK_CLOSE(sum_rule.lhs, onsite, 1e-12);
    CHECK_CLOSE(sum_rule.rhs, onsite, 1e-12);
    CHECK_CLOSE(sum_rule.difference, 0.0, 1e-12);
    CHECK(sum_rule.tolerance > 0.0);
    const double saved_q0 = got[0];
    got[0] += 1e-3;
    CHECK(szz_sum_rule_check(&square_all, got, 4.0, sum_rule_doublon,
                             &sum_rule) != 0);
    CHECK_CLOSE(sum_rule.difference, 1e-3, 1e-12);
    got[0] = saved_q0;
    CHECK(szz_sum_rule_check(&square_all, got, NAN, sum_rule_doublon,
                             &sum_rule) != 0);
    CHECK(szz_sum_rule_check(&square_all, got, 4.0, sum_rule_doublon,
                             NULL) != 0);

    SzzSumRuleDiagnostics sperp_sum_rule;
    CHECK(sperp_sum_rule_check(&square_all, sperp_got, 4.0,
                               sum_rule_doublon, &sperp_sum_rule) == 0);
    CHECK_CLOSE(sperp_sum_rule.lhs, sperp_onsite, 1e-12);
    CHECK_CLOSE(sperp_sum_rule.rhs, sperp_onsite, 1e-12);

    Lattice rectangular;
    lattice_square(&rectangular, 2, 3, -1.0, 0);
    SzzPlan rectangular_all;
    CHECK(szz_plan_init(&rectangular_all, &rectangular, "all") == 0);
    CHECK(rectangular_all.nq == 6);
    SzzWorkspace rectangular_work;
    CHECK(szz_workspace_init(&rectangular_work, &rectangular_all) == 0);
    double rectangular_gu[36] = {0};
    double rectangular_gd[36] = {0};
    double rectangular_got[6] = {0};
    double rectangular_want[6] = {0};
    double rectangular_imag[6] = {0};
    for (int j = 0; j < 6; j++) {
        for (int i = 0; i < 6; i++) {
            rectangular_gu[i + j * 6] =
                i == j ? 0.37 + 0.025 * i : 0.004 * (1 + 2 * i + 3 * j);
            rectangular_gd[i + j * 6] =
                i == j ? 0.62 - 0.018 * i : -0.003 * (2 + 3 * i + j);
        }
    }
    direct_szz(&rectangular_all, rectangular_gu, rectangular_gd,
               rectangular_want, rectangular_imag);
    CHECK(measure_szz_sample(&rectangular_all, &rectangular_work,
                             rectangular_gu, rectangular_gd,
                             rectangular_got) == 0);
    for (int q = 0; q < rectangular_all.nq; q++) {
        CHECK_CLOSE(rectangular_got[q], rectangular_want[q], 1e-13);
        CHECK_CLOSE(rectangular_imag[q], 0.0, 1e-13);
    }
    szz_workspace_free(&rectangular_work);
    szz_plan_free(&rectangular_all);
    lattice_free(&rectangular);

    SzzWorkspace chain_work;
    CHECK(szz_workspace_init(&chain_work, &all) == 0);
    double chain_got[4] = {0};
    double chain_want[4] = {0};
    double chain_imag[4] = {0};
    direct_szz(&all, gu, gd, chain_want, chain_imag);
    CHECK(measure_szz_sample(&all, &chain_work, gu, gd, chain_got) == 0);
    for (int q = 0; q < all.nq; q++) {
        CHECK_CLOSE(chain_got[q], chain_want[q], 1e-13);
        CHECK_CLOSE(chain_imag[q], 0.0, 1e-13);
    }
    CHECK_CLOSE(chain_got[1], chain_got[3], 1e-13);
    szz_workspace_free(&chain_work);

    double expected_last_dx = 0.0;
    for (int i = 0; i < n; i++) {
        const int xi = i % square_all.Lx;
        const int yi = i / square_all.Lx;
        for (int j = 0; j < n; j++) {
            const int xj = j % square_all.Lx;
            const int yj = j / square_all.Lx;
            int dx = xi - xj;
            int dy = yi - yj;
            if (dx < 0) dx += square_all.Lx;
            if (dy < 0) dy += square_all.Ly;
            if (dx == square_all.Lx - 1 && dy == 0) {
                expected_last_dx += direct_czz(gu, gd, n, i, j);
            }
        }
    }
    CHECK_CLOSE(work.corr_disp[square_all.Lx - 1], expected_last_dx, 1e-13);

    double ratio_num[2] = {2.0, -4.0};
    double ratio[2] = {0};
    CHECK(szz_bin_ratio(ratio_num, 2, -2.0, ratio) == 0);
    CHECK_CLOSE(ratio[0], -1.0, 1e-15);
    CHECK_CLOSE(ratio[1], 2.0, 1e-15);
    CHECK(szz_bin_ratio(ratio_num, 2, 0.0, ratio) != 0);
    CHECK(sperp_bin_ratio(ratio_num, 2, -2.0, ratio) == 0);
    CHECK_CLOSE(ratio[0], -1.0, 1e-15);
    CHECK_CLOSE(ratio[1], 2.0, 1e-15);

    StructureFactorWorkspace wrong_shape = sperp_work;
    wrong_shape.n = n - 1;
    CHECK(measure_sperp_sample(&square_all, &wrong_shape, gu, gd,
                               sperp_got) != 0);
    CHECK(spin_pair_correlations(n, gu, gd, -1, 0, NULL, &ratio[0]) != 0);
    CHECK(spin_pair_correlations(n, gu, gd, 0, 0, NULL, NULL) != 0);

    gu[0] = NAN;
    CHECK(measure_szz_sample(&square_all, &work, gu, gd, got) != 0);
    CHECK(measure_szz_sample(&square_all, &work, NULL, gd, got) != 0);
    CHECK(measure_sperp_sample(&square_all, &sperp_work, gu, gd,
                               sperp_got) != 0);

    szz_workspace_free(&work);
    szz_workspace_free(&sperp_work);
    CHECK(work.corr_disp == NULL && work.n == 0);
    szz_plan_free(&square_all);
    lattice_free(&square);
    szz_plan_free(&all);
    szz_plan_free(&af);
    lattice_free(&chain);

    TEST_END();
}
