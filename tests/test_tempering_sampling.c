#include "test_util.h"
#include "dqmc.h"
#include "tempering.h"

#include <stdlib.h>
#include <string.h>

enum { NS = 2, LT = 4, NC = 256, K = 3, NLADDER = 4000, CYCLES = 150 };
static const double DTAU[K] = {0.1, 0.2, 0.3};   /* beta = 0.4, 0.8, 1.2 at U=4 */

/* gamma_q: copied verbatim from tests/test_dqmc_global_sampling.c lines 14-48. */
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

static int config_index(const signed char *s)
{
    int c = 0;
    for (int k = 0; k < NS * LT; k++) {
        c |= (s[k] > 0) << k;
    }
    return c;
}

static double chi2_p(const int *count, const double *pi, int nsample)
{
    double chi2 = 0.0, pe = 0.0, po = 0.0;
    int cells = 0;
    for (int c = 0; c < NC; c++) {
        const double e = pi[c] * nsample;
        if (e < 5.0) {
            pe += e;
            po += count[c];
        } else {
            chi2 += (count[c] - e) * (count[c] - e) / e;
            cells++;
        }
    }
    if (pe > 0.0) {
        chi2 += (po - pe) * (po - pe) / pe;
        cells++;
    }
    return gamma_q(0.5 * (cells - 1), 0.5 * chi2);
}

int main(void)
{
    static double pi[K][NC];
    static int count[K][NC];
    Lattice L;
    lattice_chain(&L, NS, -1.0, 0);

    /* exact finite-dtau distributions of each slot (dqmc_log_weight is checked
       against direct determinants in test_dqmc_tempering_weight) */
    for (int k = 0; k < K; k++) {
        Model m;
        model_init(&m, &L, 4.0, DTAU[k], 1, 0.0);
        Rng r;
        rng_seed(&r, 1);
        Field f;
        field_init(&f, NS, LT, 4.0, DTAU[k], &r);
        Dqmc D;
        CHECK(dqmc_init_modes(&D, &m, &f, &r, 4, NULL, DQMC_SWEEP_FORWARD,
                              GREEN_REBUILD_COMBINE) == 0);
        double lw[NC], wmax = -1e300, Z = 0.0;
        for (int c = 0; c < NC; c++) {
            for (int j = 0; j < NS * LT; j++) {
                f.s[j] = (c >> j & 1) ? 1 : -1;
            }
            int sg;
            CHECK(dqmc_log_weight(&D, &lw[c], &sg) == 0);
            wmax = lw[c] > wmax ? lw[c] : wmax;
        }
        for (int c = 0; c < NC; c++) {
            pi[k][c] = exp(lw[c] - wmax);
            Z += pi[k][c];
        }
        for (int c = 0; c < NC; c++) {
            pi[k][c] /= Z;
        }
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
    }

    unsigned long long accepted = 0;
    for (int ch = 0; ch < NLADDER; ch++) {
        Model m[K];
        Rng r[K];
        Field f[K];
        Dqmc D[K];
        Dqmc *ptr[K];
        for (int k = 0; k < K; k++) {
            model_init(&m[k], &L, 4.0, DTAU[k], 1, 0.0);
            rng_seed(&r[k], 700000 + (uint64_t)(ch * K + k));
            field_init(&f[k], NS, LT, 4.0, DTAU[k], &r[k]);
            CHECK(dqmc_init_modes(&D[k], &m[k], &f[k], &r[k], 4, NULL,
                                  DQMC_SWEEP_FORWARD, GREEN_REBUILD_COMBINE) == 0);
            ptr[k] = &D[k];
        }
        TemperingLadder T;
        CHECK(tempering_ladder_init(&T, ptr, K, 9000000 + (uint64_t)ch) == 0);
        for (int c = 0; c < CYCLES; c++) {
            for (int k = 0; k < K; k++) {
                dqmc_sweep(&D[k]);
            }
            CHECK(tempering_ladder_round(&T) == 0);
        }
        for (int k = 0; k < K; k++) {
            count[k][config_index(f[k].s)]++;   /* one independent sample per ladder */
        }
        for (int p = 0; p < K - 1; p++) {
            accepted += T.stats.accepted[p];
        }
        tempering_ladder_free(&T);
        for (int k = 0; k < K; k++) {
            dqmc_free(&D[k]);
            field_free(&f[k]);
            model_free(&m[k]);
        }
    }
    CHECK(accepted > 0);   /* exchanges actually happened */
    for (int k = 0; k < K; k++) {
        const double p = chi2_p(count[k], pi[k], NLADDER);
        printf("slot %d p=%.4g\n", k, p);
        CHECK(p >= 1e-3);
    }
    lattice_free(&L);
    TEST_END();
}
