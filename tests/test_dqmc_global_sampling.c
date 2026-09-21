#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "lattice.h"
#include "model.h"

#include <stdlib.h>
#include <string.h>

enum { NS = 2, LT = 4, NC = 256, NCHAIN = 4000 };

/* Upper tail of chi-square via the regularized gamma function Q(k/2, x/2)
   (series/continued fraction, Numerical-Recipes style). */
static double gamma_q(double a, double x)
{
    if (x <= 0.0) {
        return 1.0;
    }
    const double gln = lgamma(a);
    if (x < a + 1.0) {
        double ap = a, sum = 1.0 / a, del = sum;
        for (int n = 0; n < 500; n++) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (fabs(del) < fabs(sum) * 1e-14) {
                break;
            }
        }
        return 1.0 - sum * exp(-x + a * log(x) - gln);
    }
    double b = x + 1.0 - a, c = 1e300, d = 1.0 / b, h = d;
    for (int i = 1; i < 500; i++) {
        const double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        d = fabs(d) < 1e-300 ? 1e-300 : d;
        c = b + an / c;
        c = fabs(c) < 1e-300 ? 1e-300 : c;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (fabs(del - 1.0) < 1e-14) {
            break;
        }
    }
    return exp(-x + a * log(x) - gln) * h;
}

static double run(int cycles, uint64_t base_seed, const double *pi)
{
    static int count[NC];
    memset(count, 0, sizeof count);
    Lattice L;
    lattice_chain(&L, NS, -1.0, 0);
    for (int ch = 0; ch < NCHAIN; ch++) {
        Model m;
        model_init(&m, &L, 4.0, 0.1, 1, 0.0);
        Rng r;
        rng_seed(&r, base_seed + (uint64_t)ch);
        Field f;
        field_init(&f, NS, LT, 4.0, 0.1, &r);
        Dqmc D;
        CHECK(dqmc_init_modes(&D, &m, &f, &r, 4, NULL, DQMC_SWEEP_FORWARD,
                              GREEN_REBUILD_COMBINE) == 0);
        for (int k = 0; k < cycles; k++) {
            dqmc_sweep(&D);
            CHECK(dqmc_global_site_pass(&D) == 0);
        }
        int c = 0;
        for (int k = 0; k < NS * LT; k++) {
            c |= (f.s[k] > 0) << k;
        }
        count[c]++;   /* one independent sample per chain */
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
    }
    lattice_free(&L);
    double chi2 = 0.0, pooled_e = 0.0, pooled_o = 0.0;
    int cells = 0;
    for (int c = 0; c < NC; c++) {
        const double e = pi[c] * NCHAIN;
        if (e < 5.0) {
            pooled_e += e;
            pooled_o += count[c];
        } else {
            chi2 += (count[c] - e) * (count[c] - e) / e;
            cells++;
        }
    }
    if (pooled_e > 0.0) {
        chi2 += (pooled_o - pooled_e) * (pooled_o - pooled_e) / pooled_e;
        cells++;
    }
    return gamma_q(0.5 * (cells - 1), 0.5 * chi2);
}

int main(void)
{
    static double logw[NC], pi[NC];
    Lattice L;
    lattice_chain(&L, NS, -1.0, 0);
    Model m;
    model_init(&m, &L, 4.0, 0.1, 1, 0.0);
    Rng r;
    rng_seed(&r, 1);
    Field f;
    field_init(&f, NS, LT, 4.0, 0.1, &r);
    Dqmc D;
    CHECK(dqmc_init_modes(&D, &m, &f, &r, 4, NULL, DQMC_SWEEP_FORWARD,
                          GREEN_REBUILD_COMBINE) == 0);
    double wmax = -1e300, Z = 0.0;
    for (int c = 0; c < NC; c++) {
        for (int k = 0; k < NS * LT; k++) {
            f.s[k] = (c >> k & 1) ? 1 : -1;
        }
        int sg = 0;
        CHECK(dqmc_log_weight(&D, &logw[c], &sg) == 0);
        wmax = logw[c] > wmax ? logw[c] : wmax;
    }
    for (int c = 0; c < NC; c++) {
        pi[c] = exp(logw[c] - wmax);
        Z += pi[c];
    }
    for (int c = 0; c < NC; c++) {
        pi[c] /= Z;
    }
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    const double p200 = run(200, 500000, pi);
    const double p100 = run(100, 900000, pi);
    printf("p(200 cycles)=%.4g p(100 cycles)=%.4g\n", p200, p100);
    CHECK(p200 >= 1e-3);
    CHECK(p100 >= 1e-3);
    TEST_END();
}
