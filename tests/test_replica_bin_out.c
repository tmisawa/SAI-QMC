#include "test_util.h"
#include "replica_bin_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path)
{
    FILE *fp = fopen(path, "r");
    fseek(fp, 0, SEEK_END);
    const long n = ftell(fp);
    rewind(fp);
    char *buf = calloc((size_t)n + 1, 1);
    fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    return buf;
}

int main(void)
{
    ReplicaBin bins[4];
    memset(bins, 0, sizeof bins);
    for (int k = 0; k < 4; k++) {
        bins[k].count = 5;
        bins[k].sum_sign = 5.0;
        bins[k].sum_sign_Ehub = -10.0 - k;
        bins[k].sum_sign_D = 0.5 + 0.01 * k;
        bins[k].accept_accepted = 40ULL;
        bins[k].accept_attempts = 80ULL;
        bins[k].global_accepted = (unsigned long long)k;
        bins[k].global_attempts = 8ULL * (unsigned long long)(k % 2);
    }
    /* szz has q order (pi, 0); sperp has q order (0, pi): indices differ */
    const double szz[8] = {1.0, 0.1, 1.1, 0.2, 1.2, 0.3, 1.3, 0.4};
    const double sperp[8] = {9.0, 2.0, 9.1, 2.1, 9.2, 2.2, 9.3, 2.3};
    const int ids[2] = {0, 1};
    const unsigned long long seeds[2] = {11ULL, 22ULL};
    ReplicaBinView v = {2, 2, 2, 2, bins, szz, sperp, ids, seeds};
    ReplicaBinMeta m = {0, 20, 4, 1.0, 1.0, 4.0, 0.05, 7, 10, 2, "chain",
                        4, 1, 1, "site", 3, 0, 1, 1};
    const char *path = "tests/tmp_replica_bins.tsv";
    FILE *fp = fopen(path, "w");
    CHECK(replica_bin_write(fp, &v, &m, 1) == 0);
    m.beta_index = 1;
    CHECK(replica_bin_write(fp, &v, &m, 0) == 0);   /* second beta appends, no header */
    fclose(fp);

    char *text = slurp(path);
    CHECK(strstr(text, "# columns: beta_index\tbeta_requested") != NULL);
    CHECK(strstr(text, "szz_Q_index=0 szz_0_index=1 sperp_Q_index=1") != NULL);
    /* first data row: beta 0, replica 0, bin 0, measurement sweeps 1..5 */
    CHECK(strstr(text, "\n0\t1\t1\t20\t0\t11\t0\t1\t5\t5\t5\t-10\t0.5\t40\t80\t0\t0\t1\t2\t0.10000000000000001\n") != NULL);
    /* replica 1, bin 1: sweeps 6..10, szz_Q=1.3, sperp_Q=2.3, szz_0=0.4 */
    CHECK(strstr(text, "\n0\t1\t1\t20\t1\t22\t1\t6\t10\t5\t5\t-13\t0.53000000000000003\t40\t80\t3\t8\t1.3\t2.2999999999999998\t0.40000000000000002\n") != NULL);
    int rows = 0;
    for (const char *q = text; (q = strstr(q, "\n")) != NULL; q++) {
        rows += (q[1] != '#' && q[1] != '\0');
    }
    CHECK(rows == 8);
    int headers = 0;
    for (const char *q = text; (q = strstr(q, "# columns:")) != NULL; q++) {
        headers++;
    }
    CHECK(headers == 1);
    free(text);

    /* unmeasured sperp -> nan column */
    ReplicaBinView v2 = v;
    v2.sperp = NULL;
    v2.sperp_nq = 0;
    ReplicaBinMeta m2 = m;
    m2.sperp_Q_index = -1;
    fp = fopen(path, "w");
    CHECK(replica_bin_write(fp, &v2, &m2, 1) == 0);
    fclose(fp);
    text = slurp(path);
    CHECK(strstr(text, "\t1\tnan\t0.10000000000000001\n") != NULL);
    free(text);
    remove(path);

    /* write failure is reported */
    fp = fopen("/dev/full", "w");
    if (fp != NULL) {
        const int rc = replica_bin_write(fp, &v, &m, 1);
        const int rc_close = fclose(fp);
        CHECK(rc != 0 || rc_close != 0);
    } else {
        printf("SKIPPED /dev/full write-failure check (not counted as passed)\n");
    }
    CHECK(replica_bin_write(NULL, &v, &m, 1) != 0);
    TEST_END();
}
