---
date: 2026-06-25
datetime: 2026-06-25 14:24 JST
model: Codex (GPT-5)
status: plan
topic: DQMC profiling timer の TDD 実装計画
summary: |
  `profile=1` で有効化する DQMC profiler/timer の実装計画。
  CSV 出力、beta/phase/region 別の inclusive timing、main/dqmc/green/linalg の instrumentation を
  TDD の小タスクへ分解する。
---

# DQMC Profiling Timer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `profile=1` のときだけ DQMC 実行時間を beta/phase/region 別に CSV 出力し、通常実行の stdout と物理計算を変えない。

**Architecture:** `src/profiler.h` / `src/profiler.c` に opt-in profiler を追加し、`main.c` が beta/phase を管理する。`Dqmc` は `Profiler *` を保持し、`green.c` と `linalg.c` は `profiler_current()` 経由で既存 API を崩さずに深い関数を計測する。region time は inclusive として記録し、CSV の `frac_beta` は `beta_total` に対する比にする。

**Tech Stack:** C11, GNU Make, existing LAPACK/BLAS linkage, existing `tests/test_*.c` standalone test pattern.

**Reference:** `docs/superpowers/specs/2026-06-25-dqmc-profiling-timer-design.md`

---

## Scope Check

この plan は profiler/timer だけを扱う。外部 profiler 連携、flamegraph、thread safety、plot script は含めない。CSV を作るところまでを実装し、plot は後続タスクに分ける。

Commit examples in this plan use `Codex (GPT-5)` because this plan was written
for the current Codex session. If a different model executes the plan, replace
the `Co-Authored-By` trailer with the actual model used, following `AGENTS.md`.

## File Map

- Create: `src/profiler.h`
  - profiler の enum、struct、API、hot path 用 macro を定義する。
- Create: `src/profiler.c`
  - clock、CSV file open/write/close、beta counter reset、phase/region counter、current profiler pointer を実装する。
- Create: `tests/test_profiler.c`
  - profiler enabled/disabled、CSV header、phase/region rows、current profiler pointer を検証する。
- Modify: `src/io.h`
  - `Params.profile` と `Params.profile_file` を追加する。
- Modify: `src/io.c`
  - `profile` / `profile_file` parser、default、validation を追加する。
- Modify: `tests/test_io.c`
  - profile parser の成功・失敗ケースを追加する。
- Modify: `src/main.c`
  - `Profiler prof` の lifetime、beta begin/end、phase set、main-level region timing を追加する。
- Modify: `src/dqmc.h`, `src/dqmc.c`
  - `Dqmc.prof` と `dqmc_init(Dqmc*, Model*, Field*, Rng*, int, Profiler*)` を追加し、`dqmc_sweep` を計測する。
- Modify: `src/green.c`
  - `green_from_scratch`, `green_wrap`, `green_update` を current profiler で計測する。
- Modify: `src/linalg.c`
  - `la_gemm`, `la_inverse`, `la_expm_sym`, `udv_lmul`, `udv_inv_one_plus` を current profiler で計測する。
- Modify: existing `tests/test_*.c`
  - `dqmc_init` 呼び出しへ `NULL` profiler を渡す。

## Task 1: Profiler Core

**Files:**
- Create: `src/profiler.h`
- Create: `src/profiler.c`
- Create: `tests/test_profiler.c`

- [ ] **Step 1: Write the failing profiler unit test**

Create `tests/test_profiler.c`:

```c
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
    const char *enabled_path = "/tmp/afqmc_profile_enabled.csv";
    const char *disabled_path = "/tmp/afqmc_profile_disabled.csv";
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
    profiler_beta_end(&prof);
    profiler_set_current(NULL);
    CHECK(profiler_current() == NULL);
    profiler_close(&prof);
    CHECK(!profiler_error(&prof));

    CHECK(file_contains(enabled_path,
                        "beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta"));
    CHECK(file_contains(enabled_path, "warmup,dqmc_sweep,1,"));
    CHECK(file_contains(enabled_path, "measurement,measure_sample,1,"));
    CHECK(file_contains(enabled_path, "all,dqmc_sweep,1,"));
    CHECK(file_contains(enabled_path, "all,beta_total,1,"));

    TEST_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
make tests/test_profiler
```

Expected: compile failure because `src/profiler.h` does not exist.

- [ ] **Step 3: Add the profiler API**

Create `src/profiler.h`:

```c
#ifndef PROFILER_H
#define PROFILER_H

#include <stdio.h>

typedef enum {
    PROF_PHASE_SETUP = 0,
    PROF_PHASE_WARMUP,
    PROF_PHASE_MEASUREMENT,
    PROF_PHASE_FINALIZE,
    PROF_PHASE_ALL,
    PROF_PHASE_COUNT
} ProfPhase;

typedef enum {
    PROF_BETA_TOTAL = 0,
    PROF_MODEL_INIT,
    PROF_FIELD_INIT,
    PROF_DQMC_INIT,
    PROF_DQMC_SWEEP,
    PROF_GREEN_FROM_SCRATCH,
    PROF_GREEN_WRAP,
    PROF_GREEN_UPDATE,
    PROF_MEASURE_SAMPLE,
    PROF_JACKKNIFE,
    PROF_UDV_LMUL,
    PROF_UDV_INV_ONE_PLUS,
    PROF_LA_GEMM,
    PROF_LA_INVERSE,
    PROF_LA_EXPM_SYM,
    PROF_REGION_COUNT
} ProfRegion;

typedef struct {
    unsigned long long calls;
    double total_sec;
} ProfStat;

typedef struct {
    int enabled;
    int error;
    FILE *fp;
    double beta;
    double T;
    double dtau;
    int Ltr;
    double beta_start_sec;
    ProfPhase phase;
    ProfStat stat[PROF_PHASE_COUNT][PROF_REGION_COUNT];
} Profiler;

void profiler_init(Profiler *p, int enabled, const char *path);
void profiler_close(Profiler *p);
int profiler_error(const Profiler *p);
void profiler_beta_begin(Profiler *p, double beta, double T, double dtau, int Ltr);
void profiler_beta_end(Profiler *p);
void profiler_phase_set(Profiler *p, ProfPhase phase);
double profiler_now(void);
void profiler_add(Profiler *p, ProfRegion region, double elapsed_sec);
void profiler_set_current(Profiler *p);
Profiler *profiler_current(void);

static inline double profiler_now_if_enabled(const Profiler *p)
{
    return (p != NULL && p->enabled) ? profiler_now() : 0.0;
}

static inline void profiler_add_elapsed(Profiler *p, ProfRegion region,
                                        double start_sec)
{
    if (p == NULL || !p->enabled) {
        return;
    }
    profiler_add(p, region, profiler_now() - start_sec);
}

#define PROF_BEGIN(prof, var) double var = profiler_now_if_enabled((prof))
#define PROF_END(prof, region, var) profiler_add_elapsed((prof), (region), (var))

#endif
```

The two hot path helpers are `static inline` in the header so `profile=0`
collapses to one inlined branch at each timing point. `profiler_now()` and
`profiler_add()` stay out-of-line because they are only reached when profiling
is enabled.

- [ ] **Step 4: Implement CSV writing and counters**

Create `src/profiler.c`:

```c
#define _POSIX_C_SOURCE 199309L
#include "profiler.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static Profiler *g_current_profiler = NULL;

static const char *phase_name(ProfPhase phase)
{
    static const char *names[PROF_PHASE_COUNT] = {
        "setup", "warmup", "measurement", "finalize", "all"};
    return (phase >= 0 && phase < PROF_PHASE_COUNT) ? names[phase] : "unknown";
}

static const char *region_name(ProfRegion region)
{
    static const char *names[PROF_REGION_COUNT] = {
        "beta_total",
        "model_init",
        "field_init",
        "dqmc_init",
        "dqmc_sweep",
        "green_from_scratch",
        "green_wrap",
        "green_update",
        "measure_sample",
        "jackknife",
        "udv_lmul",
        "udv_inv_one_plus",
        "la_gemm",
        "la_inverse",
        "la_expm_sym"};
    return (region >= 0 && region < PROF_REGION_COUNT) ? names[region] : "unknown";
}

static void clear_stats(Profiler *p)
{
    memset(p->stat, 0, sizeof(p->stat));
}

double profiler_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

void profiler_init(Profiler *p, int enabled, const char *path)
{
    memset(p, 0, sizeof(*p));
    p->enabled = enabled ? 1 : 0;
    p->phase = PROF_PHASE_SETUP;
    if (!p->enabled) {
        return;
    }

    const char *out = (path != NULL && path[0] != '\0') ? path : "profile.csv";
    p->fp = fopen(out, "w");
    if (p->fp == NULL) {
        p->error = 1;
        return;
    }
    fprintf(p->fp,
            "beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta\n");
    if (ferror(p->fp)) {
        p->error = 1;
    }
}

void profiler_close(Profiler *p)
{
    if (p == NULL || !p->enabled || p->fp == NULL) {
        return;
    }
    if (ferror(p->fp)) {
        p->error = 1;
    }
    if (fclose(p->fp) != 0) {
        p->error = 1;
    }
    p->fp = NULL;
}

int profiler_error(const Profiler *p)
{
    return (p != NULL) ? p->error : 0;
}

void profiler_beta_begin(Profiler *p, double beta, double T, double dtau, int Ltr)
{
    if (p == NULL || !p->enabled) {
        return;
    }
    clear_stats(p);
    p->beta = beta;
    p->T = T;
    p->dtau = dtau;
    p->Ltr = Ltr;
    p->phase = PROF_PHASE_SETUP;
    p->beta_start_sec = profiler_now();
}

void profiler_phase_set(Profiler *p, ProfPhase phase)
{
    if (p == NULL || !p->enabled) {
        return;
    }
    if (phase >= 0 && phase < PROF_PHASE_COUNT) {
        p->phase = phase;
    }
}

void profiler_add(Profiler *p, ProfRegion region, double elapsed_sec)
{
    if (p == NULL || !p->enabled || region < 0 || region >= PROF_REGION_COUNT) {
        return;
    }
    if (elapsed_sec < 0.0) {
        return;
    }

    ProfPhase phase = p->phase;
    if (phase < 0 || phase >= PROF_PHASE_COUNT) {
        phase = PROF_PHASE_SETUP;
    }
    p->stat[phase][region].calls++;
    p->stat[phase][region].total_sec += elapsed_sec;

    if (phase != PROF_PHASE_ALL) {
        p->stat[PROF_PHASE_ALL][region].calls++;
        p->stat[PROF_PHASE_ALL][region].total_sec += elapsed_sec;
    }
}

void profiler_beta_end(Profiler *p)
{
    if (p == NULL || !p->enabled || p->fp == NULL) {
        return;
    }

    const double beta_total = profiler_now() - p->beta_start_sec;
    p->stat[PROF_PHASE_ALL][PROF_BETA_TOTAL].calls = 1;
    p->stat[PROF_PHASE_ALL][PROF_BETA_TOTAL].total_sec = beta_total;

    for (int ph = 0; ph < PROF_PHASE_COUNT; ph++) {
        for (int rg = 0; rg < PROF_REGION_COUNT; rg++) {
            const ProfStat s = p->stat[ph][rg];
            if (s.calls == 0) {
                continue;
            }
            const double avg = s.total_sec / (double)s.calls;
            const double frac = (beta_total > 0.0) ? s.total_sec / beta_total : 0.0;
            fprintf(p->fp, "%.17g,%.17g,%.17g,%d,%s,%s,%llu,%.17g,%.17g,%.17g\n",
                    p->beta, p->T, p->dtau, p->Ltr, phase_name((ProfPhase)ph),
                    region_name((ProfRegion)rg), s.calls, s.total_sec, avg, frac);
        }
    }
    fflush(p->fp);
    if (ferror(p->fp)) {
        p->error = 1;
    }
}

void profiler_set_current(Profiler *p)
{
    g_current_profiler = p;
}

Profiler *profiler_current(void)
{
    return g_current_profiler;
}
```

- [ ] **Step 5: Run the profiler test**

Run:

```bash
make tests/test_profiler && ./tests/test_profiler
```

Expected: `OK`

- [ ] **Step 6: Run the full test suite**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 7: Commit**

```bash
git add src/profiler.h src/profiler.c tests/test_profiler.c
git commit -m "feat(profiler): add CSV timing core" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 2: Input Parser Support

**Files:**
- Modify: `src/io.h`
- Modify: `src/io.c`
- Modify: `tests/test_io.c`

- [ ] **Step 1: Extend the IO test first**

Modify `tests/test_io.c` by adding checks after the first valid input parse:

```c
    CHECK(p.profile == 0);
    CHECK(p.profile_file[0] == '\0');
```

Then add these cases before `TEST_END();`:

```c
    write_text("/tmp/afqmc_profile.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "profile=1\nprofile_file=/tmp/afqmc_profile.csv\n");
    CHECK(params_read(&p, "/tmp/afqmc_profile.in") == 0);
    CHECK(p.profile == 1);
    CHECK(strcmp(p.profile_file, "/tmp/afqmc_profile.csv") == 0);

    write_text("/tmp/afqmc_badprofile.in",
               "lattice=chain\nnmeas=20\nnbin=10\nprofile=2\n");
    CHECK(params_read(&p, "/tmp/afqmc_badprofile.in") != 0);

    write_text("/tmp/afqmc_badprofile_text.in",
               "lattice=chain\nnmeas=20\nnbin=10\nprofile=yes\n");
    CHECK(params_read(&p, "/tmp/afqmc_badprofile_text.in") != 0);
```

Add `#include <string.h>` near the top of `tests/test_io.c`:

```c
#include <string.h>
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
make tests/test_io && ./tests/test_io
```

Expected: compile failure because `Params` has no `profile` or `profile_file`.

- [ ] **Step 3: Add fields to Params**

Modify `src/io.h`:

```c
typedef struct {
    char lattice[16];
    char latfile[256];
    int Lx;
    int Ly;
    int pbc;
    double thop;
    double U;
    double dtau;
    double beta_list[64];
    int nbeta;
    int nwarm;
    int nmeas;
    int nbin;
    int stab_interval;
    int profile;
    char profile_file[256];
    unsigned long long seed;
} Params;
```

- [ ] **Step 4: Parse and validate profile keys**

Modify `defaults` in `src/io.c`:

```c
    p->stab_interval = 8;
    p->profile = 0;
    p->profile_file[0] = '\0';
    p->seed = 12345ULL;
```

Add parser branches before `seed`:

```c
        } else if (strcmp(key, "profile") == 0) {
            if (parse_int_value(val, &p->profile)) {
                FAIL("ERROR: profile must be 0 or 1 (got %s)\n", val);
            }
        } else if (strcmp(key, "profile_file") == 0) {
            strncpy(p->profile_file, val, sizeof p->profile_file - 1);
            p->profile_file[sizeof p->profile_file - 1] = '\0';
```

Add validation before `return 0;`:

```c
    if (p->profile != 0 && p->profile != 1) {
        fprintf(stderr, "ERROR: profile must be 0 or 1 (got %d)\n", p->profile);
        return 1;
    }
```

- [ ] **Step 5: Run IO test**

Run:

```bash
make tests/test_io && ./tests/test_io
```

Expected: `OK`

- [ ] **Step 6: Run full test suite**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 7: Commit**

```bash
git add src/io.h src/io.c tests/test_io.c
git commit -m "feat(io): parse profiler options" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 3: Main-Level Profiling Lifecycle

**Files:**
- Modify: `src/main.c`
- Test: manual profile-on/profile-off run

- [ ] **Step 1: Include profiler and initialize it after lattice validation**

Modify includes in `src/main.c`:

```c
#include "profiler.h"
```

Initialize the profiler after lattice construction and the non-bipartite guard,
immediately before the output header. This keeps profiler cleanup out of the
early lattice error paths.

```c
    Profiler prof;
    profiler_init(&prof, p.profile, p.profile_file);
    if (profiler_error(&prof)) {
        fprintf(stderr, "ERROR: failed to initialize profiler output\n");
        lattice_free(&L);
        return 1;
    }
    profiler_set_current(&prof);
```

The only planned early return after profiler initialization is the `beta/dtau`
integer check. Replace that error path with:

```c
            profiler_set_current(NULL);
            profiler_close(&prof);
            if (profiler_error(&prof)) {
                fprintf(stderr, "ERROR: failed to close profiler output\n");
            }
            lattice_free(&L);
            return 1;
```

Do not initialize the profiler before `lattice_from_file`, unknown lattice,
or non-bipartite checks; those paths should remain unchanged.

The end of `main` should not contain a second `lattice_free(&L)`. The final
cleanup block is:

```c
    profiler_set_current(NULL);
    profiler_close(&prof);
    lattice_free(&L);
    if (profiler_error(&prof)) {
        return 1;
    }
    return 0;
```

- [ ] **Step 2: Add beta begin/end and setup timings**

Inside the beta loop, after `T` is computed:

```c
        profiler_beta_begin(&prof, beta, T, p.dtau, Ltr);
        profiler_phase_set(&prof, PROF_PHASE_SETUP);
```

Wrap setup calls:

```c
        PROF_BEGIN(&prof, t_model_init);
        model_init(&m, &L, p.U, p.dtau, 1, 0.0);
        PROF_END(&prof, PROF_MODEL_INIT, t_model_init);
```

Use the same pattern for `field_init` and `dqmc_init`:

```c
        PROF_BEGIN(&prof, t_field_init);
        field_init(&f, L.n, Ltr, p.U, p.dtau, &r);
        PROF_END(&prof, PROF_FIELD_INIT, t_field_init);

        PROF_BEGIN(&prof, t_dqmc_init);
        dqmc_init(&D, &m, &f, &r, p.stab_interval, &prof);
        PROF_END(&prof, PROF_DQMC_INIT, t_dqmc_init);
```

This step also changes `dqmc_init` call to the new signature that Task 4 will implement. If implementing Task 3 before Task 4, temporarily pass the old signature and move the signature change to Task 4. The final code must use the six-argument signature above.

- [ ] **Step 3: Add warmup, measurement, and finalize phases**

Before warmup loop:

```c
        profiler_phase_set(&prof, PROF_PHASE_WARMUP);
```

Before measurement loop:

```c
        profiler_phase_set(&prof, PROF_PHASE_MEASUREMENT);
```

Wrap `measure_sample`:

```c
                PROF_BEGIN(&prof, t_measure_sample);
                MeasSample s = measure_sample(L.n, L.t, p.U, D.Gu.g, D.Gd.g);
                PROF_END(&prof, PROF_MEASURE_SAMPLE, t_measure_sample);
```

Before jackknife calls:

```c
        profiler_phase_set(&prof, PROF_PHASE_FINALIZE);
        PROF_BEGIN(&prof, t_jackknife);
        double Eh, Ehe, Eg, Ege, Ep, Epe, Nm, Ne, Dm, De, Sm, Se;
        jackknife(Ehub, p.nbin, &Eh, &Ehe);
        jackknife(Egc, p.nbin, &Eg, &Ege);
        jackknife(Eph, p.nbin, &Ep, &Epe);
        jackknife(Nbin, p.nbin, &Nm, &Ne);
        jackknife(Dbin, p.nbin, &Dm, &De);
        jackknife(Sbin, p.nbin, &Sm, &Se);
        PROF_END(&prof, PROF_JACKKNIFE, t_jackknife);
```

Call `profiler_beta_end(&prof);` after all per-beta cleanup is complete:

```c
        dqmc_free(&D);
        field_free(&f);
        model_free(&m);
        profiler_beta_end(&prof);
```

- [ ] **Step 4: Build to expose signature work**

Run:

```bash
make dqmc
```

Expected at this point: compile failure if `dqmc_init` signature has not been updated yet. Continue with Task 4 before expecting a clean build.

Do not commit this task independently until Task 4 is complete, because Task 3 and Task 4 share the `dqmc_init` API change.

## Task 4: Dqmc Profiler Wiring

**Files:**
- Modify: `src/dqmc.h`
- Modify: `src/dqmc.c`
- Modify: `tests/test_dqmc.c`
- Modify: `tests/test_integration.c`

- [ ] **Step 1: Add Profiler pointer to Dqmc**

Modify `src/dqmc.h`:

```c
#include "profiler.h"
```

Replace the `Dqmc` struct and function declaration with:

```c
typedef struct {
    int n;
    int L;
    Model *m;
    Field *f;
    Green Gu;
    Green Gd;
    Rng *rng;
    int stab_interval;
    double sign;
    Profiler *prof;
} Dqmc;

void dqmc_init(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval,
               Profiler *prof);
void dqmc_free(Dqmc *D);
void dqmc_sweep(Dqmc *D);
```

- [ ] **Step 2: Update dqmc_init and dqmc_sweep**

Modify `src/dqmc.c` signature and body:

```c
void dqmc_init(Dqmc *D, Model *m, Field *f, Rng *rng, int stab_interval,
               Profiler *prof)
{
    D->n = m->n;
    D->L = f->L;
    D->m = m;
    D->f = f;
    D->rng = rng;
    D->stab_interval = (stab_interval > 0) ? stab_interval : 8;
    D->prof = prof;
    green_alloc(&D->Gu, m, f, 1.0);
    green_alloc(&D->Gd, m, f, -1.0);
    green_from_scratch(&D->Gu, 0);
    green_from_scratch(&D->Gd, 0);
    D->sign = 1.0;
}
```

Wrap `dqmc_sweep`:

```c
void dqmc_sweep(Dqmc *D)
{
    PROF_BEGIN(D->prof, t_sweep);
    const int n = D->n;
    const int L = D->L;

    for (int l = 0; l < L; l++) {
        for (int i = 0; i < n; i++) {
            const double Nu = green_flipN(&D->Gu, i);
            const double Nd = green_flipN(&D->Gd, i);
            const double Ru = green_ratio_N(&D->Gu, i, Nu);
            const double Rd = green_ratio_N(&D->Gd, i, Nd);
            const double R = Ru * Rd;

            if (rng_double(D->rng) < fabs(R)) {
                green_update(&D->Gu, i, Nu);
                green_update(&D->Gd, i, Nd);
                D->f->s[l * n + i] *= -1;
                if (R < 0.0) {
                    D->sign = -D->sign;
                }
            }
        }

        green_wrap(&D->Gu);
        green_wrap(&D->Gd);
        if (((l + 1) % D->stab_interval) == 0) {
            const int nl = (l + 1) % L;
            green_from_scratch(&D->Gu, nl);
            green_from_scratch(&D->Gd, nl);
        }
    }

    green_from_scratch(&D->Gu, 0);
    green_from_scratch(&D->Gd, 0);
    PROF_END(D->prof, PROF_DQMC_SWEEP, t_sweep);
}
```

- [ ] **Step 3: Update tests to pass NULL profiler**

In every test that calls `dqmc_init`, add the final `NULL` argument.

Examples:

```c
dqmc_init(&D, &m, &f, &r, 2, NULL);
```

and

```c
dqmc_init(&D, &m, &f, &r, 8, NULL);
```

Find all call sites:

```bash
rg -n "dqmc_init\\(" src tests
```

Expected call sites after update:

```text
src/dqmc.c
src/main.c
tests/test_dqmc.c
tests/test_integration.c
```

- [ ] **Step 4: Run targeted tests**

Run:

```bash
make tests/test_dqmc tests/test_integration
./tests/test_dqmc
./tests/test_integration
```

Expected: both print `OK`.

- [ ] **Step 5: Run full tests and build dqmc**

Run:

```bash
make test
make dqmc
```

Expected:

```text
ALL TESTS PASSED
```

and `make dqmc` exits successfully.

- [ ] **Step 6: Commit Task 3 and Task 4 together**

```bash
git add src/main.c src/dqmc.h src/dqmc.c tests/test_dqmc.c tests/test_integration.c
git commit -m "feat(profiler): wire beta and sweep timing" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 5: Green Function Instrumentation

**Files:**
- Modify: `src/green.c`

- [ ] **Step 1: Include profiler**

Add to `src/green.c`:

```c
#include "profiler.h"
```

- [ ] **Step 2: Wrap green_from_scratch**

At the start of `green_from_scratch`:

```c
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_from_scratch);
```

Before the function returns:

```c
    PROF_END(prof, PROF_GREEN_FROM_SCRATCH, t_green_from_scratch);
```

The final block should be:

```c
    udv_free(&udv);
    free(B);
    PROF_END(prof, PROF_GREEN_FROM_SCRATCH, t_green_from_scratch);
}
```

- [ ] **Step 3: Wrap green_update**

At the start of `green_update`:

```c
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_update);
```

At the end:

```c
    free(v);
    free(u);
    PROF_END(prof, PROF_GREEN_UPDATE, t_green_update);
}
```

- [ ] **Step 4: Wrap green_wrap**

At the start of `green_wrap`:

```c
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_green_wrap);
```

At the end:

```c
    free(tmp);
    free(Binv);
    free(B);
    PROF_END(prof, PROF_GREEN_WRAP, t_green_wrap);
}
```

- [ ] **Step 5: Run Green and DQMC tests**

Run:

```bash
make tests/test_green_init tests/test_green_update tests/test_green_wrap tests/test_dqmc
./tests/test_green_init
./tests/test_green_update
./tests/test_green_wrap
./tests/test_dqmc
```

Expected: all print `OK`.

- [ ] **Step 6: Run full tests**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 7: Commit**

```bash
git add src/green.c
git commit -m "feat(profiler): instrument Green operations" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 6: Linear Algebra Instrumentation

**Files:**
- Modify: `src/linalg.c`

- [ ] **Step 1: Include profiler**

Add to `src/linalg.c`:

```c
#include "profiler.h"
```

- [ ] **Step 2: Wrap la_gemm**

Replace `la_gemm` body with:

```c
void la_gemm(int n, int ta, int tb, double alpha, const double *A,
             const double *B, double beta, double *C)
{
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_la_gemm);
    const char transa = ta ? 'T' : 'N';
    const char transb = tb ? 'T' : 'N';
    dgemm_(&transa, &transb, &n, &n, &n, &alpha, A, &n, B, &n, &beta, C,
           &n);
    PROF_END(prof, PROF_LA_GEMM, t_la_gemm);
}
```

- [ ] **Step 3: Wrap la_inverse**

At the start of `la_inverse`:

```c
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_la_inverse);
```

Before every return, add `PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);`.

Concrete return paths:

```c
    if (ipiv == NULL) {
        PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);
        return -1;
    }
```

```c
    if (info != 0) {
        free(ipiv);
        PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);
        return info;
    }
```

```c
    if (work == NULL) {
        free(ipiv);
        PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);
        return -1;
    }
```

and at the successful end:

```c
    free(work);
    free(ipiv);
    PROF_END(prof, PROF_LA_INVERSE, t_la_inverse);
    return info;
```

- [ ] **Step 4: Wrap la_expm_sym**

At the start of `la_expm_sym`:

```c
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_la_expm_sym);
```

At the end:

```c
    free(VD);
    free(work);
    free(w);
    free(V);
    PROF_END(prof, PROF_LA_EXPM_SYM, t_la_expm_sym);
}
```

- [ ] **Step 5: Wrap udv_lmul**

At the start of `udv_lmul`:

```c
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_udv_lmul);
```

At the end:

```c
    free(Tnew);
    free(R);
    free(work);
    free(tau);
    free(M);
    PROF_END(prof, PROF_UDV_LMUL, t_udv_lmul);
}
```

- [ ] **Step 6: Wrap udv_inv_one_plus**

At the start of `udv_inv_one_plus`:

```c
    Profiler *prof = profiler_current();
    PROF_BEGIN(prof, t_udv_inv_one_plus);
```

Before each early error return, add:

```c
        PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
        return 1;
```

At the successful end:

```c
    free(Db);
    free(Ds);
    free(Tinv);
    free(UT_Tinv);
    free(M);
    free(Minv);
    free(tmp);
    free(gp);
    PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
    return 0;
```

- [ ] **Step 7: Run linalg and UDV tests**

Run:

```bash
make tests/test_linalg tests/test_udv tests/test_green_init
./tests/test_linalg
./tests/test_udv
./tests/test_green_init
```

Expected: all print `OK`.

- [ ] **Step 8: Run full tests**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 9: Commit**

```bash
git add src/linalg.c
git commit -m "feat(profiler): instrument linear algebra kernels" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 7: End-to-End Profile Output Check

**Files:**
- Test only in this task unless a defect is found

- [ ] **Step 1: Create a temporary profile input**

Run:

```bash
cat > /tmp/afqmc_profile_check.in <<'EOF'
lattice=chain
Lx=4
pbc=1
U=4
dtau=0.1
beta_list=1.0,2.0
nwarm=4
nmeas=8
nbin=4
stab=2
seed=123
profile=1
profile_file=/tmp/afqmc_profile_check.csv
EOF
```

- [ ] **Step 2: Run dqmc with profiling enabled**

Run:

```bash
make dqmc
./dqmc /tmp/afqmc_profile_check.in > /tmp/afqmc_profile_check.out
```

Expected:

- `/tmp/afqmc_profile_check.out` contains the usual physical output header and two data rows.
- `/tmp/afqmc_profile_check.csv` exists.

Check:

```bash
wc -l /tmp/afqmc_profile_check.out /tmp/afqmc_profile_check.csv
head -n 5 /tmp/afqmc_profile_check.csv
```

Expected CSV header:

```text
beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta
```

- [ ] **Step 3: Check required regions exist**

Run:

```bash
for r in beta_total dqmc_sweep green_from_scratch green_wrap green_update measure_sample jackknife udv_lmul udv_inv_one_plus la_gemm la_inverse la_expm_sym; do
  rg ",${r}," /tmp/afqmc_profile_check.csv >/dev/null || { echo "missing ${r}"; exit 1; }
done
```

Expected: no output and exit code 0.

- [ ] **Step 4: Check profiling is opt-in**

Run:

```bash
cat > /tmp/afqmc_profile_off.in <<'EOF'
lattice=chain
Lx=4
pbc=1
U=4
dtau=0.1
beta_list=1.0
nwarm=2
nmeas=4
nbin=2
seed=123
EOF
rm -f profile.csv
./dqmc /tmp/afqmc_profile_off.in > /tmp/afqmc_profile_off.out
test ! -f profile.csv
```

Expected: `test ! -f profile.csv` exits with code 0.

- [ ] **Step 5: Check stdout remains physical data only**

Run:

```bash
rg "dqmc_sweep|green_wrap|total_sec|phase|region" /tmp/afqmc_profile_check.out && exit 1 || exit 0
```

Expected: exit code 0, proving profiler rows were not mixed into stdout.

- [ ] **Step 6: Run full tests one final time**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 7: Commit final verification notes if code changed**

If Task 7 required code fixes, commit those fixes:

```bash
git add src tests
git commit -m "fix(profiler): complete end-to-end profile output" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

If no code changed in Task 7, do not create an empty commit.

## Task 8: Documentation and LOG

**Files:**
- Modify: `LOG.md`
- Optional Modify: `VALIDATION.md`

- [ ] **Step 1: Confirm current date**

Run:

```bash
date '+%Y-%m-%d %H:%M JST'
```

Use the returned timestamp in the LOG frontmatter.

- [ ] **Step 2: Add a LOG entry**

Add a new entry at the top of `LOG.md` after the intro comment. Use this shape and replace the timestamp with the `date` command output:

```markdown
---
date: 2026-06-25
datetime: 2026-06-25 HH:MM JST
model: Codex (GPT-5)
summary: |
  DQMC profiler/timer を実装した。
  `profile=1` と `profile_file=/tmp/afqmc_profile.csv` で beta/phase/region 別の CSV timing を出力できる。
  通常実行では stdout の物理量出力を変えず、profile file も作らないことを確認した。
handoff: |
  次は生成された CSV を plot/集計する helper script を追加するか、L=6 U=4 の代表 run でボトルネックを解析する。
---

## 2026-06-25: DQMC profiler/timer を追加

### やったこと・なぜ
- 目的: DQMC 実行時間を beta/phase/region ごとに分解し、温度依存計算のボトルネックを確認できるようにする。
- `src/profiler.h` / `src/profiler.c` を追加し、`profile=1` のときだけ CSV timing を出力するようにした。
- `main`, `dqmc_sweep`, Green 関数操作、UDV/LAPACK 周辺へ instrumentation を追加した。

### 確認
- `make test` → `ALL TESTS PASSED`
- `profile=1` run で CSV header と required regions を確認。
- `profile` 未指定 run で `profile.csv` が作られないことを確認。
```

- [ ] **Step 3: Commit documentation**

```bash
git add LOG.md
git commit -m "docs: log DQMC profiling timer implementation" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Final Verification

Run:

```bash
git status --short
make test
make dqmc
```

Expected:

- No tracked source changes remain unless the user has unrelated work in progress.
- `make test` prints `ALL TESTS PASSED`.
- `make dqmc` exits successfully.

If ignored build artifacts appear in `git status --ignored`, leave them ignored. Do not commit `dqmc`, `src/*.o`, `tests/test_*` binaries, `.DS_Store`, or temporary profile CSV files unless the user explicitly requests curated profiling data.

## Self-Review Checklist

- Spec coverage:
  - `profile=1` opt-in: Task 2 and Task 3.
  - CSV output: Task 1 and Task 7.
  - beta/phase/region breakdown: Task 1, Task 3, Task 7.
  - `main`, `dqmc`, `green`, `linalg` instrumentation: Tasks 3 through 6.
  - stdout unchanged: Task 7.
  - tests: Tasks 1, 2, 4, 5, 6, 7.
- Type consistency:
  - `Profiler`, `ProfPhase`, `ProfRegion`, `ProfStat` are introduced in Task 1 before use.
  - `dqmc_init(Dqmc*, Model*, Field*, Rng*, int, Profiler*)` is introduced in Task 4 and used by `main.c` and tests.
  - `PROF_BEGIN` / `PROF_END` are introduced in Task 1 before use.
- Scope:
  - No plotting script, external profiler integration, thread safety, or flamegraph support is included.
