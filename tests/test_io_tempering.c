#include "test_util.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

static int read_text(const char *extra, Params *p)
{
    const char *path = "tests/tmp_io_tempering.in";
    FILE *fp = fopen(path, "w");
    fprintf(fp, "lattice=chain\nLx=4\nU=4\nnwarm=10\nnmeas=20\nnbin=2\nnrep=1\n%s",
            extra);
    fclose(fp);
    const int rc = params_read(p, path);
    remove(path);
    return rc;
}

int main(void)
{
    Params p;
    CHECK(read_text("dtau=0.1\nbeta_list=1\n", &p) == 0);
    CHECK(strcmp(p.tempering, "none") == 0 && p.tempering_ltr == 0 &&
          p.tempering_interval == 1 && p.tempering_file[0] == '\0' &&
          p.dtau_given == 1);

    CHECK(read_text("beta_list=1,1.5,2\ntempering=dtau_ladder\ntempering_ltr=40\n"
                    "tempering_interval=2\ntempering_file=pt.tsv\n", &p) == 0);
    CHECK(strcmp(p.tempering, "dtau_ladder") == 0 && p.tempering_ltr == 40 &&
          p.tempering_interval == 2 && strcmp(p.tempering_file, "pt.tsv") == 0 &&
          p.dtau_given == 0);

    CHECK(read_text("dtau=0.1\nbeta_list=1\ntempering=swap\n", &p) != 0);
    CHECK(read_text("dtau=0.1\nbeta_list=1\ntempering_interval=0\n", &p) != 0);
    CHECK(read_text("dtau=0.1\nbeta_list=1\ntempering_ltr=40\n", &p) != 0);
    CHECK(read_text("dtau=0.1\nbeta_list=1\ntempering_file=pt.tsv\n", &p) != 0);
    CHECK(read_text("beta_list=1,2\ntempering=dtau_ladder\n", &p) != 0);
    CHECK(read_text("beta_list=1,2\ntempering=dtau_ladder\ntempering_ltr=-4\n", &p) != 0);
    CHECK(read_text("dtau=0.1\nbeta_list=1,2\ntempering=dtau_ladder\ntempering_ltr=40\n", &p) != 0);
    CHECK(read_text("beta_list=1\ntempering=dtau_ladder\ntempering_ltr=40\n", &p) != 0);
    CHECK(read_text("beta_list=2,1\ntempering=dtau_ladder\ntempering_ltr=40\n", &p) != 0);
    CHECK(read_text("beta_list=1,1\ntempering=dtau_ladder\ntempering_ltr=40\n", &p) != 0);
    CHECK(read_text("beta_list=1,2\ntempering=dtau_ladder\ntempering_ltr=40\nprofile=1\n", &p) != 0);
    CHECK(read_text("beta_list=1,2\ntempering=dtau_ladder\ntempering_ltr=40\n"
                    "stab_drift_file=d.txt\n", &p) != 0);
    CHECK(read_text("beta_list=1,2\ntempering=dtau_ladder\ntempering_ltr=40\n"
                    "global_update=site\nglobal_site_diag_file=sd.tsv\n", &p) != 0);
    /* PT and the global update may be combined */
    CHECK(read_text("beta_list=1,2\ntempering=dtau_ladder\ntempering_ltr=40\n"
                    "global_update=site\nglobal_interval=10\n", &p) == 0);
    TEST_END();
}
