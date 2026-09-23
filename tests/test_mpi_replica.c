#include "test_util.h"
#include "measure.h"
#include "replica.h"
#include "replica_mpi.h"

#include <math.h>
#include <limits.h>

static void fill_bin(ReplicaBin *bin, double scale)
{
    MeasSample s;
    s.ekin = -2.0 * scale;
    s.eint = 0.5 * scale;
    s.ntot = 4.0 * scale;
    s.doublon = 0.125 * scale;
    s.E = -1.5 * scale;
    CHECK(replica_bin_add(bin, &s, 2.0, 4.0, 4, 1.0) == 0);
    replica_bin_add_acceptance(bin, (unsigned long long)(2.0 * scale),
                               (unsigned long long)(4.0 * scale));
}

int main(void)
{
    int first = -1;
    int count = -1;

    replica_mpi_rank_range(10, 3, 0, &first, &count);
    CHECK(first == 0);
    CHECK(count == 4);
    replica_mpi_rank_range(10, 3, 1, &first, &count);
    CHECK(first == 4);
    CHECK(count == 3);
    replica_mpi_rank_range(10, 3, 2, &first, &count);
    CHECK(first == 7);
    CHECK(count == 3);

    replica_mpi_rank_range(2, 4, 0, &first, &count);
    CHECK(first == 0);
    CHECK(count == 1);
    replica_mpi_rank_range(2, 4, 1, &first, &count);
    CHECK(first == 1);
    CHECK(count == 1);
    replica_mpi_rank_range(2, 4, 2, &first, &count);
    CHECK(first == 2);
    CHECK(count == 0);
    replica_mpi_rank_range(2, 4, 3, &first, &count);
    CHECK(first == 2);
    CHECK(count == 0);

    CHECK(replica_mpi_rank_for_replica(10, 3, 0) == 0);
    CHECK(replica_mpi_rank_for_replica(10, 3, 3) == 0);
    CHECK(replica_mpi_rank_for_replica(10, 3, 4) == 1);
    CHECK(replica_mpi_rank_for_replica(10, 3, 7) == 2);
    CHECK(replica_mpi_rank_for_replica(2, 4, 0) == 0);
    CHECK(replica_mpi_rank_for_replica(2, 4, 1) == 1);

    int recvcounts[4] = {0};
    int displs[4] = {0};
    CHECK(replica_mpi_gatherv_layout(5, 2, 3, REPLICA_MPI_BIN_DOUBLES,
                                     recvcounts, displs) == 0);
    CHECK(recvcounts[0] == 4 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(recvcounts[1] == 4 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(recvcounts[2] == 2 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(displs[0] == 0);
    CHECK(displs[1] == 2 * 2 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(displs[2] == 4 * 2 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(replica_mpi_gatherv_layout(INT_MAX, 2, 1, 2, recvcounts, displs) !=
          0);

    ReplicaResult ok = {0};
    ok.status = 0;
    ReplicaResult bad = {0};
    bad.status = 7;
    CHECK(replica_mpi_local_failed(0, &ok) == 0);
    CHECK(replica_mpi_local_failed(1, &ok) == 1);
    CHECK(replica_mpi_local_failed(0, &bad) == 1);
    CHECK(replica_mpi_local_failed(1, &bad) == 1);
    CHECK(replica_mpi_local_failed(0, NULL) == 1);

    ReplicaResult results[2] = {0};
    CHECK(replica_result_alloc(&results[0], 2) == 0);
    CHECK(replica_result_alloc(&results[1], 2) == 0);
    fill_bin(&results[0].bins[0], 1.0);
    fill_bin(&results[0].bins[1], 2.0);
    fill_bin(&results[1].bins[0], 3.0);
    fill_bin(&results[1].bins[1], 4.0);
    CHECK(replica_result_enable_szz(&results[0], 3) == 0);
    CHECK(replica_result_enable_szz(&results[1], 3) == 0);
    CHECK(replica_result_enable_sperp(&results[0], 3) == 0);
    CHECK(replica_result_enable_sperp(&results[1], 3) == 0);
    for (int r = 0; r < 2; r++) {
        for (int bi = 0; bi < 2; bi++) {
            for (int q = 0; q < 3; q++) {
                results[r].szz.sum_sign_values[q + 3 * bi] =
                    100.0 * r + 10.0 * bi + q;
                results[r].sperp.sum_sign_values[q + 3 * bi] =
                    1000.0 + 100.0 * r + 10.0 * bi + q;
            }
        }
    }
    double szz_values[2 * 2 * 3] = {0.0};
    CHECK(replica_mpi_pack_szz(results, 2, 2, 3, szz_values) == 0);
    for (int r = 0; r < 2; r++) {
        for (int bi = 0; bi < 2; bi++) {
            for (int q = 0; q < 3; q++) {
                CHECK_CLOSE(szz_values[q + 3 * (bi + 2 * r)],
                            100.0 * r + 10.0 * bi + q, 0.0);
            }
        }
    }
    CHECK(replica_mpi_pack_szz(NULL, 0, 2, 3, NULL) == 0);
    CHECK(replica_mpi_pack_szz(results, 2, 2, 0, NULL) == 0);
    double sperp_values[2 * 2 * 3] = {0.0};
    CHECK(replica_mpi_pack_sperp(results, 2, 2, 3, sperp_values) == 0);
    for (int r = 0; r < 2; r++) {
        for (int bi = 0; bi < 2; bi++) {
            for (int q = 0; q < 3; q++) {
                CHECK_CLOSE(sperp_values[q + 3 * (bi + 2 * r)],
                            1000.0 + 100.0 * r + 10.0 * bi + q, 0.0);
            }
        }
    }

    double values[2 * 2 * REPLICA_MPI_BIN_DOUBLES] = {0.0};
    int counts[2 * 2] = {0};
    replica_mpi_pack_bins(results, 2, 2, values, counts);

    ReplicaBin unpacked[4] = {{0}};
    replica_mpi_unpack_bins(values, counts, 4, unpacked);
    for (int i = 0; i < 4; i++) {
        double eh = 0.0;
        double eg = 0.0;
        double ep = 0.0;
        double nn = 0.0;
        double dd = 0.0;
        double ss = 0.0;
        CHECK(replica_bin_values(&unpacked[i], &eh, &eg, &ep, &nn, &dd,
                                 &ss) == 0);
        CHECK(counts[i] == 1);
        CHECK(fabs(ss - 1.0) < 1e-12);
        CHECK(fabs(replica_bin_acceptance(&unpacked[i]) - 0.5) < 1e-12);
    }
    CHECK(fabs(unpacked[0].sum_sign_Ehub -
               results[0].bins[0].sum_sign_Ehub) < 1e-12);
    CHECK(fabs(unpacked[3].sum_sign_D - results[1].bins[1].sum_sign_D) <
          1e-12);
    CHECK(unpacked[3].accept_accepted == results[1].bins[1].accept_accepted);
    CHECK(unpacked[3].accept_attempts == results[1].bins[1].accept_attempts);

    replica_result_free(&results[0]);
    replica_result_free(&results[1]);

    ReplicaResult nan_result = {0};
    CHECK(replica_result_alloc(&nan_result, 1) == 0);
    fill_bin(&nan_result.bins[0], 1.0);
    nan_result.bins[0].sum_sign_Ehub = NAN;
    double nan_values[REPLICA_MPI_BIN_DOUBLES] = {0.0};
    int nan_counts[1] = {0};
    replica_mpi_pack_bins(&nan_result, 1, 1, nan_values, nan_counts);
    ReplicaBin nan_unpacked[1] = {{0}};
    replica_mpi_unpack_bins(nan_values, nan_counts, 1, nan_unpacked);
    double nan_eh = 0.0;
    double nan_eg = 0.0;
    double nan_ep = 0.0;
    double nan_nn = 0.0;
    double nan_dd = 0.0;
    double nan_ss = 0.0;
    CHECK(replica_bin_values(&nan_unpacked[0], &nan_eh, &nan_eg, &nan_ep,
                             &nan_nn, &nan_dd, &nan_ss) != 0);
    replica_result_free(&nan_result);

    TEST_END();
}
