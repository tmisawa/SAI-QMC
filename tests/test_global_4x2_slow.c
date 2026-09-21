#include "test_util.h"
#include "io.h"
#include "lattice.h"
#include "profiler.h"
#include "replica.h"
#include "replica_run.h"
#include "structure_factor.h"

#include <math.h>
#include <string.h>

enum { NREP = 12 };

int main(void)
{
    Params p;
    memset(&p, 0, sizeof p);
    strcpy(p.lattice, "square");
    p.Lx = 4;
    p.Ly = 2;
    p.pbc = 1;
    p.thop = -1.0;
    p.U = 8.0;
    p.dtau = 0.05;
    p.beta_list[0] = 8.0;
    p.nbeta = 1;
    p.nwarm = 2000;
    p.nmeas = 20000;
    p.nbin = 20;
    p.stab_interval = 4;
    strcpy(p.sweep_order, "alternating");
    strcpy(p.green_rebuild, "combine");
    strcpy(p.parallel, "serial");
    p.nrep = NREP;
    strcpy(p.global_update, "site");
    p.global_interval = 10;
    strcpy(p.szz_q, "all");

    Lattice L;
    lattice_square(&L, p.Lx, p.Ly, p.thop, p.pbc);
    StructureFactorPlan plan;
    CHECK(structure_factor_plan_init(&plan, &L, "all") == 0);
    int qQ = -1, q0 = -1;
    for (int q = 0; q < plan.nq; q++) {
        if (plan.mx[q] == 2 && plan.my[q] == 1) {
            qQ = q;
        }
        if (plan.mx[q] == 0 && plan.my[q] == 0) {
            q0 = q;
        }
    }
    CHECK(qQ >= 0 && q0 >= 0);

    double mean[NREP], inner_se[NREP];
    const int Ltr = 160;
    StructureFactorPlan sperp_off = {0};   /* this regression judges Szz only; Sperp is covered by Task 12 */
    Profiler prof;
    profiler_init_memory(&prof, 0);
    for (int r = 0; r < NREP; r++) {
        ReplicaResult res;
        memset(&res, 0, sizeof res);
        if (dqmc_run_replica(&p, &L, 0, Ltr, p.U / 2.0, r,
                             replica_seed(20260921ULL, 0, r), &plan,
                             &sperp_off, &prof, &res) != 0) {
            CHECK(0 && "replica run failed");
            profiler_close(&prof);
            structure_factor_plan_free(&plan);
            lattice_free(&L);
            TEST_END();
        }
        double sQ = 0.0, s0 = 0.0, ss = 0.0, b1 = 0.0, b2 = 0.0;
        for (int b = 0; b < p.nbin; b++) {
            double vals[64];
            CHECK(plan.nq <= 64);
            CHECK(replica_result_szz_bin_values(&res, b, vals, plan.nq) == 0);
            b1 += 3.0 * vals[qQ];
            b2 += 9.0 * vals[qQ] * vals[qQ];
            sQ += vals[qQ];
            s0 += vals[q0];
            ss += 1.0;
        }
        mean[r] = 3.0 * sQ / ss;
        inner_se[r] = sqrt((b2 / ss - (b1 / ss) * (b1 / ss)) / (ss - 1.0));
        const double sz2 = (double)L.n * s0 / ss;
        printf("replica %2d: 3Szz(Q)=%.4f inner_se=%.4f <Sz_tot^2>=%.4f\n", r,
               mean[r], inner_se[r], sz2);
        CHECK(sz2 < 0.5);                     /* no long-lived S^z != 0 replica */
        replica_result_free(&res);
    }
    double m = 0.0, v = 0.0, se = 0.0;
    for (int r = 0; r < NREP; r++) {
        m += mean[r] / NREP;
        se += inner_se[r] / NREP;
    }
    for (int r = 0; r < NREP; r++) {
        v += (mean[r] - m) * (mean[r] - m) / (NREP - 1);
    }
    printf("mean=%.4f sd=%.4f mean inner se=%.4f\n", m, sqrt(v), se);
    CHECK_CLOSE(m, 1.684, 0.15);              /* dtau=0.05 value of untrapped replicas */
    CHECK(sqrt(v) <= 3.0 * se);
    profiler_close(&prof);
    structure_factor_plan_free(&plan);
    lattice_free(&L);
    TEST_END();
}
