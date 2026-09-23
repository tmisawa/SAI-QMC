#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "linalg.h"
#include "model.h"

int main(void)
{
    const int Lx = 4;
    const int Ltr = 8;
    const double dtau = 0.1;
    const double U = 4.0;
    const int isite = 2;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);

    for (int si = 0; si < 2; si++) {
        const double sigma = (si == 0) ? 1.0 : -1.0;
        Rng r;
        rng_seed(&r, 3);
        Field f;
        field_init(&f, Lx, Ltr, U, dtau, &r);
        Green G;
        green_alloc(&G, &m, &f, sigma);
        green_from_scratch(&G, 0);

        const signed char s_old = f.s[isite];
        const double Nexp = field_N(&f, sigma, s_old);
        const double Ngot = green_flipN(&G, isite);
        CHECK_CLOSE(Ngot, Nexp, 1e-12);

        const double Rexp = 1.0 + (1.0 - G.g[isite + isite * Lx]) * Nexp;
        const double Rgot = green_ratio_N(&G, isite, Ngot);
        CHECK_CLOSE(Rgot, Rexp, 1e-12);

        green_update(&G, isite, Ngot);
        f.s[isite] *= -1;

        Green G2;
        green_alloc(&G2, &m, &f, sigma);
        green_from_scratch(&G2, 0);
        for (int k = 0; k < Lx * Lx; k++) {
            CHECK_CLOSE(G.g[k], G2.g[k], 1e-10);
        }

        green_free(&G2);
        green_free(&G);
        field_free(&f);
    }

    model_free(&m);
    lattice_free(&L);
    TEST_END();
}
