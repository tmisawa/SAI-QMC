#include "test_util.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_text(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");
    fputs(text, fp);
    fclose(fp);
}

int main(void)
{
    Params p;

    write_text("/tmp/afqmc_valid.in",
               "lattice=chain\nLx=4\nLy=1\npbc=1\nU=4\ndtau=0.1\n"
               "nmeas=20\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_valid.in") == 0);
    CHECK(strcmp(p.output_file, "observables.dat") == 0);
    CHECK(p.profile == 0);
    CHECK(p.profile_file[0] == '\0');
    CHECK(p.stab_drift_file[0] == '\0');
    CHECK(p.udv_scale_file[0] == '\0');
    CHECK(p.udv_centered_file[0] == '\0');
    CHECK(strcmp(p.sweep_order, "forward") == 0);
    CHECK(strcmp(p.green_rebuild, "combine") == 0);
    CHECK(strcmp(p.szz_q, "none") == 0);
    CHECK(strcmp(p.szz_file, "szz.dat") == 0);
    CHECK(strcmp(p.sperp_q, "none") == 0);
    CHECK(strcmp(p.sperp_file, "sperp.dat") == 0);
    CHECK(strcmp(p.spin_consistency_file, "none") == 0);

    write_text("/tmp/afqmc_scalar_output.in", "output_file=results.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_scalar_output.in") == 0);
    CHECK(strcmp(p.output_file, "results.dat") == 0);
    write_text("/tmp/afqmc_scalar_output.in", "output_file=none\n");
    CHECK(params_read(&p, "/tmp/afqmc_scalar_output.in") == 0);
    CHECK(strcmp(p.output_file, "none") == 0);
    write_text("/tmp/afqmc_scalar_output.in", "output_file=\n");
    CHECK(params_read(&p, "/tmp/afqmc_scalar_output.in") != 0);
    write_text("/tmp/afqmc_scalar_output.in", "output_file=bad name.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_scalar_output.in") != 0);
    write_text("/tmp/afqmc_scalar_output.in", " output_file=result.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_scalar_output.in") != 0);

    write_text("/tmp/afqmc_szz.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "szz_q=0:0,2:0\nszz_file=/tmp/szz-out.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_szz.in") == 0);
    CHECK(strcmp(p.szz_q, "0:0,2:0") == 0);
    CHECK(strcmp(p.szz_file, "/tmp/szz-out.dat") == 0);

    write_text("/tmp/afqmc_sperp.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "sperp_q=0:0,2:0\nsperp_file=/tmp/sperp-out.dat\n"
               "spin_consistency_file=/tmp/spin-check.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_sperp.in") == 0);
    CHECK(strcmp(p.sperp_q, "0:0,2:0") == 0);
    CHECK(strcmp(p.sperp_file, "/tmp/sperp-out.dat") == 0);
    CHECK(strcmp(p.spin_consistency_file, "/tmp/spin-check.dat") == 0);

    char selector_255[256];
    memset(selector_255, 'a', 255);
    selector_255[255] = '\0';
    FILE *szz_fp = fopen("/tmp/afqmc_szz_255.in", "w");
    fprintf(szz_fp, "lattice=chain\nnmeas=20\nnbin=10\nszz_q=%s\n",
            selector_255);
    fclose(szz_fp);
    CHECK(params_read(&p, "/tmp/afqmc_szz_255.in") == 0);
    CHECK(strlen(p.szz_q) == 255);

    char selector_256[257];
    memset(selector_256, 'a', 256);
    selector_256[256] = '\0';
    szz_fp = fopen("/tmp/afqmc_szz_256.in", "w");
    fprintf(szz_fp, "lattice=chain\nnmeas=20\nnbin=10\nszz_q=%s\n",
            selector_256);
    fclose(szz_fp);
    CHECK(params_read(&p, "/tmp/afqmc_szz_256.in") != 0);

    write_text("/tmp/afqmc_szz_empty.in",
               "lattice=chain\nnmeas=20\nnbin=10\nszz_q=\n");
    CHECK(params_read(&p, "/tmp/afqmc_szz_empty.in") != 0);

    write_text("/tmp/afqmc_szz_space.in",
               "lattice=chain\nnmeas=20\nnbin=10\nszz_q=0:0, 2:0\n");
    CHECK(params_read(&p, "/tmp/afqmc_szz_space.in") != 0);

    write_text("/tmp/afqmc_szz_file_empty.in",
               "lattice=chain\nnmeas=20\nnbin=10\nszz_file=\n");
    CHECK(params_read(&p, "/tmp/afqmc_szz_file_empty.in") != 0);

    write_text("/tmp/afqmc_sperp_empty.in",
               "lattice=chain\nnmeas=20\nnbin=10\nsperp_q=\n");
    CHECK(params_read(&p, "/tmp/afqmc_sperp_empty.in") != 0);

    write_text("/tmp/afqmc_sperp_space.in",
               "lattice=chain\nnmeas=20\nnbin=10\nsperp_q=0:0, 2:0\n");
    CHECK(params_read(&p, "/tmp/afqmc_sperp_space.in") != 0);

    write_text("/tmp/afqmc_consistency_empty.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "spin_consistency_file=\n");
    CHECK(params_read(&p, "/tmp/afqmc_consistency_empty.in") != 0);

    szz_fp = fopen("/tmp/afqmc_szz_long_line.in", "w");
    fputs("lattice=chain\nnmeas=20\nnbin=10\n#", szz_fp);
    for (int i = 0; i < 600; i++) {
        fputc('x', szz_fp);
    }
    fputc('\n', szz_fp);
    fclose(szz_fp);
    CHECK(params_read(&p, "/tmp/afqmc_szz_long_line.in") != 0);

    write_text("/tmp/afqmc_unknown.in",
               "lattice=chain\nunknown_key=1\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_unknown.in") != 0);

    write_text("/tmp/afqmc_badnum.in",
               "lattice=chain\nLx=abc\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_badnum.in") != 0);

    write_text("/tmp/afqmc_badbin.in",
               "lattice=chain\nnmeas=21\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_badbin.in") != 0);

    write_text("/tmp/afqmc_badbeta.in",
               "lattice=chain\nbeta_list=1.0,foo\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_badbeta.in") != 0);

    write_text("/tmp/afqmc_badbc.in",
               "lattice=chain\nbc=typo\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_badbc.in") != 0);

    write_text("/tmp/afqmc_badseed.in",
               "lattice=chain\nseed=-1\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_badseed.in") != 0);

    write_text("/tmp/afqmc_profile.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "profile=1\nprofile_file=/tmp/afqmc_profile.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_profile.in") == 0);
    CHECK(p.profile == 1);
    CHECK(strcmp(p.profile_file, "/tmp/afqmc_profile.dat") == 0);

    write_text("/tmp/afqmc_stab_drift.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "stab_drift_file=/tmp/afqmc_stab_drift.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_stab_drift.in") == 0);
    CHECK(strcmp(p.stab_drift_file, "/tmp/afqmc_stab_drift.dat") == 0);

    write_text("/tmp/afqmc_udv_scale.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "udv_scale_file=/tmp/afqmc_udv_scale.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_udv_scale.in") == 0);
    CHECK(strcmp(p.udv_scale_file, "/tmp/afqmc_udv_scale.dat") == 0);

    write_text("/tmp/afqmc_udv_scale_alias.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "udv_scale_diagnostics_file=/tmp/afqmc_udv_scale_alias.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_udv_scale_alias.in") == 0);
    CHECK(strcmp(p.udv_scale_file,
                 "/tmp/afqmc_udv_scale_alias.dat") == 0);

    write_text("/tmp/afqmc_udv_centered.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "udv_centered_file=/tmp/afqmc_udv_centered.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_udv_centered.in") == 0);
    CHECK(strcmp(p.udv_centered_file,
                 "/tmp/afqmc_udv_centered.dat") == 0);

    write_text("/tmp/afqmc_udv_centered_alias.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "udv_centered_diagnostics_file=/tmp/afqmc_udv_centered_alias.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_udv_centered_alias.in") == 0);
    CHECK(strcmp(p.udv_centered_file,
                 "/tmp/afqmc_udv_centered_alias.dat") == 0);

    write_text("/tmp/afqmc_sweep_order_alt.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "sweep_order=alternating\n");
    CHECK(params_read(&p, "/tmp/afqmc_sweep_order_alt.in") == 0);
    CHECK(strcmp(p.sweep_order, "alternating") == 0);

    write_text("/tmp/afqmc_bad_sweep_order.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "sweep_order=zigzag\n");
    CHECK(params_read(&p, "/tmp/afqmc_bad_sweep_order.in") != 0);

    write_text("/tmp/afqmc_green_rebuild_combine.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "green_rebuild=combine\n");
    CHECK(params_read(&p, "/tmp/afqmc_green_rebuild_combine.in") == 0);
    CHECK(strcmp(p.green_rebuild, "combine") == 0);

    write_text("/tmp/afqmc_green_rebuild_two_sided.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "green_rebuild=two_sided\n");
    CHECK(params_read(&p, "/tmp/afqmc_green_rebuild_two_sided.in") == 0);
    CHECK(strcmp(p.green_rebuild, "two_sided") == 0);

    write_text("/tmp/afqmc_green_rebuild_centered.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "green_rebuild=centered\n");
    CHECK(params_read(&p, "/tmp/afqmc_green_rebuild_centered.in") == 0);
    CHECK(strcmp(p.green_rebuild, "centered") == 0);

    write_text("/tmp/afqmc_bad_green_rebuild.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "green_rebuild=log\n");
    CHECK(params_read(&p, "/tmp/afqmc_bad_green_rebuild.in") != 0);

    write_text("/tmp/afqmc_badprofile.in",
               "lattice=chain\nnmeas=20\nnbin=10\nprofile=2\n");
    CHECK(params_read(&p, "/tmp/afqmc_badprofile.in") != 0);

    write_text("/tmp/afqmc_badprofile_text.in",
               "lattice=chain\nnmeas=20\nnbin=10\nprofile=yes\n");
    CHECK(params_read(&p, "/tmp/afqmc_badprofile_text.in") != 0);

    write_text("/tmp/afqmc_parallel_defaults.in",
               "lattice=chain\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_parallel_defaults.in") == 0);
    CHECK(strcmp(p.parallel, "serial") == 0);
    CHECK(p.nrep == 1);
    CHECK(p.replica_log[0] == '\0');

    write_text("/tmp/afqmc_parallel_omp.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "parallel=omp\nnrep=4\nreplica_log=/tmp/reps.dat\n");
    CHECK(params_read(&p, "/tmp/afqmc_parallel_omp.in") == 0);
    CHECK(strcmp(p.parallel, "omp") == 0);
    CHECK(p.nrep == 4);
    CHECK(strcmp(p.replica_log, "/tmp/reps.dat") == 0);

    write_text("/tmp/afqmc_parallel_reserved.in",
               "lattice=chain\nnmeas=20\nnbin=10\nparallel=mpi\n");
    CHECK(params_read(&p, "/tmp/afqmc_parallel_reserved.in") == 0);
    CHECK(strcmp(p.parallel, "mpi") == 0);

    write_text("/tmp/afqmc_parallel_hybrid.in",
               "lattice=chain\nnmeas=20\nnbin=10\nparallel=hybrid\n");
    CHECK(params_read(&p, "/tmp/afqmc_parallel_hybrid.in") == 0);
    CHECK(strcmp(p.parallel, "hybrid") == 0);

    write_text("/tmp/afqmc_badparallel.in",
               "lattice=chain\nnmeas=20\nnbin=10\nparallel=threads\n");
    CHECK(params_read(&p, "/tmp/afqmc_badparallel.in") != 0);

    write_text("/tmp/afqmc_badnrep_zero.in",
               "lattice=chain\nnmeas=20\nnbin=10\nnrep=0\n");
    CHECK(params_read(&p, "/tmp/afqmc_badnrep_zero.in") != 0);

    write_text("/tmp/afqmc_badnrep_text.in",
               "lattice=chain\nnmeas=20\nnbin=10\nnrep=four\n");
    CHECK(params_read(&p, "/tmp/afqmc_badnrep_text.in") != 0);

    TEST_END();
}
