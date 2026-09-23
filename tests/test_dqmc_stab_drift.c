#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "lattice.h"
#include "model.h"

static void run_case(int Ltr, int stab, double U, int ph_symmetric,
                     int sweeps, double drift_tol)
{
    const int Lx = 4;
    const double dtau = 0.1;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);
    if (!ph_symmetric) {
        m.ph_symmetric = 0;
    }

    Rng r;
    rng_seed(&r, ph_symmetric ? 777 : 778);
    Field f;
    field_init(&f, Lx, Ltr, U, dtau, &r);

    Dqmc D;
    dqmc_init(&D, &m, &f, &r, stab, NULL);
    CHECK(D.status == 0);
    CHECK(dqmc_enable_stab_drift(&D, 1) == 0);

    for (int sweep = 0; sweep < sweeps; sweep++) {
        dqmc_sweep(&D);
    }

    CHECK(D.status == 0);
    CHECK(D.stab_drift.enabled == 1);
    CHECK(D.stab_drift.failed == 0);
    const int samples_per_sweep = (Ltr - 1) / stab + 1;
    CHECK(D.stab_drift.samples ==
          (unsigned long long)(samples_per_sweep * sweeps));
    CHECK(D.stab_drift.max_tau >= 0);
    CHECK(D.stab_drift.max_tau < Ltr);
    CHECK(D.stab_drift.max_sweep < (unsigned long long)sweeps);
    CHECK(D.stab_drift.max_inf < drift_tol);
    CHECK(D.stab_drift.sum_inf >= 0.0);

    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
}

int main(void)
{
    run_case(8, 4, 4.0, 1, 2, 1e-8);
    run_case(8, 4, 4.0, 0, 2, 1e-8);
    run_case(40, 8, 12.0, 0, 1, 1e-6);
    TEST_END();
}
