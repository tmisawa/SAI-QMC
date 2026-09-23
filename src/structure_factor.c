#include "structure_factor.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checked_product_size(size_t a, size_t b, size_t *out)
{
    if (out == NULL || (a != 0 && b > SIZE_MAX / a)) {
        return 1;
    }
    *out = a * b;
    return 0;
}

static int selector_has_space(const char *selector)
{
    for (const unsigned char *p = (const unsigned char *)selector; *p; p++) {
        if (isspace(*p)) {
            return 1;
        }
    }
    return 0;
}

static int parse_index(const char *begin, const char **end_out, int *out)
{
    char *end = NULL;
    errno = 0;
    const long value = strtol(begin, &end, 10);
    if (errno != 0 || end == begin || value < 0 || value > INT_MAX) {
        return 1;
    }
    *end_out = end;
    *out = (int)value;
    return 0;
}

static int parse_explicit_selector(StructureFactorPlan *plan, const Lattice *L,
                                   const char *selector)
{
    int nq = 1;
    for (const char *p = selector; *p; p++) {
        if (*p == ',') {
            if (nq == INT_MAX) {
                return 1;
            }
            nq++;
        }
    }

    size_t bytes = 0;
    if (checked_product_size((size_t)nq, sizeof(int), &bytes)) {
        return 1;
    }
    plan->mx = malloc(bytes);
    plan->my = malloc(bytes);
    if (plan->mx == NULL || plan->my == NULL) {
        return 1;
    }

    const char *p = selector;
    for (int q = 0; q < nq; q++) {
        const char *end = NULL;
        if (parse_index(p, &end, &plan->mx[q]) || *end != ':') {
            return 1;
        }
        p = end + 1;
        if (parse_index(p, &end, &plan->my[q])) {
            return 1;
        }
        if ((q + 1 < nq && *end != ',') || (q + 1 == nq && *end != '\0')) {
            return 1;
        }
        if (plan->mx[q] >= L->Lx || plan->my[q] >= L->Ly ||
            (L->type == LAT_CHAIN && plan->my[q] != 0)) {
            return 1;
        }
        for (int prior = 0; prior < q; prior++) {
            if (plan->mx[prior] == plan->mx[q] &&
                plan->my[prior] == plan->my[q]) {
                return 1;
            }
        }
        p = end + (q + 1 < nq ? 1 : 0);
    }
    plan->nq = nq;
    return 0;
}

void structure_factor_plan_free(StructureFactorPlan *plan)
{
    if (plan == NULL) {
        return;
    }
    free(plan->mx);
    free(plan->my);
    free(plan->phase_cos);
    memset(plan, 0, sizeof(*plan));
}

int structure_factor_plan_init(StructureFactorPlan *plan, const Lattice *L,
                               const char *selector)
{
    if (plan == NULL) {
        return 1;
    }
    memset(plan, 0, sizeof(*plan));
    if (L == NULL || selector == NULL || selector[0] == '\0' ||
        selector_has_space(selector)) {
        return 1;
    }
    if (strcmp(selector, "none") == 0) {
        return 0;
    }
    if (!L->has_coordinates ||
        (L->type != LAT_CHAIN && L->type != LAT_SQUARE) || L->n <= 0 ||
        L->Lx <= 0 || L->Ly <= 0 ||
        (size_t)L->Lx * (size_t)L->Ly != (size_t)L->n) {
        return 1;
    }

    plan->enabled = 1;
    plan->n = L->n;
    plan->Lx = L->Lx;
    plan->Ly = L->Ly;

    if (strcmp(selector, "af") == 0) {
        if ((L->Lx > 1 && L->Lx % 2 != 0) ||
            (L->Ly > 1 && L->Ly % 2 != 0)) {
            goto fail;
        }
        plan->nq = 1;
        plan->mx = malloc(sizeof(int));
        plan->my = malloc(sizeof(int));
        if (plan->mx == NULL || plan->my == NULL) {
            goto fail;
        }
        plan->mx[0] = L->Lx > 1 ? L->Lx / 2 : 0;
        plan->my[0] = L->Ly > 1 ? L->Ly / 2 : 0;
    } else if (strcmp(selector, "all") == 0) {
        plan->is_all = 1;
        plan->nq = L->n;
        size_t index_bytes = 0;
        if (checked_product_size((size_t)plan->nq, sizeof(int),
                                 &index_bytes)) {
            goto fail;
        }
        plan->mx = malloc(index_bytes);
        plan->my = malloc(index_bytes);
        if (plan->mx == NULL || plan->my == NULL) {
            goto fail;
        }
        int q = 0;
        for (int my = 0; my < L->Ly; my++) {
            for (int mx = 0; mx < L->Lx; mx++) {
                plan->mx[q] = mx;
                plan->my[q] = my;
                q++;
            }
        }
    } else if (parse_explicit_selector(plan, L, selector) != 0) {
        goto fail;
    }

    size_t phase_count = 0;
    size_t phase_bytes = 0;
    if (plan->nq <= 0 ||
        checked_product_size((size_t)plan->n, (size_t)plan->nq,
                             &phase_count) ||
        checked_product_size(phase_count, sizeof(double), &phase_bytes)) {
        goto fail;
    }
    plan->phase_cos = malloc(phase_bytes);
    if (plan->phase_cos == NULL) {
        goto fail;
    }

    const double two_pi = 2.0 * acos(-1.0);
    for (int q = 0; q < plan->nq; q++) {
        for (int disp = 0; disp < plan->n; disp++) {
            const int dx = disp % plan->Lx;
            const int dy = disp / plan->Lx;
            const double angle =
                two_pi * ((double)plan->mx[q] * (double)dx /
                              (double)plan->Lx +
                          (double)plan->my[q] * (double)dy /
                              (double)plan->Ly);
            const double phase = cos(angle);
            if (!isfinite(phase)) {
                goto fail;
            }
            plan->phase_cos[disp + q * plan->n] = phase;
        }
    }
    return 0;

fail:
    structure_factor_plan_free(plan);
    return 1;
}

void szz_plan_free(SzzPlan *plan)
{
    structure_factor_plan_free(plan);
}

int szz_plan_init(SzzPlan *plan, const Lattice *L, const char *selector)
{
    return structure_factor_plan_init(plan, L, selector);
}

int structure_factor_workspace_init(StructureFactorWorkspace *work,
                                    const StructureFactorPlan *plan)
{
    if (work == NULL || plan == NULL) {
        return 1;
    }
    memset(work, 0, sizeof(*work));
    if (!plan->enabled) {
        return 0;
    }
    if (plan->n <= 0) {
        return 1;
    }
    work->corr_disp = calloc((size_t)plan->n, sizeof(double));
    if (work->corr_disp == NULL) {
        return 1;
    }
    work->n = plan->n;
    return 0;
}

void structure_factor_workspace_free(StructureFactorWorkspace *work)
{
    if (work == NULL) {
        return;
    }
    free(work->corr_disp);
    memset(work, 0, sizeof(*work));
}

int szz_workspace_init(SzzWorkspace *work, const SzzPlan *plan)
{
    return structure_factor_workspace_init(work, plan);
}

void szz_workspace_free(SzzWorkspace *work)
{
    structure_factor_workspace_free(work);
}

int spin_pair_correlations(int n, const double *g_up, const double *g_dn,
                           int i, int j, double *czz, double *cperp)
{
    if (n <= 0 || g_up == NULL || g_dn == NULL || i < 0 || i >= n ||
        j < 0 || j >= n || (czz == NULL && cperp == NULL)) {
        return 1;
    }
    const double gu_ii = g_up[i + i * n];
    const double gd_ii = g_dn[i + i * n];
    const double gu_jj = g_up[j + j * n];
    const double gd_jj = g_dn[j + j * n];
    const double gu_ij = g_up[i + j * n];
    const double gu_ji = g_up[j + i * n];
    const double gd_ij = g_dn[i + j * n];
    const double gd_ji = g_dn[j + i * n];
    if (!isfinite(gu_ii) || !isfinite(gd_ii) || !isfinite(gu_jj) ||
        !isfinite(gd_jj) || !isfinite(gu_ij) || !isfinite(gu_ji) ||
        !isfinite(gd_ij) || !isfinite(gd_ji)) {
        return 1;
    }
    const double delta = i == j ? 1.0 : 0.0;
    if (czz != NULL) {
        const double spin_i = gd_ii - gu_ii;
        const double spin_j = gd_jj - gu_jj;
        *czz = 0.25 * (spin_i * spin_j +
                       (delta - gu_ji) * gu_ij +
                       (delta - gd_ji) * gd_ij);
        if (!isfinite(*czz)) {
            return 1;
        }
    }
    if (cperp != NULL) {
        *cperp = 0.5 * ((delta - gu_ji) * gd_ij +
                        (delta - gd_ji) * gu_ij);
        if (!isfinite(*cperp)) {
            return 1;
        }
    }
    return 0;
}

static int channel_is_valid(const StructureFactorPlan *plan,
                            const StructureFactorWorkspace *work,
                            const double *out)
{
    if (plan == NULL || !plan->enabled) {
        return 1;
    }
    return work != NULL && out != NULL && plan->n > 0 && plan->nq > 0 &&
           work->n == plan->n && work->corr_disp != NULL &&
           plan->phase_cos != NULL;
}

static int finish_fourier(const StructureFactorPlan *plan,
                          const StructureFactorWorkspace *work, double *out)
{
    for (int q = 0; q < plan->nq; q++) {
        double sum = 0.0;
        for (int disp = 0; disp < plan->n; disp++) {
            sum += plan->phase_cos[disp + q * plan->n] *
                   work->corr_disp[disp];
        }
        out[q] = sum / (double)plan->n;
        if (!isfinite(out[q])) {
            return 1;
        }
    }
    return 0;
}

int measure_szz_sperp_sample(
    const StructureFactorPlan *szz_plan, StructureFactorWorkspace *szz_work,
    double *szz_out, const StructureFactorPlan *sperp_plan,
    StructureFactorWorkspace *sperp_work, double *sperp_out,
    const double *g_up, const double *g_dn)
{
    const int do_szz = szz_plan != NULL && szz_plan->enabled;
    const int do_sperp = sperp_plan != NULL && sperp_plan->enabled;
    if (!do_szz && !do_sperp) {
        return 0;
    }
    if ((do_szz && !channel_is_valid(szz_plan, szz_work, szz_out)) ||
        (do_sperp && !channel_is_valid(sperp_plan, sperp_work, sperp_out)) ||
        g_up == NULL || g_dn == NULL) {
        return 1;
    }
    const StructureFactorPlan *geometry = do_szz ? szz_plan : sperp_plan;
    if (do_szz && do_sperp &&
        (szz_plan->n != sperp_plan->n || szz_plan->Lx != sperp_plan->Lx ||
         szz_plan->Ly != sperp_plan->Ly)) {
        return 1;
    }

    const int n = geometry->n;
    if (do_szz) {
        memset(szz_work->corr_disp, 0, (size_t)n * sizeof(double));
    }
    if (do_sperp) {
        memset(sperp_work->corr_disp, 0, (size_t)n * sizeof(double));
    }
    for (int i = 0; i < n; i++) {
        const int xi = i % geometry->Lx;
        const int yi = i / geometry->Lx;
        for (int j = 0; j < n; j++) {
            double czz = 0.0;
            double cperp = 0.0;
            if (spin_pair_correlations(n, g_up, g_dn, i, j,
                                       do_szz ? &czz : NULL,
                                       do_sperp ? &cperp : NULL) != 0) {
                return 1;
            }

            const int xj = j % geometry->Lx;
            const int yj = j / geometry->Lx;
            int dx = xi - xj;
            int dy = yi - yj;
            if (dx < 0) {
                dx += geometry->Lx;
            }
            if (dy < 0) {
                dy += geometry->Ly;
            }
            const int disp = dx + dy * geometry->Lx;
            if (do_szz) {
                szz_work->corr_disp[disp] += czz;
            }
            if (do_sperp) {
                sperp_work->corr_disp[disp] += cperp;
            }
            if ((do_szz && !isfinite(szz_work->corr_disp[disp])) ||
                (do_sperp && !isfinite(sperp_work->corr_disp[disp]))) {
                return 1;
            }
        }
    }

    if ((do_szz && finish_fourier(szz_plan, szz_work, szz_out) != 0) ||
        (do_sperp &&
         finish_fourier(sperp_plan, sperp_work, sperp_out) != 0)) {
        return 1;
    }
    return 0;
}

int measure_szz_sample(const SzzPlan *plan, SzzWorkspace *work,
                       const double *g_up, const double *g_dn, double *out)
{
    if (plan == NULL || work == NULL) {
        return 1;
    }
    return measure_szz_sperp_sample(plan, work, out, NULL, NULL, NULL, g_up,
                                    g_dn);
}

int measure_sperp_sample(const StructureFactorPlan *plan,
                         StructureFactorWorkspace *work,
                         const double *g_up, const double *g_dn, double *out)
{
    if (plan == NULL || work == NULL) {
        return 1;
    }
    return measure_szz_sperp_sample(NULL, NULL, NULL, plan, work, out, g_up,
                                    g_dn);
}

int szz_bin_ratio(const double *num, int nq, double sum_sign, double *out)
{
    if (num == NULL || out == NULL || nq <= 0 || !isfinite(sum_sign) ||
        sum_sign == 0.0) {
        return 1;
    }
    for (int q = 0; q < nq; q++) {
        if (!isfinite(num[q])) {
            return 1;
        }
        const double value = num[q] / sum_sign;
        if (!isfinite(value)) {
            return 1;
        }
        out[q] = value;
    }
    return 0;
}

int sperp_bin_ratio(const double *num, int nq, double sum_sign, double *out)
{
    return szz_bin_ratio(num, nq, sum_sign, out);
}

static int sum_rule_check(const StructureFactorPlan *plan,
                          const double *values, double ntot, double doublon,
                          double prefactor,
                          StructureFactorSumRuleDiagnostics *diagnostics)
{
    if (diagnostics == NULL) {
        return 1;
    }
    diagnostics->lhs = NAN;
    diagnostics->rhs = NAN;
    diagnostics->difference = NAN;
    diagnostics->tolerance = NAN;

    if (plan == NULL) {
        return 1;
    }
    if (!plan->is_all) {
        return 0;
    }
    if (!plan->enabled || values == NULL || plan->n <= 0 ||
        plan->nq != plan->n || !isfinite(ntot) || !isfinite(doublon) ||
        !isfinite(prefactor)) {
        return 1;
    }

    const double rhs =
        prefactor * (ntot - 2.0 * (double)plan->n * doublon);
    const double tolerance = 1e-12 + 1e-10 * fmax(1.0, fabs(rhs));
    diagnostics->rhs = rhs;
    diagnostics->tolerance = tolerance;
    if (!isfinite(rhs) || !isfinite(tolerance)) {
        return 1;
    }

    double lhs = 0.0;
    for (int q = 0; q < plan->nq; q++) {
        if (!isfinite(values[q])) {
            return 1;
        }
        lhs += values[q];
        if (!isfinite(lhs)) {
            return 1;
        }
    }
    diagnostics->lhs = lhs;
    diagnostics->difference = lhs - rhs;
    return !isfinite(diagnostics->difference) ||
                   fabs(diagnostics->difference) > tolerance
               ? 1
               : 0;
}

int szz_sum_rule_check(const SzzPlan *plan, const double *szz, double ntot,
                       double doublon, SzzSumRuleDiagnostics *diagnostics)
{
    return sum_rule_check(plan, szz, ntot, doublon, 0.25, diagnostics);
}

int sperp_sum_rule_check(
    const StructureFactorPlan *plan, const double *sperp, double ntot,
    double doublon, StructureFactorSumRuleDiagnostics *diagnostics)
{
    return sum_rule_check(plan, sperp, ntot, doublon, 0.5, diagnostics);
}
