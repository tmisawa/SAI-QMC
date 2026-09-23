#include "test_util.h"
#include "linalg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int read_label(FILE *fp, const char *expected)
{
    char label[64];
    return fscanf(fp, "%63s", label) == 1 && strcmp(label, expected) == 0;
}

static int load_factor(const char *path, UDV *s, double *min_log,
                       double *max_log)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 1;
    int n = 0;
    double offset = 0.0;
    int ok = read_label(fp, "n") && fscanf(fp, "%d", &n) == 1 &&
             n == s->n && read_label(fp, "offset") &&
             fscanf(fp, "%lf", &offset) == 1 &&
             read_label(fp, "min_logD") &&
             fscanf(fp, "%lf", min_log) == 1 &&
             read_label(fp, "max_logD") &&
             fscanf(fp, "%lf", max_log) == 1 && read_label(fp, "U");
    for (int i = 0; ok && i < n * n; i++) ok = fscanf(fp, "%lf", &s->U[i]) == 1;
    ok = ok && read_label(fp, "D");
    for (int i = 0; ok && i < n; i++) ok = fscanf(fp, "%lf", &s->D[i]) == 1;
    ok = ok && read_label(fp, "T");
    for (int i = 0; ok && i < n * n; i++) ok = fscanf(fp, "%lf", &s->T[i]) == 1;
    s->log_offset = offset;
    if (fclose(fp) != 0) ok = 0;
    return ok ? 0 : 1;
}

static int load_green(const char *path, int n, double *g, int *det_sign)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 1;
    int file_n = 0;
    int ok = read_label(fp, "n") && fscanf(fp, "%d", &file_n) == 1 &&
             file_n == n && read_label(fp, "det_sign") &&
             fscanf(fp, "%d", det_sign) == 1 && read_label(fp, "G");
    for (int i = 0; ok && i < n * n; i++) ok = fscanf(fp, "%lf", &g[i]) == 1;
    if (fclose(fp) != 0) ok = 0;
    return ok ? 0 : 1;
}

static int read_values(FILE *fp, double *x, int count)
{
    for (int i = 0; i < count; i++) {
        if (fscanf(fp, "%lf", &x[i]) != 1) return 0;
    }
    return 1;
}

static int load_two_sided(const char *path, UDV *l, UDV *r, double *g,
                          int *det_sign)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 1;
    int n = 0;
    int ok = read_label(fp, "n") && fscanf(fp, "%d", &n) == 1 &&
             n == l->n && n == r->n && read_label(fp, "left_offset") &&
             fscanf(fp, "%lf", &l->log_offset) == 1 &&
             read_label(fp, "left_U") && read_values(fp, l->U, n * n) &&
             read_label(fp, "left_D") && read_values(fp, l->D, n) &&
             read_label(fp, "left_T") && read_values(fp, l->T, n * n) &&
             read_label(fp, "right_offset") &&
             fscanf(fp, "%lf", &r->log_offset) == 1 &&
             read_label(fp, "right_U") && read_values(fp, r->U, n * n) &&
             read_label(fp, "right_D") && read_values(fp, r->D, n) &&
             read_label(fp, "right_T") && read_values(fp, r->T, n * n) &&
             read_label(fp, "det_sign") &&
             fscanf(fp, "%d", det_sign) == 1 && read_label(fp, "G") &&
             read_values(fp, g, n * n);
    if (fclose(fp) != 0) ok = 0;
    return ok ? 0 : 1;
}

static double relative_max_error(const double *got, const double *expected,
                                 int count)
{
    double max_diff = 0.0;
    double max_ref = 0.0;
    for (int i = 0; i < count; i++) {
        const double diff = fabs(got[i] - expected[i]);
        const double ref = fabs(expected[i]);
        if (diff > max_diff) max_diff = diff;
        if (ref > max_ref) max_ref = ref;
    }
    return max_diff / max_ref;
}

int main(void)
{
    const int n = 16;
    UDV actual;
    udv_init(&actual, n);
    double min_log = 0.0;
    double max_log = 0.0;
    CHECK(load_factor("tests/fixtures/udv_phase6a_actual_n16.txt", &actual,
                      &min_log, &max_log) == 0);
    CHECK_CLOSE(min_log, -644.99938472700933, 1e-12);
    CHECK_CLOSE(max_log, 705.77378900409929, 1e-12);
    CHECK_CLOSE(max_log - min_log, 1350.7731737311086, 1e-12);

    double expected[256];
    int expected_sign = 0;
    CHECK(load_green("tests/fixtures/udv_phase6a_actual_n16_green.txt", n,
                     expected, &expected_sign) == 0);

    double radius = 0.0;
    double margin = 0.0;
    CHECK(udv_recenter(&actual, 8.0, &radius, &margin) == 0);
    CHECK_CLOSE(actual.log_offset, 30.38720213854498, 1e-12);
    CHECK_CLOSE(radius, 675.38658686555431, 1e-12);
    CHECK(margin > 34.39 && margin < 34.40);

    double got[256];
    int got_sign = 0;
    LinalgWork work;
    linalg_work_init(&work, n);
    CHECK(work.ok);
    CHECK(udv_inv_one_plus_work(&actual, got, &got_sign, &work) == 0);
    CHECK(got_sign == expected_sign);
    const double actual_error = relative_max_error(got, expected, n * n);
    CHECK(isfinite(actual_error));
    CHECK(actual_error <= 1e-9);

    linalg_work_free(&work);
    udv_free(&actual);

    const int n2 = 4;
    UDV left;
    UDV right;
    udv_init(&left, n2);
    udv_init(&right, n2);
    double expected2[16];
    double got2[16];
    int expected_sign2 = 0;
    int got_sign2 = 0;
    CHECK(load_two_sided("tests/fixtures/udv_phase6a_two_sided_n4.txt",
                         &left, &right, expected2, &expected_sign2) == 0);
    linalg_work_init(&work, n2);
    CHECK(work.ok);
    CHECK(udv_inv_one_plus_two_sided_work(&left, &right, got2, &got_sign2,
                                          &work) == 0);
    CHECK(got_sign2 == expected_sign2);
    const double two_sided_error =
        relative_max_error(got2, expected2, n2 * n2);
    CHECK(isfinite(two_sided_error));
    CHECK(two_sided_error <= 1e-9);
    linalg_work_free(&work);
    udv_free(&right);
    udv_free(&left);
    TEST_END();
}
