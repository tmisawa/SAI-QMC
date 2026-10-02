#include "test_util.h"
#include "io.h"
#include "lattice.h"
#include "replica.h"
#include "structure_factor.h"
#include "tempering_run.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    Params p;
    memset(&p, 0, sizeof p);
    strcpy(p.lattice, "chain");
    p.Lx = 4; p.Ly = 1; p.pbc = 1; p.thop = -1.0; p.U = 4.0;
    p.beta_list[0] = 0.5; p.beta_list[1] = 0.75; p.beta_list[2] = 1.0; p.nbeta = 3;
    p.nwarm = 20; p.nmeas = 40; p.nbin = 4; p.stab_interval = 4;
    strcpy(p.sweep_order, "forward"); strcpy(p.green_rebuild, "combine");
    strcpy(p.global_update, "none"); p.global_interval = 100;
    strcpy(p.global_site_select, "fixed"); p.global_site_power = 2.0;
    strcpy(p.tempering, "dtau_ladder"); p.tempering_ltr = 20; p.tempering_interval = 1;
    p.nrep = 1; p.seed = 12345;

    Lattice L;
    lattice_chain(&L, 4, -1.0, 1);
    StructureFactorPlan off;
    memset(&off, 0, sizeof off);          /* disabled plans */
    ReplicaResult res[3];
    memset(res, 0, sizeof res);
    TemperingLadderOut out;
    CHECK(tempering_ladder_out_alloc(&out, 3, 4) == 0);
    CHECK(dqmc_run_ladder(&p, &L, 2.0, 0, &off, &off, res, &out) == 0);
    for (int k = 0; k < 3; k++) {
        CHECK(res[k].status == 0);
        CHECK(res[k].replica_id == 0);
        for (int b = 0; b < 4; b++) {
            CHECK(res[k].bins[b].count == 10);
        }
    }
    /* 40 measurement rounds alternate pairs: 20 attempts on each pair */
    unsigned long long a0 = 0, a1 = 0;
    for (int b = 0; b < 4; b++) {
        a0 += out.bin_attempts[b * 2 + 0];
        a1 += out.bin_attempts[b * 2 + 1];
    }
    CHECK(a0 + a1 == 40);
    CHECK(a0 == 20 && a1 == 20);
    CHECK(out.warm_attempts[0] + out.warm_attempts[1] == 20);
    /* walkers form a permutation */
    int seen[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        seen[out.final_walker_at[k]]++;
    }
    CHECK(seen[0] == 1 && seen[1] == 1 && seen[2] == 1);
    /* pack/unpack is lossless for 64-bit seeds (a double keeps only 53 bits) */
    uint64_t ibuf[4096];
    double fbuf[4];
    CHECK(tempering_ladder_out_width_u64(3, 4) <= 4096);
    CHECK(tempering_ladder_out_width_f64() == 4);
    TemperingLadderOut back;
    CHECK(tempering_ladder_out_alloc(&back, 3, 4) == 0);
    const unsigned long long seeds[3] = {
        (1ULL << 53) + 1ULL,
        UINT64_MAX,
        replica_seed(4242, 3, 1)          /* 12600977545066055218 at 3215eee */
    };
    for (int t = 0; t < 3; t++) {
        out.swap_seed = seeds[t];
        tempering_ladder_out_pack(&out, ibuf, fbuf);
        CHECK(tempering_ladder_out_unpack(ibuf, fbuf, 3, 4, &back) == 0);
        CHECK(back.swap_seed == seeds[t]);
    }
    CHECK(seeds[2] == 12600977545066055218ULL);
    CHECK(memcmp(back.bin_attempts, out.bin_attempts, 8 * sizeof(unsigned long long)) == 0);
    CHECK(back.round_trips[0] == out.round_trips[0]);
    CHECK(back.sec_meas_exchange == out.sec_meas_exchange);
    /* a non-zero ladder id derives its swap seed from the ladder id */
    ReplicaResult res1[3];
    memset(res1, 0, sizeof res1);
    TemperingLadderOut out1;
    CHECK(tempering_ladder_out_alloc(&out1, 3, 4) == 0);
    CHECK(dqmc_run_ladder(&p, &L, 2.0, 1, &off, &off, res1, &out1) == 0);
    CHECK(out1.ladder_id == 1 && res1[0].replica_id == 1);
    CHECK(out1.swap_seed == replica_seed(p.seed, p.nbeta, 1));
    CHECK(res1[2].seed == replica_seed(p.seed, 2, 1));
    for (int k = 0; k < 3; k++) {
        replica_result_free(&res1[k]);
    }
    tempering_ladder_out_free(&out1);
    /* determinism: the same seed reproduces the same bins */
    ReplicaResult res2[3];
    memset(res2, 0, sizeof res2);
    TemperingLadderOut out2;
    CHECK(tempering_ladder_out_alloc(&out2, 3, 4) == 0);
    CHECK(dqmc_run_ladder(&p, &L, 2.0, 0, &off, &off, res2, &out2) == 0);
    CHECK(res2[2].bins[3].sum_sign_Ehub == res[2].bins[3].sum_sign_Ehub);
    CHECK(memcmp(out2.bin_accepted, out.bin_accepted, 8 * sizeof(unsigned long long)) == 0);
    for (int k = 0; k < 3; k++) {
        replica_result_free(&res[k]);
        replica_result_free(&res2[k]);
    }
    tempering_ladder_out_free(&out);
    tempering_ladder_out_free(&out2);
    tempering_ladder_out_free(&back);
    lattice_free(&L);
    TEST_END();
}
