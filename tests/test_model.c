#include "test_util.h"
#include "lattice.h"
#include "linalg.h"
#include "model.h"

int main(void)
{
    Lattice L;
    lattice_chain(&L, 4, -1.0, 1);

    Model m;
    model_init(&m, &L, 4.0, 0.1, 1, 0.0);
    CHECK_CLOSE(m.mu, 2.0, 1e-12);
    CHECK(m.half_filling == 1);
    CHECK(m.ph_symmetric == 1);
    CHECK(m.bipart == L.bipart);
    for (int i = 0; i < 4; i++) {
        CHECK_CLOSE(m.K[i + i * 4], -2.0, 1e-12);
    }

    double P[16];
    la_matmul(4, m.expK, m.expKinv, P);
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            CHECK_CLOSE(P[i + j * 4], (i == j) ? 1.0 : 0.0, 1e-10);
        }
    }

    model_free(&m);
    lattice_free(&L);

    lattice_chain(&L, 4, -1.0, 1);
    model_init(&m, &L, 4.0, 0.1, 0, 0.7);
    CHECK_CLOSE(m.mu, 0.7, 1e-12);
    CHECK(m.half_filling == 0);
    CHECK(m.ph_symmetric == 0);
    model_free(&m);
    lattice_free(&L);

    lattice_chain(&L, 3, -1.0, 1);
    model_init(&m, &L, 4.0, 0.1, 1, 0.0);
    CHECK(m.half_filling == 1);
    CHECK(m.ph_symmetric == 0);
    model_free(&m);
    lattice_free(&L);
    TEST_END();
}
