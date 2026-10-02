#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "lattice.h"
#include "model.h"

#include <string.h>

int main(void)
{
    Lattice L;
    lattice_chain(&L, 4, -1.0, 1);
    Model m;
    model_init(&m, &L, 4.0, 0.1, 1, 0.0);
    Rng r1, r2;
    rng_seed(&r1, 77);
    rng_seed(&r2, 77);
    Field fr, fu;
    field_init(&fr, 4, 8, 4.0, 0.1, &r1);
    field_init(&fu, 4, 8, 4.0, 0.1, &r2);
    field_set_uniform(&fu, 1);
    for (int k = 0; k < 32; k++) {
        CHECK(fu.s[k] == 1);
    }
    CHECK(memcmp(&r1, &r2, sizeof r1) == 0);   /* same RNG consumption as random */
    Dqmc D;
    CHECK(dqmc_init_modes(&D, &m, &fu, &r2, 4, NULL, DQMC_SWEEP_FORWARD,
                          GREEN_REBUILD_COMBINE) == 0);
    Model m2;
    model_init(&m2, &L, 4.0, 0.1, 1, 0.0);
    Rng r3;
    rng_seed(&r3, 1);
    Field f3;
    field_init(&f3, 4, 8, 4.0, 0.1, &r3);
    field_set_uniform(&f3, 1);
    Dqmc D3;
    CHECK(dqmc_init_modes(&D3, &m2, &f3, &r3, 4, NULL, DQMC_SWEEP_FORWARD,
                          GREEN_REBUILD_COMBINE) == 0);
    for (int q = 0; q < 16; q++) {
        CHECK_CLOSE(D.Gu.g[q], D3.Gu.g[q], 1e-12);  /* G built from the uniform field */
    }
    dqmc_free(&D);
    dqmc_free(&D3);
    field_free(&fr);
    field_free(&fu);
    field_free(&f3);
    model_free(&m);
    model_free(&m2);
    lattice_free(&L);
    TEST_END();
}
