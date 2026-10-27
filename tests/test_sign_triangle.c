/* Negative-sign fixture (plan section 6.2 items 4-5): 3-site triangle, every
   off-diagonal hopping -1, U=4, mu=2, dtau=0.5, Ltr=4. 48 of the 4096 HS
   configurations have a negative weight.

   Reference values come from two sources that do not use the solver:
   tests/fixtures/sign_triangle/oracle.txt (NumPy enumeration and Fock trace,
   generate_oracle.py) and an in-test enumeration with the closed form
   exp(-dtau K) = exp(-dtau (1 - mu)) [I + (exp(3 dtau) - 1)/3 J], plain
   matrix products and Gauss-Jordan elimination. Against them the solver's
   UDV signs, log weights, Green functions and measurements are checked for
   every configuration and stabilization setting; local flip ratios at every
   slice; initialization from a negative configuration; sweeps with global
   updates that visit negative configurations; global world-line flips with
   acceptance and rejection; and the invariance of the polarized selection
   weights under a flip. */
#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "global_select.h"
#include "green.h"
#include "io.h"
#include "lattice.h"
#include "measure.h"
#include "model.h"
#include "replica_chain.h"
#include "rng.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NS = 3, LT = 4, NF = NS * LT, NCFG = 1 << NF };
#define FIX "tests/fixtures/sign_triangle/"

static const double U = 4.0, MU = 2.0, DT = 0.5;

typedef struct {
    double Z_HS, Z_abs, sign, E, N, D, Z_trotter;
    int nneg;
    int neg[NCFG];
    signed char first[NF];
} Oracle;

typedef struct {
    double w, logw;
    int sign;
    double g[2][NS * NS];
    double E, N, D;
} Ref;

static Ref REF[NCFG];

static int read_oracle(Oracle *o)
{
    FILE *fp = fopen(FIX "oracle.txt", "r");
    if (fp == NULL) {
        return 1;
    }
    memset(o, 0, sizeof *o);
    char key[64];
    int seen = 0;
    while (fscanf(fp, "%63s", key) == 1) {
        if (key[0] == '#') {
            int ch;
            while ((ch = fgetc(fp)) != '\n' && ch != EOF) {
            }
            continue;
        }
        double *dst = strcmp(key, "Z_HS") == 0 ? &o->Z_HS
                    : strcmp(key, "Z_abs") == 0 ? &o->Z_abs
                    : strcmp(key, "average_sign") == 0 ? &o->sign
                    : strcmp(key, "E_hub") == 0 ? &o->E
                    : strcmp(key, "ntot") == 0 ? &o->N
                    : strcmp(key, "doublon") == 0 ? &o->D
                    : strcmp(key, "Z_trotter") == 0 ? &o->Z_trotter : NULL;
        if (dst != NULL) {
            if (fscanf(fp, "%lf", dst) != 1) {
                break;
            }
            seen++;
        } else if (strcmp(key, "negative_configurations") == 0) {
            if (fscanf(fp, "%d", &o->nneg) != 1) {
                break;
            }
        } else if (strcmp(key, "negative_indices") == 0) {
            for (int k = 0; k < o->nneg; k++) {
                if (fscanf(fp, "%d", &o->neg[k]) != 1) {
                    fclose(fp);
                    return 1;
                }
            }
            seen++;
        } else if (strcmp(key, "first_negative_slice_major") == 0) {
            for (int k = 0; k < NF; k++) {
                int v = 0;
                if (fscanf(fp, "%d", &v) != 1) {
                    fclose(fp);
                    return 1;
                }
                o->first[k] = (signed char)v;
            }
            seen++;
        } else {
            char rest[512];
            if (fscanf(fp, "%511s", rest) != 1) {
                break;
            }
        }
    }
    fclose(fp);
    return seen == 9 ? 0 : 1;
}

static void config_field(int c, signed char *s)
{
    for (int k = 0; k < NF; k++) {
        s[k] = ((c >> k) & 1) ? 1 : -1;
    }
}

static int field_config(const signed char *s)
{
    int c = 0;
    for (int k = 0; k < NF; k++) {
        if (s[k] > 0) {
            c |= 1 << k;
        }
    }
    return c;
}

static void mat3_mul(const double *a, const double *b, double *out)
{
    double t[9];
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            double s = 0.0;
            for (int k = 0; k < 3; k++) {
                s += a[i + 3 * k] * b[k + 3 * j];
            }
            t[i + 3 * j] = s;
        }
    }
    memcpy(out, t, sizeof t);
}

/* Gauss-Jordan with partial pivoting; returns det(m), fills inv. */
static double det_inv3(const double *m, double *inv)
{
    double a[3][6];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            a[i][j] = m[i + 3 * j];
            a[i][j + 3] = (i == j) ? 1.0 : 0.0;
        }
    }
    double det = 1.0;
    for (int c = 0; c < 3; c++) {
        int piv = c;
        for (int r = c + 1; r < 3; r++) {
            if (fabs(a[r][c]) > fabs(a[piv][c])) {
                piv = r;
            }
        }
        if (piv != c) {
            for (int j = 0; j < 6; j++) {
                const double t = a[c][j];
                a[c][j] = a[piv][j];
                a[piv][j] = t;
            }
            det = -det;
        }
        const double d = a[c][c];
        det *= d;
        for (int j = 0; j < 6; j++) {
            a[c][j] /= d;
        }
        for (int r = 0; r < 3; r++) {
            if (r != c) {
                const double f = a[r][c];
                for (int j = 0; j < 6; j++) {
                    a[r][j] -= f * a[c][j];
                }
            }
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            inv[i + 3 * j] = a[i][j + 3];
        }
    }
    return det;
}

static void ref_expk(double *e)
{
    const double a = exp(-DT * (1.0 - MU));
    const double c = (exp(3.0 * DT) - 1.0) / 3.0;
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            e[i + 3 * j] = a * ((i == j ? 1.0 : 0.0) + c);
        }
    }
}

static void build_reference(void)
{
    double ek[9];
    ref_expk(ek);
    const double lam = acosh(exp(DT * U / 2.0));
    for (int c = 0; c < NCFG; c++) {
        signed char s[NF];
        config_field(c, s);
        Ref *r = &REF[c];
        r->w = 1.0;
        for (int sp = 0; sp < 2; sp++) {
            const double sigma = sp == 0 ? 1.0 : -1.0;
            double a[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            for (int l = 0; l < LT; l++) {
                double b[9];
                for (int j = 0; j < 3; j++) {
                    const double v = exp(sigma * lam * s[l * NS + j] - DT * U / 2.0);
                    for (int i = 0; i < 3; i++) {
                        b[i + 3 * j] = ek[i + 3 * j] * v;
                    }
                }
                mat3_mul(b, a, a);
            }
            for (int i = 0; i < 3; i++) {
                a[i + 3 * i] += 1.0;
            }
            r->w *= det_inv3(a, r->g[sp]);
        }
        r->sign = r->w < 0.0 ? -1 : 1;
        r->logw = log(fabs(r->w));
        double kin = 0.0, dbl = 0.0, ntot = 0.0;
        for (int sp = 0; sp < 2; sp++) {
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    if (i != j) {
                        kin += -1.0 * (0.0 - r->g[sp][j + 3 * i]);
                    }
                }
            }
        }
        for (int i = 0; i < 3; i++) {
            const double nu = 1.0 - r->g[0][i + 3 * i];
            const double nd = 1.0 - r->g[1][i + 3 * i];
            dbl += nu * nd;
            ntot += nu + nd;
        }
        r->E = kin + U * dbl;
        r->N = ntot;
        r->D = dbl / 3.0;
    }
}

static void check_reference_against_oracle(const Oracle *o)
{
    double zw = 0.0, za = 0.0, ew = 0.0, nw = 0.0, dw = 0.0;
    int nneg = 0;
    int ok_list = 1;
    for (int c = 0; c < NCFG; c++) {
        const Ref *r = &REF[c];
        zw += r->w;
        za += fabs(r->w);
        ew += r->w * r->E;
        nw += r->w * r->N;
        dw += r->w * r->D;
        if (r->sign < 0) {
            ok_list &= nneg < o->nneg && o->neg[nneg] == c;
            nneg++;
        }
    }
    CHECK(o->nneg == 48 && nneg == 48 && ok_list);
    CHECK_CLOSE(zw / NCFG, o->Z_HS, 1e-12 * o->Z_HS);
    CHECK_CLOSE(za / NCFG, o->Z_abs, 1e-12 * o->Z_abs);
    CHECK_CLOSE(zw / za, o->sign, 1e-13);
    CHECK_CLOSE(ew / zw, o->E, 1e-12);
    CHECK_CLOSE(nw / zw, o->N, 1e-12);
    CHECK_CLOSE(dw / zw, o->D, 1e-13);
    CHECK_CLOSE(o->Z_HS, o->Z_trotter, 1e-12 * o->Z_trotter);
    CHECK(field_config(o->first) == o->neg[0]);
}

static int make_params(Params *p, int stab, const char *rebuild)
{
    const char *path = "tests/tmp_sign_triangle.in";
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return 1;
    }
    fprintf(fp, "lattice=file\nlatfile=" FIX "triangle.txt\nU=4\nmu=2\n"
                "dtau=0.5\nbeta_list=2\nnwarm=0\nnmeas=2\nnbin=2\nstab=%d\n"
                "green_rebuild=%s\nseed=1\n", stab, rebuild);
    fclose(fp);
    const int rc = params_read(p, path);
    remove(path);
    return rc;
}

/* Largest Green-function difference relative to the largest reference
   element: configurations with det(1 + A) near zero have large entries. */
static double state_error(const Dqmc *D, const Ref *r)
{
    double m = 0.0;
    double scale = 1.0;
    for (int k = 0; k < 9; k++) {
        m = fmax(m, fabs(D->Gu.g[k] - r->g[0][k]));
        m = fmax(m, fabs(D->Gd.g[k] - r->g[1][k]));
        scale = fmax(scale, fmax(fabs(r->g[0][k]), fabs(r->g[1][k])));
    }
    return m / scale;
}

static void check_measurement(const Lattice *L, const Dqmc *D, const Ref *r,
                              double tol)
{
    const MeasSample s = measure_sample(3, L->t, U, D->Gu.g, D->Gd.g);
    CHECK_CLOSE(s.E, r->E, tol * (1.0 + fabs(r->E)));
    CHECK_CLOSE(s.ntot, r->N, tol * (1.0 + fabs(r->N)));
    CHECK_CLOSE(s.doublon, r->D, tol * (1.0 + fabs(r->D)));
}

/* Every configuration through the solver's UDV path. */
static void check_all_configurations(const Lattice *L, ReplicaChain *ch)
{
    Dqmc *D = &ch->D;
    double gmax = 0.0, lmax = 0.0;
    int sign_bad = 0, logw_sign_bad = 0;
    for (int c = 0; c < NCFG; c++) {
        signed char s[NF];
        config_field(c, s);
        CHECK(dqmc_replace_field(D, s) == 0);
        sign_bad += (int)D->sign != REF[c].sign;
        gmax = fmax(gmax, state_error(D, &REF[c]));
        double logw = 0.0;
        int sg = 0;
        CHECK(dqmc_log_weight(D, &logw, &sg) == 0);
        logw_sign_bad += sg != REF[c].sign;
        lmax = fmax(lmax, fabs(logw - REF[c].logw));
        if (c % 97 == 0 || REF[c].sign < 0) {
            check_measurement(L, D, &REF[c], 2e-10);
        }
    }
    CHECK(sign_bad == 0);
    CHECK(logw_sign_bad == 0);
    /* observed: 3.5e-11 and 6.5e-11, set by the conditioning of 1 + A */
    CHECK(gmax < 2e-10);
    CHECK(lmax < 2e-10);
    if (gmax >= 2e-10 || lmax >= 2e-10) {
        printf("  all configurations: relative G error %.3g, log|W| error %.3g\n",
               gmax, lmax);
    }
}

/* Local flip ratio at every slice equals W(flipped) / W, sign included. */
static void check_local_ratios(ReplicaChain *ch)
{
    Dqmc *D = &ch->D;
    double rmax = 0.0;
    int sign_bad = 0;
    int negative_ratios = 0;
    for (int c = 0; c < NCFG; c += 3) {
        config_field(c, D->f->s);
        for (int l = 0; l < LT; l++) {
            CHECK(green_from_scratch(&D->Gu, l) == 0);
            CHECK(green_from_scratch(&D->Gd, l) == 0);
            for (int i = 0; i < NS; i++) {
                const double Ru = green_delay_ratio_N(&D->Gu, i, green_flipN(&D->Gu, i));
                const double Rd = green_delay_ratio_N(&D->Gd, i, green_flipN(&D->Gd, i));
                const double ref = REF[c ^ (1 << (l * NS + i))].w / REF[c].w;
                rmax = fmax(rmax, fabs(Ru * Rd - ref) / (1.0 + fabs(ref)));
                sign_bad += (Ru * Rd < 0.0) != (ref < 0.0);
                negative_ratios += ref < 0.0;
            }
        }
    }
    CHECK(rmax < 1e-10);
    CHECK(sign_bad == 0);
    CHECK(negative_ratios > 0);
    signed char s[NF];
    config_field(0, s);
    CHECK(dqmc_replace_field(D, s) == 0);
}

/* dqmc_init_modes from the stored negative configuration. */
static void check_init_from_negative(const Lattice *L, const Oracle *o)
{
    const GreenRebuildMode modes[3] = {GREEN_REBUILD_COMBINE,
                                       GREEN_REBUILD_TWO_SIDED,
                                       GREEN_REBUILD_CENTERED};
    for (int k = 0; k < 3; k++) {
        Model m;
        model_init(&m, L, U, DT, 0, MU);
        Rng r;
        rng_seed(&r, 5);
        Field f;
        field_init(&f, NS, LT, U, DT, &r);
        memcpy(f.s, o->first, NF);
        Dqmc D;
        CHECK(dqmc_init_modes(&D, &m, &f, &r, 3, NULL, DQMC_SWEEP_FORWARD,
                              modes[k]) == 0);
        CHECK(D.use_ph == 0);
        CHECK(D.sign == -1.0);
        CHECK(state_error(&D, &REF[o->neg[0]]) < 1e-10);
        check_measurement(L, &D, &REF[o->neg[0]], 1e-10);
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
    }
}

/* Sweeps (and global passes) from the negative configuration: after each,
   the running sign and Green functions are those of the current field. */
static void check_sweeps(const Lattice *L, ReplicaChain *ch, const Oracle *o,
                         int polarized)
{
    Dqmc *D = &ch->D;
    CHECK(dqmc_replace_field(D, o->first) == 0);
    CHECK(D->sign == -1.0);
    if (polarized) {
        CHECK(dqmc_set_global_site_select(D, 1, 2.0) == 0);
    }
    int sign_bad = 0, negative_visits = 0, global_negative = 0;
    double gmax = 0.0;
    for (int k = 0; k < 2000; k++) {
        dqmc_sweep(D);
        if (k % 5 == 4) {
            CHECK(dqmc_global_site_pass(D) == 0);
        }
        CHECK(D->status == 0);
        const int c = field_config(D->f->s);
        sign_bad += (int)D->sign != REF[c].sign;
        negative_visits += D->sign < 0.0;
        global_negative += (k % 5 == 4) && D->sign < 0.0;
        gmax = fmax(gmax, state_error(D, &REF[c]));
        if (D->sign < 0.0 && negative_visits < 4) {
            check_measurement(L, D, &REF[c], 1e-9);
        }
    }
    CHECK(sign_bad == 0);
    CHECK(gmax < 1e-9);
    CHECK(negative_visits > 0);
    CHECK(global_negative > 0);
    if (polarized) {
        CHECK(dqmc_set_global_site_select(D, 0, 0.0) == 0);
    }
}

/* Global world-line flips: accepted proposals carry the exact new weight
   and sign; rejected ones restore the field and keep the weight. */
static void check_global_steps(ReplicaChain *ch, const Oracle *o)
{
    Dqmc *D = &ch->D;
    CHECK(dqmc_replace_field(D, o->first) == 0);
    const int c0 = o->neg[0];
    int sign_changes = 0, rejections = 0;
    for (int i = 0; i < NS; i++) {
        double logw = 0.0;
        int sg = 0;
        CHECK(dqmc_log_weight(D, &logw, &sg) == 0);
        CHECK(sg == -1 && fabs(logw - REF[c0].logw) < 2e-10);
        int flipped = c0;
        for (int l = 0; l < LT; l++) {
            flipped ^= 1 << (l * NS + i);
        }
        /* u = 0 always accepts */
        int acc = 0;
        CHECK(dqmc_global_site_step(D, i, 0.0, &logw, &sg, &acc) == 0);
        CHECK(acc == 1);
        CHECK(field_config(D->f->s) == flipped);
        CHECK(sg == REF[flipped].sign);
        CHECK(fabs(logw - REF[flipped].logw) < 2e-10);
        sign_changes += sg != -1;
        field_flip_site_worldline(D->f, i);
        CHECK(dqmc_log_weight(D, &logw, &sg) == 0);
        /* u just below 1: accepted only if the weight does not decrease */
        const double u = nextafter(1.0, 0.0);
        const double delta = REF[flipped].logw - REF[c0].logw;
        const double before = logw;
        CHECK(dqmc_global_site_step(D, i, u, &logw, &sg, &acc) == 0);
        if (delta < log(u)) {
            rejections++;
            CHECK(acc == 0);
            CHECK(field_config(D->f->s) == c0);
            CHECK(logw == before && sg == -1);
        } else {
            CHECK(acc == 1 && field_config(D->f->s) == flipped);
            field_flip_site_worldline(D->f, i);
            CHECK(dqmc_log_weight(D, &logw, &sg) == 0);
        }
    }
    CHECK(sign_changes > 0);
    CHECK(rejections > 0);
    /* the pass ends with a rebuild that matches the reached configuration */
    CHECK(dqmc_global_site_pass(D) == 0);
    const int c = field_config(D->f->s);
    CHECK((int)D->sign == REF[c].sign);
    CHECK(state_error(D, &REF[c]) < 2e-10);
}

/* Polarized selection weights depend on |sum_l s_il| only: a world-line flip
   leaves every weight unchanged, whatever the sign of the configuration. */
static void check_polarized_invariance(const Oracle *o, double lambda)
{
    Field f;
    Rng r;
    rng_seed(&r, 9);
    field_init(&f, NS, LT, U, DT, &r);
    memcpy(f.s, o->first, NF);
    int m[NS];
    double p[NS], d[NS], w0[NS], cum0[NS], w1[NS], cum1[NS];
    const double p0 = global_site_weight_scale(lambda);
    field_site_sums(&f, m);
    CHECK(global_site_indicators(m, NULL, NS, LT, p, d) == 0);
    CHECK(global_site_weights_p(p, NS, p0, 2.0, w0, cum0) == 0);
    int across_sign = 0;
    for (int i = 0; i < NS; i++) {
        field_flip_site_worldline(&f, i);
        field_site_sums(&f, m);
        CHECK(global_site_indicators(m, NULL, NS, LT, p, d) == 0);
        CHECK(global_site_weights_p(p, NS, p0, 2.0, w1, cum1) == 0);
        CHECK(memcmp(w0, w1, sizeof w0) == 0);
        across_sign += REF[field_config(f.s)].sign != REF[o->neg[0]].sign;
        field_flip_site_worldline(&f, i);
    }
    /* at least one flip changes the sign of the weight */
    CHECK(across_sign > 0);
    field_free(&f);
}

int main(void)
{
    Oracle o;
    CHECK(read_oracle(&o) == 0);
    build_reference();
    check_reference_against_oracle(&o);

    Lattice L;
    CHECK(lattice_from_file(&L, FIX "triangle.txt") == 0);
    CHECK(L.is_bipartite == 0 && L.n == 3);
    check_init_from_negative(&L, &o);

    const char *modes[3] = {"combine", "two_sided", "centered"};
    for (int mk = 0; mk < 3; mk++) {
        for (int stab = 1; stab <= 4; stab++) {
            Params p;
            CHECK(make_params(&p, stab, modes[mk]) == 0);
            ReplicaChain ch;
            CHECK(replica_chain_init(&ch, &p, &L, 0, LT, p.dtau, 0,
                                     1000ULL + (unsigned long long)(4 * mk + stab),
                                     NULL) == 0);
            CHECK(ch.m.mu == MU && ch.m.ph_symmetric == 0 && ch.D.use_ph == 0);
            double ek[9];
            ref_expk(ek);
            for (int k = 0; k < 9; k++) {
                CHECK_CLOSE(ch.m.expK[k], ek[k], 1e-14 * fabs(ek[k]) + 1e-15);
            }
            check_all_configurations(&L, &ch);
            if (stab == 3) {
                check_local_ratios(&ch);
                check_global_steps(&ch, &o);
                check_polarized_invariance(&o, ch.f.lambda);
            }
            check_sweeps(&L, &ch, &o, stab % 2 == 0);
            replica_chain_free(&ch);
        }
    }
    lattice_free(&L);
    TEST_END();
}
