/* Report the actual team using the same compiler flags as each solver mode. */
#include <stdio.h>
#ifdef AFQMC_USE_MPI
#include <mpi.h>
#endif
#ifdef AFQMC_USE_OPENMP
#include <omp.h>
#endif
int main(int argc, char **argv)
{
    int rank = 0, ranks = 1, threads = 1;
#ifdef AFQMC_USE_MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
#else
    (void)argc;
    (void)argv;
#endif
#ifdef AFQMC_USE_OPENMP
#pragma omp parallel
    {
#pragma omp single
        threads = omp_get_num_threads();
    }
#endif
    printf("{\"rank\":%d,\"ranks\":%d,\"threads\":%d}\n", rank, ranks, threads);
#ifdef AFQMC_USE_MPI
    MPI_Finalize();
#endif
    return 0;
}
