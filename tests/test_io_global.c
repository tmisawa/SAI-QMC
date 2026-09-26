#include "test_util.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

static int read_with(const char *extra, Params *p)
{
    const char *path = "tests/tmp_io_global.in";
    FILE *fp = fopen(path, "w");
    fprintf(fp, "lattice=chain\nLx=4\npbc=1\nt=-1.0\nU=4\ndtau=0.1\n"
                "beta_list=1\nnwarm=10\nnmeas=20\nnbin=2\nstab=4\nnrep=1\n"
                "seed=1\nszz_file=a.tsv\n%s", extra);
    fclose(fp);
    const int rc = params_read(p, path);
    remove(path);
    return rc;
}

int main(void)
{
    Params p;
    CHECK(read_with("", &p) == 0);
    CHECK(strcmp(p.global_update, "none") == 0);
    CHECK(p.global_interval == 100);
    CHECK(p.replica_bin_file[0] == '\0');

    CHECK(read_with("global_update=site\nglobal_interval=7\n"
                    "replica_bin_file=bins.tsv\n", &p) == 0);
    CHECK(strcmp(p.global_update, "site") == 0);
    CHECK(p.global_interval == 7);
    CHECK(strcmp(p.replica_bin_file, "bins.tsv") == 0);

    CHECK(read_with("global_update=cluster\n", &p) != 0);
    CHECK(read_with("global_interval=0\n", &p) != 0);
    CHECK(read_with("global_interval=-3\n", &p) != 0);
    CHECK(read_with("global_interval=2.5\n", &p) != 0);
    CHECK(read_with("global_update=none\nglobal_interval=0\n", &p) != 0);
    /* strict tokens: no trailing junk, no empty values (a typo must not silently disable the update) */
    CHECK(read_with("global_interval=3 junk\n", &p) != 0);
    CHECK(read_with("global_update=site extra\n", &p) != 0);
    CHECK(read_with("global_update=\n", &p) != 0);
    CHECK(read_with("global_interval=\n", &p) != 0);
    CHECK(read_with("replica_bin_file=\n", &p) != 0);
    CHECK(read_with("replica_bin_file=a b.tsv\n", &p) != 0);
    CHECK(read_with("", &p) == 0);
    CHECK(p.global_site_diag_file[0] == '\0');
    CHECK(read_with("global_update=site\nglobal_site_diag_file=site_diag.tsv\n", &p) == 0);
    CHECK(strcmp(p.global_site_diag_file, "site_diag.tsv") == 0);
    /* only meaningful with the site update: reject otherwise (never silently ignore) */
    CHECK(read_with("global_site_diag_file=site_diag.tsv\n", &p) != 0);
    CHECK(read_with("global_update=none\nglobal_site_diag_file=site_diag.tsv\n", &p) != 0);
    CHECK(read_with("global_update=site\nglobal_site_diag_file=\n", &p) != 0);
    CHECK(read_with("global_update=site\nglobal_site_diag_file=a b.tsv\n", &p) != 0);
    /* Stage B keys (spec 4): defaults, accepted values, rejection independent of global_update */
    CHECK(read_with("", &p) == 0);
    CHECK(strcmp(p.global_site_select, "fixed") == 0);
    CHECK(p.global_site_power == 2.0);
    CHECK(read_with("global_update=site\nglobal_site_select=polarized\nglobal_site_power=1.5\n", &p) == 0);
    CHECK(strcmp(p.global_site_select, "polarized") == 0);
    CHECK(p.global_site_power == 1.5);
    CHECK(read_with("global_update=site\nglobal_site_select=fixed\nglobal_site_power=0\n", &p) == 0);
    CHECK(p.global_site_power == 0.0);
    /* the keys are validated even without the site update (never silently ignored) */
    CHECK(read_with("global_site_select=polarized\n", &p) == 0);
    CHECK(strcmp(p.global_site_select, "polarized") == 0);
    CHECK(read_with("global_update=none\nglobal_site_power=3\n", &p) == 0);
    CHECK(read_with("global_site_select=staggered\n", &p) != 0);          /* not implemented */
    CHECK(read_with("global_site_select=random\n", &p) != 0);             /* unknown */
    CHECK(read_with("global_update=none\nglobal_site_select=staggered\n", &p) != 0);
    CHECK(read_with("global_site_select=\n", &p) != 0);
    CHECK(read_with("global_site_select=polarized extra\n", &p) != 0);
    CHECK(read_with("global_site_power=-0.5\n", &p) != 0);
    CHECK(read_with("global_site_power=nan\n", &p) != 0);
    CHECK(read_with("global_site_power=inf\n", &p) != 0);
    CHECK(read_with("global_site_power=abc\n", &p) != 0);
    CHECK(read_with("global_site_power=\n", &p) != 0);
    CHECK(read_with("global_site_power=2 junk\n", &p) != 0);
    CHECK(read_with("global_update=site\nglobal_site_select=fixed\nglobal_site_power=-1\n", &p) != 0); /* validated for fixed too */
    /* path length: 255 characters are accepted unchanged, 256 and 300 are rejected (never truncated) */
    char line[400];
    char path[301];
    memset(path, 'x', 300);
    path[255] = '\0';
    snprintf(line, sizeof line, "replica_bin_file=%s\n", path);
    CHECK(read_with(line, &p) == 0);
    CHECK(strlen(p.replica_bin_file) == 255);
    memset(path, 'x', 300);
    path[256] = '\0';
    snprintf(line, sizeof line, "replica_bin_file=%s\n", path);
    CHECK(read_with(line, &p) != 0);
    memset(path, 'x', 300);
    path[300] = '\0';
    snprintf(line, sizeof line, "replica_bin_file=%s\n", path);
    CHECK(read_with(line, &p) != 0);
    TEST_END();
}
