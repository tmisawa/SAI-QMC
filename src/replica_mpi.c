#include "replica_mpi.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

static int min_int(int a, int b)
{
    return (a < b) ? a : b;
}

void replica_mpi_rank_range(int nrep, int nranks, int rank, int *first_replica,
                            int *local_nrep)
{
    if (first_replica != NULL) {
        *first_replica = 0;
    }
    if (local_nrep != NULL) {
        *local_nrep = 0;
    }
    if (nrep < 0 || nranks <= 0 || rank < 0 || rank >= nranks) {
        return;
    }

    const int base = nrep / nranks;
    const int rem = nrep % nranks;
    const int count = base + (rank < rem ? 1 : 0);
    const int first = rank * base + min_int(rank, rem);

    if (first_replica != NULL) {
        *first_replica = first;
    }
    if (local_nrep != NULL) {
        *local_nrep = count;
    }
}

int replica_mpi_rank_for_replica(int nrep, int nranks, int replica_id)
{
    if (nrep <= 0 || nranks <= 0 || replica_id < 0 || replica_id >= nrep) {
        return -1;
    }
    for (int rank = 0; rank < nranks; rank++) {
        int first = 0;
        int count = 0;
        replica_mpi_rank_range(nrep, nranks, rank, &first, &count);
        if (replica_id >= first && replica_id < first + count) {
            return rank;
        }
    }
    return -1;
}

int replica_mpi_gatherv_layout(int nrep, int nbin, int nranks,
                               int item_width, int *recvcounts, int *displs)
{
    if (recvcounts == NULL || displs == NULL || nrep < 0 || nranks <= 0 ||
        nbin < 0 || item_width < 0) {
        return 1;
    }
    if (nbin == 0 || item_width == 0) {
        for (int rank = 0; rank < nranks; rank++) {
            recvcounts[rank] = 0;
            displs[rank] = 0;
        }
        return 0;
    }
    for (int rank = 0; rank < nranks; rank++) {
        int first = 0;
        int count = 0;
        replica_mpi_rank_range(nrep, nranks, rank, &first, &count);
        const size_t width = (size_t)item_width;
        if ((size_t)count > SIZE_MAX / (size_t)nbin ||
            (size_t)count * (size_t)nbin > SIZE_MAX / width ||
            (size_t)first > SIZE_MAX / (size_t)nbin ||
            (size_t)first * (size_t)nbin > SIZE_MAX / width) {
            return 1;
        }
        const size_t recv = (size_t)count * (size_t)nbin * width;
        const size_t disp = (size_t)first * (size_t)nbin * width;
        if (recv > INT_MAX || disp > INT_MAX) {
            return 1;
        }
        recvcounts[rank] = (int)recv;
        displs[rank] = (int)disp;
    }
    return 0;
}

int replica_mpi_local_failed(int run_failed, const ReplicaResult *result)
{
    if (run_failed) {
        return 1;
    }
    if (result == NULL) {
        return 1;
    }
    return result->status != 0;
}

void replica_mpi_pack_bins(const ReplicaResult *results, int local_nrep,
                           int nbin, double *values, int *counts)
{
    if (results == NULL || values == NULL || counts == NULL || local_nrep < 0 ||
        nbin < 0) {
        return;
    }
    for (int r = 0; r < local_nrep; r++) {
        for (int bi = 0; bi < nbin; bi++) {
            const int flat = r * nbin + bi;
            const ReplicaBin *bin = &results[r].bins[bi];
            values[flat * REPLICA_MPI_BIN_DOUBLES + 0] = bin->sum_sign_Ehub;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 1] = bin->sum_sign_Egc;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 2] = bin->sum_sign_Eph;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 3] = bin->sum_sign_N;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 4] = bin->sum_sign_D;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 5] = bin->sum_sign;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 6] =
                (double)bin->accept_accepted;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 7] =
                (double)bin->accept_attempts;
            counts[flat] = bin->count;
        }
    }
}

void replica_mpi_unpack_bins(const double *values, const int *counts,
                             int total_bins, ReplicaBin *bins)
{
    if (values == NULL || counts == NULL || bins == NULL || total_bins < 0) {
        return;
    }
    for (int i = 0; i < total_bins; i++) {
        ReplicaBin *bin = &bins[i];
        bin->sum_sign_Ehub = values[i * REPLICA_MPI_BIN_DOUBLES + 0];
        bin->sum_sign_Egc = values[i * REPLICA_MPI_BIN_DOUBLES + 1];
        bin->sum_sign_Eph = values[i * REPLICA_MPI_BIN_DOUBLES + 2];
        bin->sum_sign_N = values[i * REPLICA_MPI_BIN_DOUBLES + 3];
        bin->sum_sign_D = values[i * REPLICA_MPI_BIN_DOUBLES + 4];
        bin->sum_sign = values[i * REPLICA_MPI_BIN_DOUBLES + 5];
        bin->accept_accepted =
            (unsigned long long)values[i * REPLICA_MPI_BIN_DOUBLES + 6];
        bin->accept_attempts =
            (unsigned long long)values[i * REPLICA_MPI_BIN_DOUBLES + 7];
        bin->count = counts[i];
    }
}

static int replica_mpi_pack_q_observable(const ReplicaResult *results,
                                         int local_nrep, int nbin, int nq,
                                         double *values, int use_sperp)
{
    if (local_nrep < 0 || nbin < 0 || nq < 0) {
        return 1;
    }
    if (nq == 0 || local_nrep == 0 || nbin == 0) {
        return 0;
    }
    if (results == NULL || values == NULL) {
        return 1;
    }
    for (int r = 0; r < local_nrep; r++) {
        const ReplicaQObservable *observable =
            use_sperp ? &results[r].sperp : &results[r].szz;
        if (results[r].nbin != nbin || observable->nq != nq ||
            observable->sum_sign_values == NULL) {
            return 1;
        }
        for (int bi = 0; bi < nbin; bi++) {
            for (int q = 0; q < nq; q++) {
                const double value = observable->sum_sign_values[q + nq * bi];
                if (!isfinite(value)) {
                    return 1;
                }
                values[q + nq * (bi + nbin * r)] = value;
            }
        }
    }
    return 0;
}

int replica_mpi_pack_szz(const ReplicaResult *results, int local_nrep,
                         int nbin, int nq, double *values)
{
    return replica_mpi_pack_q_observable(results, local_nrep, nbin, nq,
                                         values, 0);
}

int replica_mpi_pack_sperp(const ReplicaResult *results, int local_nrep,
                           int nbin, int nq, double *values)
{
    return replica_mpi_pack_q_observable(results, local_nrep, nbin, nq,
                                         values, 1);
}
