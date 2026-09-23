#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "linalg.h"
#include "measure.h"
#include "model.h"

#include <math.h>

extern void dsyev_(const char *, const char *, const int *, double *,
                   const int *, double *, double *, const int *, int *);

int main(void)
{
    const int Lx = 4;
    const int Ltr = 20;
    const double dtau = 0.1;
    const double beta = Ltr * dtau;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, 0.0, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 5);
    Field f;
    field_init(&f, Lx, Ltr, 0.0, dtau, &r);

    Green Gu;
    Green Gd;
    green_alloc(&Gu, &m, &f, 1.0);
    green_alloc(&Gd, &m, &f, -1.0);
    green_from_scratch(&Gu, 0);
    green_from_scratch(&Gd, 0);
    MeasSample s = measure_sample(Lx, L.t, 0.0, Gu.g, Gd.g);
    CHECK(measure_sample_is_finite(&s) != 0);

    double Kc[16];
    for (int i = 0; i < 16; i++) {
        Kc[i] = m.K[i];
    }
    double w[4];
    double work[256];
    int n = 4;
    int lwork = 256;
    int info = 0;
    dsyev_("N", "U", &n, Kc, &n, w, work, &lwork, &info);
    CHECK(info == 0);

    double ek = 0.0;
    for (int i = 0; i < 4; i++) {
        const double fe = 1.0 / (1.0 + exp(beta * w[i]));
        ek += w[i] * fe;
    }
    ek *= 2.0;

    CHECK_CLOSE(s.ekin, ek, 1e-8);
    CHECK_CLOSE(s.eint, 0.0, 1e-12);
    CHECK_CLOSE(s.ntot, 4.0, 1e-8);

    MeasSample bad_sample = s;
    bad_sample.E = NAN;
    CHECK(measure_sample_is_finite(&bad_sample) == 0);

    double x[3] = {1.0, 3.0, 5.0};
    double mean = 0.0;
    double err = 0.0;
    jackknife(x, 3, &mean, &err);
    CHECK_CLOSE(mean, 3.0, 1e-12);
    CHECK_CLOSE(err, sqrt(4.0 / 3.0), 1e-12);
    jackknife(x, 1, &mean, &err);
    CHECK_CLOSE(mean, 1.0, 1e-12);
    CHECK_CLOSE(err, 0.0, 1e-12);

    green_free(&Gu);
    green_free(&Gd);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
    TEST_END();
}
