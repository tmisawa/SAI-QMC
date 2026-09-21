#include "test_util.h"
#include "io.h"
#include "lattice.h"
#include "profiler.h"
#include "replica.h"
#include "replica_run.h"
#include "structure_factor.h"

#include <string.h>

static void base_params(Params *p)
{
    memset(p, 0, sizeof *p);
    strcpy(p->lattice, "chain");
    p->Lx = 4;
    p->Ly = 1;
    p->pbc = 1;
    p->thop = -1.0;
    p->U = 4.0;
    p->dtau = 0.1;
    p->beta_list[0] = 1.0;
    p->nbeta = 1;
    p->nwarm = 7;
    p->nmeas = 20;
    p->nbin = 4;
    p->stab_interval = 4;
    strcpy(p->sweep_order, "alternating");
    strcpy(p->green_rebuild, "combine");
    strcpy(p->parallel, "serial");
    p->nrep = 1;
    strcpy(p->global_update, "none");
    p->global_interval = 100;
}

/* Returns 0 on success. dqmc_run_replica rejects NULL plans before it initializes
   the result, so disabled plans are passed by address and the result is only read on success. */
static int run(const Params *p, ReplicaResult *res)
{
    Lattice L;
    lattice_chain(&L, p->Lx, p->thop, p->pbc);
    Profiler prof;
    profiler_init_memory(&prof, 0);
    StructureFactorPlan szz_plan = {0};
    StructureFactorPlan sperp_plan = {0};
    memset(res, 0, sizeof *res);
    const int rc = dqmc_run_replica(p, &L, 0, 10, p->U / 2.0, 0, 4711ULL,
                                    &szz_plan, &sperp_plan, &prof, res);
    profiler_close(&prof);
    lattice_free(&L);
    return rc;
}

int main(void)
{
    Params p;
    ReplicaResult a, b, c;

    /* none: no attempts, identical to a run that never heard of the key */
    base_params(&p);
    if (run(&p, &a) != 0) {
        CHECK(0 && "baseline replica run failed");
        TEST_END();
    }
    for (int bi = 0; bi < 4; bi++) {
        CHECK(a.bins[bi].global_attempts == 0ULL);
        CHECK(a.bins[bi].global_accepted == 0ULL);
    }

    /* interval 3, nwarm 7: passes after global sweeps 3,6 (warmup) and 9,12,15,18,21,24,27.
       Measurement sweeps 1..20 are global 8..27; bins of 5 sweeps hold
       global 8-12, 13-17, 18-22, 23-27 -> passes 2,1,2,2 -> attempts 8,4,8,8. */
    strcpy(p.global_update, "site");
    p.global_interval = 3;
    if (run(&p, &b) != 0) {
        CHECK(0 && "global replica run failed");
        replica_result_free(&a);
        TEST_END();
    }
    const unsigned long long expect[4] = {8ULL, 4ULL, 8ULL, 8ULL};
    for (int bi = 0; bi < 4; bi++) {
        CHECK(b.bins[bi].global_attempts == expect[bi]);
        CHECK(b.bins[bi].global_accepted <= b.bins[bi].global_attempts);
    }

    /* interval longer than the run: no pass at all */
    p.global_interval = 1000;
    if (run(&p, &c) != 0) {
        CHECK(0 && "long-interval replica run failed");
        replica_result_free(&a);
        replica_result_free(&b);
        TEST_END();
    }
    for (int bi = 0; bi < 4; bi++) {
        CHECK(c.bins[bi].global_attempts == 0ULL);
        CHECK_CLOSE(c.bins[bi].sum_sign_Ehub, a.bins[bi].sum_sign_Ehub, 0.0);
    }

    replica_result_free(&a);
    replica_result_free(&b);
    replica_result_free(&c);
    TEST_END();
}
