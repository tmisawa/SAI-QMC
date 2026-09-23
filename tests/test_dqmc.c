#include "test_util.h"
#include "dqmc.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "model.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

#define CENTERED_DIAG_HEADER                                                   \
    "# beta_index Ltr replica_id seed sweep_count tau boundary spin"  \
    " direction stored_min_logD stored_max_logD stored_radius"          \
    " log_offset effective_min_logD effective_max_logD"                  \
    " remaining_margin warning status\n"

static int file_contains_count(const char *path, const char *needle)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    int count = 0;
    char line[1024];
    while (fgets(line, sizeof line, fp) != NULL) {
        if (strstr(line, needle) != NULL) {
            count++;
        }
    }
    fclose(fp);
    return count;
}

static int count_spaces(const char *line)
{
    int count = 0;
    for (const char *p = line; *p != '\0'; p++) {
        if (*p == ' ') {
            count++;
        }
    }
    return count;
}

static int split_dat(char *line, char **fields, int max_fields)
{
    int n = 0;
    char *p = line;
    while (n < max_fields) {
        fields[n++] = p;
        char *separator = strchr(p, ' ');
        if (separator == NULL) {
            break;
        }
        *separator = '\0';
        p = separator + 1;
    }
    char *nl = strchr(fields[n - 1], '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    return n;
}

static void check_single_factor_sentinel(char **fields)
{
    CHECK(isnan(strtod(fields[18], NULL)));
    CHECK(isnan(strtod(fields[19], NULL)));
    CHECK(isnan(strtod(fields[20], NULL)));
    CHECK(strcmp(fields[21], "0") == 0);
    CHECK(strcmp(fields[22], "0") == 0);
    CHECK(strcmp(fields[23], "0") == 0);
    CHECK(isnan(strtod(fields[24], NULL)));
}

static void check_udv_scale_phase21_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    CHECK(fp != NULL);
    if (fp == NULL) {
        return;
    }

    int forward_u = 0;
    int backward_u = 0;
    int suffix_pre = 0;
    int prefix_pre = 0;
    int left_forward = 0;
    int left_backward = 0;
    char line[2048];
    while (fgets(line, sizeof line, fp) != NULL) {
        CHECK(count_spaces(line) == 24);
        char *fields[25];
        const int nfield = split_dat(line, fields, 25);
        CHECK(nfield == 25);
        if (nfield != 25) {
            continue;
        }

        const char *spin = fields[10];
        const char *direction = fields[11];
        if (strcmp(spin, "u") == 0 && strcmp(direction, "forward") == 0) {
            forward_u++;
        } else if (strcmp(spin, "u") == 0 &&
                   strcmp(direction, "backward") == 0) {
            backward_u++;
        } else if (strcmp(direction, "stack_suffix_pre_rmul") == 0) {
            suffix_pre++;
            check_single_factor_sentinel(fields);
        } else if (strcmp(direction, "stack_prefix_pre_lmul") == 0) {
            prefix_pre++;
            check_single_factor_sentinel(fields);
        } else if (strcmp(direction, "left_udv_forward") == 0) {
            left_forward++;
            CHECK(atoi(fields[8]) == 6);
            CHECK(atoi(fields[9]) == 3);
            check_single_factor_sentinel(fields);
        } else if (strcmp(direction, "left_udv_backward") == 0) {
            left_backward++;
            CHECK(atoi(fields[8]) == 0);
            CHECK(atoi(fields[9]) == 0);
            check_single_factor_sentinel(fields);
        }
    }
    fclose(fp);

    CHECK(forward_u == 2);
    CHECK(backward_u == 2);
    CHECK(suffix_pre >= 2);
    CHECK(prefix_pre >= 1);
    CHECK(left_forward == 1);
    CHECK(left_backward == 1);
}

static void init_centered_diag_file(const char *path)
{
    FILE *fp = fopen(path, "w");
    CHECK(fp != NULL);
    if (fp == NULL) {
        return;
    }
    fputs(CENTERED_DIAG_HEADER, fp);
    CHECK(fclose(fp) == 0);
}

static void check_centered_diag_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    CHECK(fp != NULL);
    if (fp == NULL) {
        return;
    }

    int healthy = 0;
    int warning = 0;
    int centered_radius = 0;
    int centered_update = 0;
    int effective_range = 0;
    int margin_hard_fail = 0;
    char line[2048];
    CHECK(fgets(line, sizeof line, fp) != NULL);
    CHECK(strcmp(line, CENTERED_DIAG_HEADER) == 0);
    while (fgets(line, sizeof line, fp) != NULL) {
        CHECK(count_spaces(line) == 17);
        char *fields[18];
        const int nfield = split_dat(line, fields, 18);
        CHECK(nfield == 18);
        if (nfield != 18) {
            continue;
        }
        CHECK(strcmp(fields[0], "2") == 0);
        CHECK(strcmp(fields[1], "6") == 0);
        CHECK(strcmp(fields[2], "3") == 0);
        CHECK(strcmp(fields[3], "777") == 0);
        CHECK(strcmp(fields[7], "u") == 0);

        const char *direction = fields[8];
        const double radius = strtod(fields[11], NULL);
        const double offset = strtod(fields[12], NULL);
        const double remaining = strtod(fields[15], NULL);
        if (strcmp(direction, "test_healthy") == 0) {
            healthy++;
            CHECK_CLOSE(radius, 2.0, 1e-12);
            CHECK_CLOSE(offset, 3.0, 1e-12);
            CHECK(strcmp(fields[16], "0") == 0);
            CHECK(strcmp(fields[17], "ok") == 0);
        } else if (strcmp(direction, "test_warning") == 0) {
            warning++;
            CHECK_CLOSE(radius, 680.0, 1e-12);
            CHECK_CLOSE(remaining, log(DBL_MAX) - 680.0, 1e-12);
            CHECK(strcmp(fields[16], "1") == 0);
            CHECK(strcmp(fields[17], "warning") == 0);
        } else if (strcmp(direction, "test_centered_radius") == 0) {
            centered_radius++;
            CHECK(strcmp(fields[17], "hard_fail_centered_radius") == 0);
        } else if (strcmp(direction, "test_centered_update") == 0) {
            centered_update++;
            CHECK(strcmp(fields[17],
                         "hard_fail_centered_update_margin") == 0);
        } else if (strcmp(direction, "test_effective_range") == 0) {
            effective_range++;
            CHECK(strcmp(fields[17],
                         "hard_fail_effective_log_range") == 0);
        } else if (strcmp(direction, "test_margin_hard_fail") == 0) {
            margin_hard_fail++;
            CHECK(remaining <= 8.0);
            CHECK(strcmp(fields[16], "1") == 0);
            CHECK(strcmp(fields[17], "hard_fail_centered_radius") == 0);
        }
    }
    fclose(fp);

    CHECK(healthy == 1);
    CHECK(warning == 1);
    CHECK(centered_radius == 1);
    CHECK(centered_update == 1);
    CHECK(effective_range == 1);
    CHECK(margin_hard_fail == 1);
}

int main(void)
{
    const int Lx = 4;
    const int Ltr = 6;
    const double dtau = 0.1;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, 0.0, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 17);
    Field f;
    field_init(&f, Lx, Ltr, 0.0, dtau, &r);

    Dqmc D;
    dqmc_init(&D, &m, &f, &r, 2, NULL);
    dqmc_sweep(&D);
    CHECK(D.Gu.cur_l == 0);
    CHECK(D.Gd.cur_l == 0);
    CHECK_CLOSE(D.sign, 1.0, 1e-12);
    CHECK(D.accept_attempts == (unsigned long long)Lx * (unsigned long long)Ltr);
    CHECK(D.accept_accepted <= D.accept_attempts);

    Green Gu_ref;
    Green Gd_ref;
    green_alloc(&Gu_ref, &m, &f, 1.0);
    green_alloc(&Gd_ref, &m, &f, -1.0);
    green_from_scratch(&Gu_ref, 0);
    green_from_scratch(&Gd_ref, 0);
    for (int k = 0; k < Lx * Lx; k++) {
        CHECK_CLOSE(D.Gu.g[k], Gu_ref.g[k], 1e-9);
        CHECK_CLOSE(D.Gd.g[k], Gd_ref.g[k], 1e-9);
    }

    green_free(&Gu_ref);
    green_free(&Gd_ref);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    /* The opt-in centered rebuild must also run in the non-PH forward path. */
    lattice_chain(&L, 3, -1.0, 1);
    model_init(&m, &L, 4.0, dtau, 1, 0.0);
    rng_seed(&r, 29);
    field_init(&f, L.n, 5, 4.0, dtau, &r);
    CHECK(dqmc_init_modes(&D, &m, &f, &r, 2, NULL, DQMC_SWEEP_FORWARD,
                          GREEN_REBUILD_CENTERED) == 0);
    CHECK(D.status == 0);
    CHECK(D.use_ph == 0);
    dqmc_sweep(&D);
    CHECK(D.status == 0);
    CHECK(D.Gu.rebuild_mode == GREEN_REBUILD_CENTERED);
    CHECK(D.Gd.rebuild_mode == GREEN_REBUILD_CENTERED);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    lattice_chain(&L, 3, -1.0, 1);
    model_init(&m, &L, 4.0, dtau, 1, 0.0);
    rng_seed(&r, 19);
    field_init(&f, L.n, 5, 4.0, dtau, &r);
    dqmc_init(&D, &m, &f, &r, 2, NULL);
    CHECK(D.status == 0);
    CHECK(D.use_ph == 0);
    dqmc_sweep(&D);
    CHECK(D.status == 0);
    CHECK(D.accept_attempts == (unsigned long long)L.n * 5ULL);
    CHECK(D.accept_accepted <= D.accept_attempts);
    CHECK_CLOSE(D.sign, (double)(D.Gu.det_sign * D.Gd.det_sign), 0.0);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    lattice_chain(&L, Lx, -1.0, 1);
    model_init(&m, &L, 4.0, dtau, 1, 0.0);
    rng_seed(&r, 23);
    field_init(&f, Lx, 5, 4.0, dtau, &r);
    dqmc_init(&D, &m, &f, &r, 8, NULL);
    for (int sweep = 0; sweep < 20; sweep++) {
        dqmc_sweep(&D);
        CHECK(D.Gu.cur_l == 0);
        CHECK(D.Gd.cur_l == 0);
        CHECK_CLOSE(D.sign, 1.0, 1e-12);
    }
    CHECK(D.accept_attempts == 20ULL * (unsigned long long)L.n * 5ULL);
    CHECK(D.accept_accepted <= D.accept_attempts);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    /* Deterministic sign-reset guard (no reliance on natural drift):
       deliberately corrupt D.sign, then one sweep must reset it to the exact
       stabilized value via the end-of-sweep green_from_scratch(0). Ltr < stab
       so the periodic block never fires and only the end-of-sweep reset runs. */
    lattice_chain(&L, Lx, -1.0, 1);
    model_init(&m, &L, 8.0, dtau, 1, 0.0);
    rng_seed(&r, 31);
    field_init(&f, Lx, 5, 8.0, dtau, &r);
    dqmc_init(&D, &m, &f, &r, 16, NULL);
    CHECK(D.status == 0);
    CHECK(D.Gu.det_sign == 1 || D.Gu.det_sign == -1);
    CHECK(D.Gd.det_sign == 1 || D.Gd.det_sign == -1);
    for (int sweep = 0; sweep < 5; sweep++) {
        D.sign = -12345.0;
        dqmc_sweep(&D);
        CHECK(D.status == 0);
        CHECK_CLOSE(D.sign, (double)(D.Gu.det_sign * D.Gd.det_sign), 0.0);
    }
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    const char *scale_path = "/tmp/afqmc_test_udv_scale.dat";
    remove(scale_path);
    lattice_chain(&L, Lx, -1.0, 1);
    model_init(&m, &L, 4.0, dtau, 1, 0.0);
    rng_seed(&r, 37);
    field_init(&f, Lx, 6, 4.0, dtau, &r);
    CHECK(dqmc_init_mode(&D, &m, &f, &r, 2, NULL,
                         DQMC_SWEEP_ALTERNATING) == 0);
    CHECK(D.status == 0);
    CHECK(dqmc_enable_udv_scale_diag(&D, scale_path, 2, 6, 3, 777ULL, 4.0,
                                     dtau) == 0);
    green_stack_build_prefix(&D.Gu, &D.stack_alt_u);
    CHECK(D.Gu.work.failed == 0);
    dqmc_sweep(&D);
    CHECK(D.status == 0);
    dqmc_sweep(&D);
    CHECK(D.status == 0);
    CHECK(file_contains_count(scale_path, " u forward ") == 2);
    check_udv_scale_phase21_file(scale_path);
    remove(scale_path);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    const char *centered_path = "/tmp/afqmc_test_udv_centered.dat";
    remove(centered_path);
    init_centered_diag_file(centered_path);
    lattice_chain(&L, Lx, -1.0, 1);
    model_init(&m, &L, 4.0, dtau, 1, 0.0);
    rng_seed(&r, 41);
    field_init(&f, Lx, 6, 4.0, dtau, &r);
    CHECK(dqmc_init_modes(&D, &m, &f, &r, 2, NULL,
                          DQMC_SWEEP_ALTERNATING,
                          GREEN_REBUILD_CENTERED) == 0);
    CHECK(dqmc_enable_udv_centered_diag(&D, centered_path, 2, 6, 3, 777ULL) ==
          0);
    CHECK(D.Gu.scale_observer != NULL);
    CHECK(dqmc_enable_udv_scale_diag(&D, NULL, 0, 0, 0, 0, 0.0, 0.0) == 0);
    CHECK(D.Gu.scale_observer != NULL);
    dqmc_sweep(&D);
    CHECK(D.status == 0);
    CHECK(file_contains_count(centered_path, " left_udv_forward ") == 1);

    UDV factor;
    udv_init(&factor, Lx);
    factor.D[0] = exp(1.0);
    factor.D[1] = exp(5.0);
    factor.D[2] = exp(1.0);
    factor.D[3] = -exp(1.0);
    factor.log_offset = 3.0;
    CHECK(D.Gu.scale_observer(D.Gu.scale_observer_ctx, &D.Gu,
                              "test_healthy", 1, 1, &factor) == 0);

    factor.D[0] = exp(-680.0);
    factor.D[1] = exp(680.0);
    factor.D[2] = 1.0;
    factor.D[3] = -1.0;
    factor.log_offset = 0.0;
    CHECK(D.Gu.scale_observer(D.Gu.scale_observer_ctx, &D.Gu,
                              "test_warning", 2, 1, &factor) == 0);

    const int reasons[] = {LINALG_FAILURE_CENTERED_RADIUS,
                           LINALG_FAILURE_CENTERED_UPDATE_MARGIN,
                           LINALG_FAILURE_EFFECTIVE_LOG_RANGE};
    const char *directions[] = {"test_centered_radius",
                                "test_centered_update",
                                "test_effective_range"};
    for (int i = 0; i < 3; i++) {
        D.Gu.work.failed = 1;
        D.Gu.work.failure_reason = reasons[i];
        CHECK(D.Gu.scale_observer(D.Gu.scale_observer_ctx, &D.Gu,
                                  directions[i], 3 + i, 2, &factor) == 0);
        D.Gu.work.failed = 0;
        D.Gu.work.failure_reason = LINALG_FAILURE_NONE;
    }

    UDV boundary_prefix;
    UDV boundary_suffix;
    UDV boundary_combined;
    udv_init(&boundary_prefix, Lx);
    udv_init(&boundary_suffix, Lx);
    udv_init(&boundary_combined, Lx);
    boundary_prefix.log_offset = 800.0;
    CHECK(green_from_boundary_factors(&D.Gu, &boundary_prefix,
                                      &boundary_suffix, &boundary_combined,
                                      0) != 0);
    CHECK(D.Gu.work.failure_reason == LINALG_FAILURE_EFFECTIVE_LOG_RANGE);
    CHECK(file_contains_count(centered_path,
                              " boundary_failed_prefix ") == 1);
    CHECK(file_contains_count(centered_path,
                              " boundary_failed_suffix ") == 1);
    CHECK(file_contains_count(centered_path,
                              " hard_fail_effective_log_range\n") == 3);
    D.Gu.work.failed = 0;
    D.Gu.work.failure_reason = LINALG_FAILURE_NONE;
    udv_free(&boundary_combined);
    udv_free(&boundary_suffix);
    udv_free(&boundary_prefix);

    factor.D[0] = exp(-705.0);
    factor.D[1] = exp(705.0);
    CHECK(D.Gu.scale_observer(D.Gu.scale_observer_ctx, &D.Gu,
                              "test_margin_hard_fail", 6, 3, &factor) != 0);
    CHECK(D.udv_centered_diag.failed != 0);
    check_centered_diag_file(centered_path);
    udv_free(&factor);
    remove(centered_path);
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    TEST_END();
}
