#include "test_util.h"
#include "global_site_diag_out.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int beta_index, Ltr, replica_id, bin;
    double beta_requested, lower, upper;
    unsigned long long seed, attempts, accepted;
    char indicator;
} Row;

/* Parses one data line; returns 0 when the 11 columns are present. */
static int parse_row(const char *line, Row *r)
{
    char ind[4];
    const int n = sscanf(line, "%d\t%lf\t%d\t%d\t%llu\t%3s\t%d\t%lf\t%lf\t%llu\t%llu",
                         &r->beta_index, &r->beta_requested, &r->Ltr, &r->replica_id,
                         &r->seed, ind, &r->bin, &r->lower, &r->upper, &r->attempts,
                         &r->accepted);
    r->indicator = ind[0];
    return n == 11 ? 0 : 1;
}

/* Finds the row (beta_index, replica_id, indicator, bin) in the file text. */
static int find_row(const char *text, int beta, int rep, char ind, int bin, Row *out)
{
    const char *line = text;
    while (line != NULL && *line != '\0') {
        const char *next = strchr(line, '\n');
        if (line[0] != '#') {
            Row r;
            if (parse_row(line, &r) == 0 && r.beta_index == beta &&
                r.replica_id == rep && r.indicator == ind && r.bin == bin) {
                *out = r;
                return 0;
            }
        }
        line = next != NULL ? next + 1 : NULL;
    }
    return 1;
}

static double header_value(const char *text, const char *key)
{
    const char *at = strstr(text, key);
    return at != NULL ? strtod(at + strlen(key), NULL) : NAN;
}

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
    GlobalSiteDiag h[2];
    memset(h, 0, sizeof h);
    h[0].attempts[0][15] = 12ULL; h[0].accepted[0][15] = 3ULL;
    h[0].attempts[1][30] = 12ULL; h[0].accepted[1][30] = 3ULL;
    h[1].attempts[0][49] = 1ULL;
    const int ids[2] = {0, 1};
    const unsigned long long seeds[2] = {11ULL, 22ULL};
    const double lambda = 0.31886946750622885;
    GlobalSiteDiagMeta m = {0, 20, 4, 1.0, 4.0, 0.05, lambda,
                            7, 10, 2, "chain", 4, 1, "pbc=1", 3, "fixed"};
    const char *path = "tests/tmp_site_diag.tsv";
    FILE *fp = fopen(path, "w");
    CHECK(global_site_diag_write(fp, h, 2, ids, seeds, &m, 1) == 0);
    m.beta_index = 1;
    CHECK(global_site_diag_write(fp, h, 2, ids, seeds, &m, 0) == 0);
    fclose(fp);
    char *text = slurp(path);
    CHECK(strstr(text, "# columns: beta_index\tbeta_requested\tLtr\treplica_id\tseed\tindicator\tbin\tbin_lower\tbin_upper\tattempts\taccepted\n") != NULL);
    /* Include the leading space so lambda cannot match the suffix of tanh_lambda. */
    CHECK(strstr(text, " lambda=") != NULL);
    CHECK(strstr(text, " tanh_lambda=") != NULL);
    const double got_lambda = header_value(text, " lambda=");
    const double got_tanh_lambda = header_value(text, " tanh_lambda=");
    CHECK(isfinite(got_lambda));
    CHECK(isfinite(got_tanh_lambda));
    CHECK_CLOSE(got_lambda, lambda, 1e-15);
    CHECK_CLOSE(got_tanh_lambda, tanh(lambda), 1e-15);
    CHECK(strstr(text, "global_site_select=fixed") != NULL);
    CHECK(strstr(text, "\n# lattice=chain Lx=4 Ly=1 n=4 pbc=1 U=4 dtau=") != NULL);
    CHECK(strstr(text, "global_update=site global_interval=3") != NULL);
    Row r;
    CHECK(find_row(text, 0, 0, 'p', 15, &r) == 0);
    CHECK(r.Ltr == 20 && r.seed == 11ULL && r.attempts == 12ULL && r.accepted == 3ULL);
    CHECK_CLOSE(r.beta_requested, 1.0, 1e-15);
    CHECK_CLOSE(r.lower, 0.30, 1e-12);
    CHECK_CLOSE(r.upper, 0.32, 1e-12);
    CHECK(find_row(text, 0, 0, 'd', 30, &r) == 0);
    CHECK(r.attempts == 12ULL && r.accepted == 3ULL);
    CHECK_CLOSE(r.lower, 0.20, 1e-12);
    CHECK_CLOSE(r.upper, 0.24, 1e-12);
    CHECK(find_row(text, 0, 1, 'p', 49, &r) == 0);   /* empty bins are written as zeros */
    CHECK(r.seed == 22ULL && r.attempts == 1ULL && r.accepted == 0ULL);
    CHECK_CLOSE(r.lower, 0.98, 1e-12);
    CHECK_CLOSE(r.upper, 1.0, 1e-12);
    CHECK(find_row(text, 0, 1, 'd', 0, &r) == 0);
    CHECK(r.attempts == 0ULL && r.accepted == 0ULL);
    CHECK_CLOSE(r.lower, -1.0, 1e-12);
    CHECK_CLOSE(r.upper, -0.96, 1e-12);
    CHECK(find_row(text, 1, 0, 'p', 0, &r) == 0);    /* second beta appended without a header */
    CHECK(find_row(text, 2, 0, 'p', 0, &r) != 0);
    int rows = 0, headers = 0;
    for (const char *q = text; (q = strchr(q, '\n')) != NULL; q++) {
        rows += (q[1] != '#' && q[1] != '\0');
        headers += (q[1] == '#');
    }
    CHECK(rows == 2 * 2 * 100);
    CHECK(headers == 4);   /* three explanatory lines + one columns line, written once */
    /* order: beta, replica, indicator (p before d), bin ascending */
    {
        long prev = -1;
        int ordered = 1;
        const char *line = text;
        while (line != NULL && *line != '\0') {
            const char *next = strchr(line, '\n');
            if (line[0] != '#') {
                Row x;
                if (parse_row(line, &x) == 0) {
                    const long key = ((long)x.beta_index * 100 + x.replica_id) * 200 +
                                     (x.indicator == 'p' ? 0 : 100) + x.bin;
                    if (key <= prev) ordered = 0;
                    prev = key;
                }
            }
            line = next != NULL ? next + 1 : NULL;
        }
        CHECK(ordered);
    }
    free(text);

    /* a directional boundary label replaces pbc= verbatim */
    GlobalSiteDiagMeta mb = m;
    mb.beta_index = 0;
    strcpy(mb.boundary, "bc_x=antiperiodic bc_y=periodic");
    fp = fopen(path, "w");
    CHECK(global_site_diag_write(fp, h, 2, ids, seeds, &mb, 1) == 0);
    fclose(fp);
    text = slurp(path);
    CHECK(strstr(text, "\n# lattice=chain Lx=4 Ly=1 n=4 bc_x=antiperiodic bc_y=periodic U=4 dtau=") != NULL);
    CHECK(strstr(text, "pbc=") == NULL);
    free(text);
    remove(path);
    CHECK(global_site_diag_write(NULL, h, 2, ids, seeds, &m, 0) != 0);
    CHECK(global_site_diag_write(stdout, h, 0, ids, seeds, &m, 0) != 0);
    TEST_END();
}
