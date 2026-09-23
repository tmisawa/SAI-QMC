#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "model.h"

#include <math.h>

int main(void)
{
    const int Lx = 2;
    const int Ltr = 4;
    const double dtau = 0.1;
    const double U = 4.0;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 0);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 9);
    Field f;
    field_init(&f, Lx, Ltr, U, dtau, &r);
    Green G;
    green_alloc(&G, &m, &f, 1.0);

    double B[4];
    green_build_B(&G, 0, B);
    const double c = -dtau * U / 2.0;
    for (int j = 0; j < Lx; j++) {
        const double dj = exp(f.lambda * G.sigma * (double)f.s[j] + c);
        for (int i = 0; i < Lx; i++) {
            CHECK_CLOSE(B[i + j * Lx], m.expK[i + j * Lx] * dj, 1e-12);
        }
    }

    green_free(&G);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
    TEST_END();
}
