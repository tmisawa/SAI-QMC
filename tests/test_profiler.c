#include "test_util.h"
#include "profiler.h"

#include <stdio.h>
#include <string.h>

static int file_contains(const char *path, const char *needle)
{
    char buf[8192];
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

int main(void)
{
    const char *enabled_path = "/tmp/afqmc_profile_enabled.dat";
    const char *disabled_path = "/tmp/afqmc_profile_disabled.dat";
    remove(enabled_path);
    remove(disabled_path);

    Profiler disabled;
    profiler_init(&disabled, 0, disabled_path);
    CHECK(!profiler_error(&disabled));
    profiler_beta_begin(&disabled, 1.0, 1.0, 0.1, 10);
    profiler_phase_set(&disabled, PROF_PHASE_WARMUP);
    profiler_add(&disabled, PROF_DQMC_SWEEP, 0.25);
    profiler_beta_end(&disabled);
    profiler_close(&disabled);
    CHECK(!file_exists(disabled_path));

    Profiler prof;
    profiler_init(&prof, 1, enabled_path);
    CHECK(!profiler_error(&prof));
    profiler_set_current(&prof);
    CHECK(profiler_current() == &prof);
    profiler_beta_begin(&prof, 1.0, 1.0, 0.1, 10);
    profiler_phase_set(&prof, PROF_PHASE_WARMUP);
    profiler_add(&prof, PROF_DQMC_SWEEP, 0.25);
    profiler_phase_set(&prof, PROF_PHASE_MEASUREMENT);
    profiler_add(&prof, PROF_MEASURE_SAMPLE, 0.125);
    profiler_add(&prof, PROF_MEASURE_SZZ, 0.0625);
    profiler_add(&prof, PROF_MEASURE_SPERP, 0.03125);
    profiler_add(&prof, PROF_MEASURE_SPIN, 0.015625);
    profiler_beta_end(&prof);
    profiler_set_current(NULL);
    CHECK(profiler_current() == NULL);
    profiler_close(&prof);
    CHECK(!profiler_error(&prof));

    CHECK(file_contains(
        enabled_path,
        "# beta T dtau Ltr phase region calls total_sec avg_sec frac_beta"));
    CHECK(file_contains(enabled_path, "warmup dqmc_sweep 1 "));
    CHECK(file_contains(enabled_path, "measurement measure_sample 1 "));
    CHECK(file_contains(enabled_path, "measurement measure_szz 1 "));
    CHECK(file_contains(enabled_path, "measurement measure_sperp 1 "));
    CHECK(file_contains(enabled_path, "measurement measure_spin 1 "));
    CHECK(file_contains(enabled_path, "all dqmc_sweep 1 "));
    CHECK(file_contains(enabled_path, "all beta_total 1 "));
    CHECK(file_contains(enabled_path, "wall_sec thread_total_sec nrep parallel"));
    CHECK(file_contains(enabled_path, " 1 serial"));

    Profiler local;
    profiler_init_memory(&local, 1);
    profiler_beta_begin(&local, 2.0, 0.5, 0.1, 20);
    profiler_phase_set(&local, PROF_PHASE_MEASUREMENT);
    profiler_add(&local, PROF_DQMC_SWEEP, 1.25);

    Profiler merged;
    const char *merged_path = "/tmp/afqmc_profile_merged.dat";
    remove(merged_path);
    profiler_init(&merged, 1, merged_path);
    profiler_set_metadata(&merged, 4, "omp");
    profiler_beta_begin(&merged, 2.0, 0.5, 0.1, 20);
    profiler_merge(&merged, &local);
    profiler_beta_end(&merged);
    profiler_close(&merged);
    CHECK(!profiler_error(&merged));
    CHECK(file_contains(merged_path, "measurement dqmc_sweep 1 1.25"));
    CHECK(file_contains(merged_path, " 4 omp"));

#ifdef AFQMC_USE_MPI
    Profiler mpi_prof;
    const char *mpi_path = "/tmp/afqmc_profile_mpi.dat";
    remove(mpi_path);
    profiler_init(&mpi_prof, 1, mpi_path);
    profiler_set_mpi_metadata(&mpi_prof, 5, "mpi", 3);
    profiler_beta_begin(&mpi_prof, 4.0, 0.25, 0.1, 40);
    profiler_phase_set(&mpi_prof, PROF_PHASE_MEASUREMENT);
    profiler_add(&mpi_prof, PROF_DQMC_SWEEP, 2.0);
    profiler_beta_end(&mpi_prof);
    profiler_close(&mpi_prof);
    CHECK(!profiler_error(&mpi_prof));
    CHECK(file_contains(mpi_path,
                        "wall_sec thread_total_sec nrep parallel nranks"));
    CHECK(file_contains(mpi_path, " 5 mpi 3"));
#endif

    TEST_END();
}
