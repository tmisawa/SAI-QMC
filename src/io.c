#include "io.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int_value(const char *s, int *out)
{
    char *end = NULL;
    errno = 0;
    const long v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX) {
        return 1;
    }
    *out = (int)v;
    return 0;
}

static int parse_ull_value(const char *s, unsigned long long *out)
{
    char *end = NULL;
    errno = 0;
    if (s[0] == '-') {
        return 1;
    }
    const unsigned long long v = strtoull(s, &end, 10);
    if (errno || end == s || *end != '\0') {
        return 1;
    }
    *out = v;
    return 0;
}

static int parse_double_value(const char *s, double *out)
{
    char *end = NULL;
    errno = 0;
    const double v = strtod(s, &end);
    if (errno || end == s || *end != '\0' || !isfinite(v)) {
        return 1;
    }
    *out = v;
    return 0;
}

static int parse_beta_list(Params *p, char *val)
{
    p->nbeta = 0;
    char *tok = strtok(val, ",");
    if (tok == NULL) {
        return 1;
    }
    while (tok != NULL) {
        if (p->nbeta >= 64) {
            return 2;
        }
        if (parse_double_value(tok, &p->beta_list[p->nbeta]) ||
            p->beta_list[p->nbeta] <= 0.0) {
            return 1;
        }
        p->nbeta++;
        tok = strtok(NULL, ",");
    }
    return 0;
}

static void defaults(Params *p)
{
    strcpy(p->lattice, "chain");
    p->latfile[0] = '\0';
    p->Lx = 4;
    p->Ly = 1;
    p->pbc = 1;
    p->thop = -1.0;
    p->U = 4.0;
    p->dtau = 0.1;
    p->nbeta = 0;
    p->nwarm = 200;
    p->nmeas = 2000;
    p->nbin = 20;
    p->stab_interval = 8;
    strcpy(p->output_file, "observables.dat");
    p->profile = 0;
    p->profile_file[0] = '\0';
    p->stab_drift_file[0] = '\0';
    p->udv_scale_file[0] = '\0';
    p->udv_centered_file[0] = '\0';
    strcpy(p->sweep_order, "forward");
    strcpy(p->green_rebuild, "combine");
    strcpy(p->parallel, "serial");
    p->nrep = 1;
    p->replica_log[0] = '\0';
    strcpy(p->global_update, "none");
    p->global_interval = 100;
    p->replica_bin_file[0] = '\0';

    strcpy(p->szz_q, "none");
    strcpy(p->szz_file, "szz.dat");
    strcpy(p->sperp_q, "none");
    strcpy(p->sperp_file, "sperp.dat");
    strcpy(p->spin_consistency_file, "none");
    p->seed = 12345ULL;
}

static int parse_strict_string(const char *line, const char *key,
                               char out[256])
{
    const char *eq = strchr(line, '=');
    if (eq == NULL) {
        return 1;
    }

    const char *key_begin = line;
    while (isspace((unsigned char)*key_begin)) {
        key_begin++;
    }
    const char *key_end = eq;
    while (key_end > key_begin && isspace((unsigned char)key_end[-1])) {
        key_end--;
    }
    if ((size_t)(key_end - key_begin) != strlen(key) ||
        strncmp(key_begin, key, strlen(key)) != 0) {
        return 1;
    }

    const char *value_begin = eq + 1;
    while (isspace((unsigned char)*value_begin)) {
        value_begin++;
    }
    const char *value_end = value_begin + strlen(value_begin);
    while (value_end > value_begin && isspace((unsigned char)value_end[-1])) {
        value_end--;
    }
    const size_t len = (size_t)(value_end - value_begin);
    if (len == 0 || len >= 256) {
        return 1;
    }
    for (const char *p = value_begin; p < value_end; p++) {
        if (isspace((unsigned char)*p)) {
            return 1;
        }
    }
    memcpy(out, value_begin, len);
    out[len] = '\0';
    return 0;
}

static int is_strict_string_key(const char *key)
{
    return strcmp(key, "output_file") == 0 ||
           strcmp(key, "szz_q") == 0 || strcmp(key, "szz_file") == 0 ||
           strcmp(key, "sperp_q") == 0 || strcmp(key, "sperp_file") == 0 ||
           strcmp(key, "spin_consistency_file") == 0 ||
           strcmp(key, "global_update") == 0 ||
           strcmp(key, "global_interval") == 0 ||
           strcmp(key, "replica_bin_file") == 0;
}

int params_read(Params *p, const char *path)
{
    defaults(p);
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror(path);
        return 1;
    }

#define FAIL(...)                                                              \
    do {                                                                       \
        fprintf(stderr, __VA_ARGS__);                                          \
        fclose(fp);                                                            \
        return 1;                                                              \
    } while (0)

    char line[512];
    int line_number = 0;
    while (fgets(line, sizeof line, fp) != NULL) {
        line_number++;
        if (strchr(line, '\n') == NULL && !feof(fp)) {
            FAIL("ERROR: input line %d exceeds %zu bytes\n", line_number,
                 sizeof line - 1);
        }
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }

        char key[64];
        char val[256];
        if (sscanf(line, "%63[^= \t]=%255s", key, val) != 2) {
            char probe[64];
            if (sscanf(line, " %63[^= \t]", probe) == 1 &&
                is_strict_string_key(probe)) {
                FAIL("ERROR: invalid %s value on line %d\n", probe,
                     line_number);
            }
            continue;
        }

        if (is_strict_string_key(key)) {
            if (parse_strict_string(line, key, val) != 0) {
                FAIL("ERROR: invalid %s value on line %d\n", key,
                     line_number);
            }
        }

        if (strcmp(key, "lattice") == 0) {
            strncpy(p->lattice, val, sizeof p->lattice - 1);
            p->lattice[sizeof p->lattice - 1] = '\0';
        } else if (strcmp(key, "latfile") == 0) {
            strncpy(p->latfile, val, sizeof p->latfile - 1);
            p->latfile[sizeof p->latfile - 1] = '\0';
        } else if (strcmp(key, "Lx") == 0) {
            if (parse_int_value(val, &p->Lx)) {
                FAIL("ERROR: Lx must be integer (got %s)\n", val);
            }
        } else if (strcmp(key, "Ly") == 0) {
            if (parse_int_value(val, &p->Ly)) {
                FAIL("ERROR: Ly must be integer (got %s)\n", val);
            }
        } else if (strcmp(key, "pbc") == 0) {
            if (parse_int_value(val, &p->pbc)) {
                FAIL("ERROR: pbc must be 0 or 1 (got %s)\n", val);
            }
        } else if (strcmp(key, "bc") == 0) {
            if (strcmp(val, "periodic") == 0 || strcmp(val, "pbc") == 0 ||
                strcmp(val, "1") == 0) {
                p->pbc = 1;
            } else if (strcmp(val, "open") == 0 || strcmp(val, "obc") == 0 ||
                       strcmp(val, "0") == 0) {
                p->pbc = 0;
            } else {
                FAIL("ERROR: bc must be periodic/open/pbc/obc/1/0 (got %s)\n",
                     val);
            }
        } else if (strcmp(key, "t") == 0) {
            if (parse_double_value(val, &p->thop)) {
                FAIL("ERROR: t must be numeric (got %s)\n", val);
            }
        } else if (strcmp(key, "U") == 0) {
            if (parse_double_value(val, &p->U)) {
                FAIL("ERROR: U must be numeric (got %s)\n", val);
            }
        } else if (strcmp(key, "dtau") == 0) {
            if (parse_double_value(val, &p->dtau)) {
                FAIL("ERROR: dtau must be numeric (got %s)\n", val);
            }
        } else if (strcmp(key, "nwarm") == 0) {
            if (parse_int_value(val, &p->nwarm)) {
                FAIL("ERROR: nwarm must be integer (got %s)\n", val);
            }
        } else if (strcmp(key, "nmeas") == 0) {
            if (parse_int_value(val, &p->nmeas)) {
                FAIL("ERROR: nmeas must be integer (got %s)\n", val);
            }
        } else if (strcmp(key, "nbin") == 0) {
            if (parse_int_value(val, &p->nbin)) {
                FAIL("ERROR: nbin must be integer (got %s)\n", val);
            }
        } else if (strcmp(key, "stab") == 0 ||
                   strcmp(key, "stabilize_interval") == 0) {
            if (parse_int_value(val, &p->stab_interval)) {
                FAIL("ERROR: stab must be integer (got %s)\n", val);
            }
        } else if (strcmp(key, "output_file") == 0) {
            memcpy(p->output_file, val, strlen(val) + 1);
        } else if (strcmp(key, "profile") == 0) {
            if (parse_int_value(val, &p->profile)) {
                FAIL("ERROR: profile must be 0 or 1 (got %s)\n", val);
            }
        } else if (strcmp(key, "profile_file") == 0) {
            strncpy(p->profile_file, val, sizeof p->profile_file - 1);
            p->profile_file[sizeof p->profile_file - 1] = '\0';
        } else if (strcmp(key, "stab_drift_file") == 0 ||
                   strcmp(key, "stabilization_drift_file") == 0) {
            strncpy(p->stab_drift_file, val, sizeof p->stab_drift_file - 1);
            p->stab_drift_file[sizeof p->stab_drift_file - 1] = '\0';
        } else if (strcmp(key, "udv_scale_file") == 0 ||
                   strcmp(key, "udv_scale_diagnostics_file") == 0) {
            strncpy(p->udv_scale_file, val, sizeof p->udv_scale_file - 1);
            p->udv_scale_file[sizeof p->udv_scale_file - 1] = '\0';
        } else if (strcmp(key, "udv_centered_file") == 0 ||
                   strcmp(key, "udv_centered_diagnostics_file") == 0) {
            strncpy(p->udv_centered_file, val,
                    sizeof p->udv_centered_file - 1);
            p->udv_centered_file[sizeof p->udv_centered_file - 1] = '\0';
        } else if (strcmp(key, "sweep_order") == 0) {
            strncpy(p->sweep_order, val, sizeof p->sweep_order - 1);
            p->sweep_order[sizeof p->sweep_order - 1] = '\0';
        } else if (strcmp(key, "green_rebuild") == 0) {
            strncpy(p->green_rebuild, val, sizeof p->green_rebuild - 1);
            p->green_rebuild[sizeof p->green_rebuild - 1] = '\0';
        } else if (strcmp(key, "parallel") == 0) {
            strncpy(p->parallel, val, sizeof p->parallel - 1);
            p->parallel[sizeof p->parallel - 1] = '\0';
        } else if (strcmp(key, "nrep") == 0) {
            if (parse_int_value(val, &p->nrep)) {
                FAIL("ERROR: nrep must be integer (got %s)\n", val);
            }
        } else if (strcmp(key, "replica_log") == 0) {
            strncpy(p->replica_log, val, sizeof p->replica_log - 1);
            p->replica_log[sizeof p->replica_log - 1] = '\0';
        } else if (strcmp(key, "global_update") == 0) {
            if (strlen(val) >= sizeof p->global_update) {
                FAIL("ERROR: global_update must be none or site (got %s)\n", val);
            }
            strcpy(p->global_update, val);
        } else if (strcmp(key, "global_interval") == 0) {
            if (parse_int_value(val, &p->global_interval) ||
                p->global_interval <= 0) {
                FAIL("ERROR: global_interval must be a positive integer (got %s)\n",
                     val);
            }
        } else if (strcmp(key, "replica_bin_file") == 0) {
            memcpy(p->replica_bin_file, val, strlen(val) + 1); /* val < 256 by the strict parse */
        } else if (strcmp(key, "szz_q") == 0) {
            memcpy(p->szz_q, val, strlen(val) + 1);
        } else if (strcmp(key, "szz_file") == 0) {
            memcpy(p->szz_file, val, strlen(val) + 1);
        } else if (strcmp(key, "sperp_q") == 0) {
            memcpy(p->sperp_q, val, strlen(val) + 1);
        } else if (strcmp(key, "sperp_file") == 0) {
            memcpy(p->sperp_file, val, strlen(val) + 1);
        } else if (strcmp(key, "spin_consistency_file") == 0) {
            memcpy(p->spin_consistency_file, val, strlen(val) + 1);
        } else if (strcmp(key, "seed") == 0) {
            if (parse_ull_value(val, &p->seed)) {
                FAIL("ERROR: seed must be unsigned integer (got %s)\n", val);
            }
        } else if (strcmp(key, "beta_list") == 0) {
            const int rc = parse_beta_list(p, val);
            if (rc == 2) {
                FAIL("ERROR: beta_list has too many entries (max 64)\n");
            }
            if (rc != 0) {
                FAIL("ERROR: beta_list must contain positive numeric values\n");
            }
        } else {
            FAIL("ERROR: unknown key %s\n", key);
        }
    }
    fclose(fp);
#undef FAIL

    if (p->nbeta == 0) {
        p->beta_list[0] = 2.0;
        p->nbeta = 1;
    }
    if (strcmp(p->lattice, "chain") != 0 &&
        strcmp(p->lattice, "square") != 0 &&
        strcmp(p->lattice, "file") != 0) {
        fprintf(stderr,
                "ERROR: lattice must be chain, square, or file (got %s)\n",
                p->lattice);
        return 1;
    }
    if (p->Lx <= 0) {
        fprintf(stderr, "ERROR: Lx must be > 0\n");
        return 1;
    }
    if (p->Ly <= 0) {
        fprintf(stderr, "ERROR: Ly must be > 0\n");
        return 1;
    }
    if (p->pbc != 0 && p->pbc != 1) {
        fprintf(stderr, "ERROR: pbc must be 0 or 1\n");
        return 1;
    }
    if (p->U < 0.0) {
        fprintf(stderr, "ERROR: U must be >= 0\n");
        return 1;
    }
    if (p->dtau <= 0.0) {
        fprintf(stderr, "ERROR: dtau must be > 0\n");
        return 1;
    }
    if (p->nwarm < 0) {
        fprintf(stderr, "ERROR: nwarm must be >= 0\n");
        return 1;
    }
    if (p->stab_interval <= 0) {
        fprintf(stderr, "ERROR: stab must be > 0\n");
        return 1;
    }
    if (p->profile != 0 && p->profile != 1) {
        fprintf(stderr, "ERROR: profile must be 0 or 1 (got %d)\n",
                p->profile);
        return 1;
    }
    if (strcmp(p->sweep_order, "forward") != 0 &&
        strcmp(p->sweep_order, "alternating") != 0) {
        fprintf(stderr,
                "ERROR: sweep_order must be forward or alternating (got %s)\n",
                p->sweep_order);
        return 1;
    }
    if (strcmp(p->global_update, "none") != 0 &&
        strcmp(p->global_update, "site") != 0) {
        fprintf(stderr, "ERROR: global_update must be none or site (got %s)\n",
                p->global_update);
        return 1;
    }

    if (strcmp(p->green_rebuild, "combine") != 0 &&
        strcmp(p->green_rebuild, "two_sided") != 0 &&
        strcmp(p->green_rebuild, "centered") != 0) {
        fprintf(stderr,
                "ERROR: green_rebuild must be combine, two_sided, or centered "
                "(got %s)\n",
                p->green_rebuild);
        return 1;
    }
    if (strcmp(p->parallel, "serial") != 0 &&
        strcmp(p->parallel, "omp") != 0 &&
        strcmp(p->parallel, "mpi") != 0 &&
        strcmp(p->parallel, "hybrid") != 0) {
        fprintf(stderr,
                "ERROR: parallel must be serial, omp, mpi, or hybrid (got %s)\n",
                p->parallel);
        return 1;
    }
    if (p->nrep < 1) {
        fprintf(stderr, "ERROR: nrep must be >= 1 (got %d)\n", p->nrep);
        return 1;
    }
    if (p->nbin < 2) {
        fprintf(stderr, "ERROR: nbin must be >= 2 (got %d)\n", p->nbin);
        return 1;
    }
    if (p->nmeas < p->nbin) {
        fprintf(stderr, "ERROR: nmeas (%d) must be >= nbin (%d)\n", p->nmeas,
                p->nbin);
        return 1;
    }
    if (p->nmeas % p->nbin != 0) {
        fprintf(stderr, "ERROR: nmeas (%d) must be divisible by nbin (%d)\n",
                p->nmeas, p->nbin);
        return 1;
    }
    return 0;
}
