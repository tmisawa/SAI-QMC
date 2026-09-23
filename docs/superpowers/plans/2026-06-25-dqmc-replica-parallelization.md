---
date: 2026-06-25
datetime: 2026-06-25 16:49 JST
model: Codex (GPT-5)
status: plan
topic: DQMC replica 並列化の TDD 実装計画
summary: |
  DQMC replica 並列化の実装計画。
  まず `parallel=serial`, `nrep=1` の既存互換を保ったまま replica 実行単位を切り出す。
  その後、serial replica 合成、replica seed/log、profiler TLS/merge、OpenMP build/loop を小タスクで追加する。
---

# DQMC Replica Parallelization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 同一温度点の独立 Markov chain replica を `parallel=serial|omp`, `nrep=N` で実行し、`nrep * nbin` 個の bin を合成して既存 stdout 形式の物理量を出す。

**Architecture:** `dqmc_sweep()` の内側は逐次のまま維持し、上位に `dqmc_run_replica()` を切り出す。各 replica は独立した `Model/Field/Dqmc/Rng/Profiler` を持ち、`ReplicaBin` を返す。OpenMP は replica loop だけに使い、MPI/hybrid へ進めるために seed と bin を global replica id 順で決定的に扱う。

**Tech Stack:** C11, GNU Make, optional OpenMP via Apple clang + Homebrew libomp or GCC/Clang `-fopenmp`, existing LAPACK/BLAS linkage, existing standalone `tests/test_*.c` pattern.

**Reference:** `docs/superpowers/specs/2026-06-25-dqmc-replica-parallelization-design.md`

---

## Scope Check

この plan は replica 並列 v1 だけを扱う。実行可能にする mode は `parallel=serial` と `parallel=omp` である。`parallel=mpi` と `parallel=hybrid` は入力語彙として認識し、v1 では明示エラーで止める。

この plan では `dqmc_sweep()` 内の site/slice loop、Green 更新式、UDV/QR、LAPACK kernel は変更しない。replica exchange、tempering、autocorrelation 推定、MPI 実装は含めない。

Commit examples in this plan use `Codex (GPT-5)` because this plan was written for the current Codex session. If a different model executes the plan, replace the `Co-Authored-By` trailer with the actual model used, following `AGENTS.md`.

## File Map

- Modify: `src/io.h`
  - `Params.parallel`, `Params.nrep`, `Params.replica_log` を追加する。
- Modify: `src/io.c`
  - `parallel`, `nrep`, `replica_log` の default、parser、validation を追加する。
- Modify: `tests/test_io.c`
  - 新入力 key、予約 mode、invalid mode、invalid `nrep` を検証する。
- Create: `src/replica.h`
  - `ReplicaBin`, `ReplicaResult`, seed/bin helper API を定義する。
- Create: `src/replica.c`
  - seed mixer、bin accumulator、bin ratio 変換、result allocation/free を実装する。
- Create: `tests/test_replica.c`
  - legacy seed 互換、mixer の安定性、seed 重複チェック、bin ratio 変換を検証する。
- Create: `src/replica_run.h`
  - `dqmc_run_replica()` の API を定義する。
- Create: `src/replica_run.c`
  - 既存 `main.c` の per-beta 単一 chain 実行を replica 単位へ切り出す。
- Modify: `src/main.c`
  - beta loop を replica loop + bin 合成へ置き換える。stdout header、reserved mode guard、replica log を追加する。
- Modify: `src/profiler.h`
  - local memory profiler、merge API、metadata API を追加する。
- Modify: `src/profiler.c`
  - `profiler_current()` を C11 `_Thread_local` にし、CSV header を拡張し、local stats merge を実装する。
- Modify: `tests/test_profiler.c`
  - header prefix 維持、新列、metadata、merge、current profiler pointer を検証する。
- Modify: `Makefile`
  - serial build を維持し、separate object の `dqmc_omp` / `test_omp` target を追加する。
- Create during manual verification only: `/tmp/afqmc_replica_*.in`, `/tmp/afqmc_replica_*.out`, `/tmp/afqmc_replica_*.csv`
  - repo には commit しない。
- Modify after implementation: `LOG.md`
  - 実装完了時に検証結果を追記する。

## Task 1: Parse Replica Parallel Options

**Files:**
- Modify: `src/io.h`
- Modify: `src/io.c`
- Modify: `tests/test_io.c`

- [ ] **Step 1: Write failing parser tests**

Append these checks before `TEST_END();` in `tests/test_io.c`:

```c
    write_text("/tmp/afqmc_parallel_defaults.in",
               "lattice=chain\nnmeas=20\nnbin=10\n");
    CHECK(params_read(&p, "/tmp/afqmc_parallel_defaults.in") == 0);
    CHECK(strcmp(p.parallel, "serial") == 0);
    CHECK(p.nrep == 1);
    CHECK(p.replica_log[0] == '\0');

    write_text("/tmp/afqmc_parallel_omp.in",
               "lattice=chain\nnmeas=20\nnbin=10\n"
               "parallel=omp\nnrep=4\nreplica_log=/tmp/reps.csv\n");
    CHECK(params_read(&p, "/tmp/afqmc_parallel_omp.in") == 0);
    CHECK(strcmp(p.parallel, "omp") == 0);
    CHECK(p.nrep == 4);
    CHECK(strcmp(p.replica_log, "/tmp/reps.csv") == 0);

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
```

- [ ] **Step 2: Run parser test to verify it fails**

Run:

```bash
make tests/test_io && ./tests/test_io
```

Expected: compile failure because `Params` has no `parallel`, `nrep`, or `replica_log` members.

- [ ] **Step 3: Add fields to `Params`**

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
    char parallel[16];
    int nrep;
    char replica_log[256];
    unsigned long long seed;
} Params;
```

- [ ] **Step 4: Add defaults and parser branches**

In `defaults()` in `src/io.c`, after `profile_file` default:

```c
    strcpy(p->parallel, "serial");
    p->nrep = 1;
    p->replica_log[0] = '\0';
```

In `params_read()`, add parser branches before `seed`:

```c
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
```

- [ ] **Step 5: Add validation**

In `src/io.c`, after `profile` validation:

```c
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
```

- [ ] **Step 6: Run parser test to verify it passes**

Run:

```bash
make tests/test_io && ./tests/test_io
```

Expected: `OK`

- [ ] **Step 7: Run full tests**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 8: Commit**

Run:

```bash
git add src/io.h src/io.c tests/test_io.c
git commit -m "feat(io): parse replica parallel options" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 2: Replica Data Model and Seed Helpers

**Files:**
- Create: `src/replica.h`
- Create: `src/replica.c`
- Create: `tests/test_replica.c`

- [ ] **Step 1: Write failing replica unit test**

Create `tests/test_replica.c`:

```c
#include "test_util.h"
#include "measure.h"
#include "replica.h"

#include <math.h>
#include <stdlib.h>

int main(void)
{
    const unsigned long long base = 246813579ULL;
    CHECK(replica_seed(base, 0, 0) == base);
    CHECK(replica_seed(base, 3, 0) == base + 3000ULL);

    const unsigned long long s10 = replica_seed(base, 0, 1);
    const unsigned long long s11 = replica_seed(base, 0, 1);
    const unsigned long long s20 = replica_seed(base, 0, 2);
    CHECK(s10 == s11);
    CHECK(s10 != base);
    CHECK(s10 != s20);

    unsigned long long seeds[4];
    for (int r = 0; r < 4; r++) {
        seeds[r] = replica_seed(base, 2, r);
    }
    CHECK(replica_check_seed_unique(seeds, 4) == 0);
    seeds[3] = seeds[1];
    CHECK(replica_check_seed_unique(seeds, 4) != 0);

    ReplicaBin bin = {0};
    MeasSample sample;
    sample.ekin = -2.0;
    sample.eint = 0.5;
    sample.ntot = 4.0;
    sample.doublon = 0.125;
    sample.E = -1.5;
    replica_bin_add(&bin, &sample, 2.0, 4.0, 4, 1.0);
    replica_bin_add(&bin, &sample, 2.0, 4.0, 4, 1.0);
    CHECK(bin.count == 2);
    CHECK(fabs(bin.sum_sign_Ehub - (-3.0)) < 1e-12);
    CHECK(fabs(bin.sum_sign_Egc - (-19.0)) < 1e-12);
    CHECK(fabs(bin.sum_sign_Eph - (-11.0)) < 1e-12);
    CHECK(fabs(bin.sum_sign_N - 8.0) < 1e-12);
    CHECK(fabs(bin.sum_sign_D - 0.25) < 1e-12);
    CHECK(fabs(bin.sum_sign - 2.0) < 1e-12);

    double eh, eg, ep, nn, dd, ss;
    CHECK(replica_bin_values(&bin, &eh, &eg, &ep, &nn, &dd, &ss) == 0);
    CHECK(fabs(eh - (-1.5)) < 1e-12);
    CHECK(fabs(eg - (-9.5)) < 1e-12);
    CHECK(fabs(ep - (-5.5)) < 1e-12);
    CHECK(fabs(nn - 4.0) < 1e-12);
    CHECK(fabs(dd - 0.125) < 1e-12);
    CHECK(fabs(ss - 1.0) < 1e-12);

    ReplicaBin bad = {0};
    bad.count = 1;
    CHECK(replica_bin_values(&bad, &eh, &eg, &ep, &nn, &dd, &ss) != 0);

    ReplicaResult result;
    CHECK(replica_result_alloc(&result, 3) == 0);
    CHECK(result.nbin == 3);
    CHECK(result.bins != NULL);
    replica_result_free(&result);
    CHECK(result.bins == NULL);
    CHECK(result.nbin == 0);

    TEST_END();
}
```

- [ ] **Step 2: Run replica test to verify it fails**

Run:

```bash
make tests/test_replica && ./tests/test_replica
```

Expected: compile failure because `replica.h` does not exist.

- [ ] **Step 3: Add `src/replica.h`**

Create `src/replica.h`:

```c
#ifndef REPLICA_H
#define REPLICA_H

#include "measure.h"

typedef struct {
    double sum_sign_Ehub;
    double sum_sign_Egc;
    double sum_sign_Eph;
    double sum_sign_N;
    double sum_sign_D;
    double sum_sign;
    int count;
} ReplicaBin;

typedef struct {
    ReplicaBin *bins;
    int nbin;
    int replica_id;
    unsigned long long seed;
    int status;
} ReplicaResult;

unsigned long long replica_seed(unsigned long long base_seed, int beta_index,
                                int replica_id);
int replica_check_seed_unique(const unsigned long long *seeds, int nseed);
void replica_bin_add(ReplicaBin *bin, const MeasSample *s, double mu, double U,
                     int nsite, double sign);
int replica_bin_values(const ReplicaBin *bin, double *Ehub, double *Egc,
                       double *Eph, double *N, double *D, double *sign);
int replica_result_alloc(ReplicaResult *result, int nbin);
void replica_result_free(ReplicaResult *result);

#endif
```

- [ ] **Step 4: Add `src/replica.c`**

Create `src/replica.c`:

```c
#include "replica.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t splitmix64_value(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

unsigned long long replica_seed(unsigned long long base_seed, int beta_index,
                                int replica_id)
{
    const unsigned long long legacy =
        base_seed + 1000ULL * (unsigned long long)beta_index;
    if (replica_id == 0) {
        return legacy;
    }

    uint64_t x = (uint64_t)base_seed;
    x ^= 0xD1B54A32D192ED03ULL * (uint64_t)(unsigned int)(beta_index + 1);
    x ^= 0xABC98388FB8FAC03ULL * (uint64_t)(unsigned int)(replica_id + 1);
    uint64_t mixed = splitmix64_value(x);
    if (mixed == legacy) {
        mixed = splitmix64_value(mixed);
    }
    return (unsigned long long)mixed;
}

int replica_check_seed_unique(const unsigned long long *seeds, int nseed)
{
    for (int i = 0; i < nseed; i++) {
        for (int j = i + 1; j < nseed; j++) {
            if (seeds[i] == seeds[j]) {
                return 1;
            }
        }
    }
    return 0;
}

void replica_bin_add(ReplicaBin *bin, const MeasSample *s, double mu, double U,
                     int nsite, double sign)
{
    const double e_hub = s->E;
    const double e_gc = s->E - mu * s->ntot;
    const double e_ph = s->E - 0.5 * U * s->ntot + 0.25 * U * (double)nsite;

    bin->sum_sign_Ehub += sign * e_hub;
    bin->sum_sign_Egc += sign * e_gc;
    bin->sum_sign_Eph += sign * e_ph;
    bin->sum_sign_N += sign * s->ntot;
    bin->sum_sign_D += sign * s->doublon;
    bin->sum_sign += sign;
    bin->count++;
}

int replica_bin_values(const ReplicaBin *bin, double *Ehub, double *Egc,
                       double *Eph, double *N, double *D, double *sign)
{
    if (bin->count <= 0 || fabs(bin->sum_sign) == 0.0) {
        return 1;
    }

    *Ehub = bin->sum_sign_Ehub / bin->sum_sign;
    *Egc = bin->sum_sign_Egc / bin->sum_sign;
    *Eph = bin->sum_sign_Eph / bin->sum_sign;
    *N = bin->sum_sign_N / bin->sum_sign;
    *D = bin->sum_sign_D / bin->sum_sign;
    *sign = bin->sum_sign / (double)bin->count;
    return 0;
}

int replica_result_alloc(ReplicaResult *result, int nbin)
{
    memset(result, 0, sizeof(*result));
    result->bins = calloc((size_t)nbin, sizeof(ReplicaBin));
    if (result->bins == NULL) {
        return 1;
    }
    result->nbin = nbin;
    return 0;
}

void replica_result_free(ReplicaResult *result)
{
    free(result->bins);
    result->bins = NULL;
    result->nbin = 0;
    result->replica_id = 0;
    result->seed = 0;
    result->status = 0;
}
```

- [ ] **Step 5: Run replica test to verify it passes**

Run:

```bash
make tests/test_replica && ./tests/test_replica
```

Expected: `OK`

- [ ] **Step 6: Run full tests**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 7: Commit**

Run:

```bash
git add src/replica.h src/replica.c tests/test_replica.c
git commit -m "feat(replica): add replica bins and seeds" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 3: Extract Single-Replica Execution

**Files:**
- Create: `src/replica_run.h`
- Create: `src/replica_run.c`
- Modify: `src/main.c`

- [ ] **Step 1: Capture a pre-refactor single-chain baseline**

Run:

```bash
cat > /tmp/afqmc_replica_baseline.in <<'EOF'
lattice=chain
Lx=4
pbc=1
U=4
dtau=0.1
beta_list=1.0
nwarm=20
nmeas=40
nbin=10
stab=8
seed=13579
EOF
make dqmc
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc /tmp/afqmc_replica_baseline.in > /tmp/afqmc_replica_baseline.before
grep -v '^#' /tmp/afqmc_replica_baseline.before > /tmp/afqmc_replica_baseline.before.data
```

Expected: `/tmp/afqmc_replica_baseline.before.data` contains one data row.

- [ ] **Step 2: Add `src/replica_run.h`**

Create `src/replica_run.h`:

```c
#ifndef REPLICA_RUN_H
#define REPLICA_RUN_H

#include "io.h"
#include "lattice.h"
#include "profiler.h"
#include "replica.h"

int dqmc_run_replica(const Params *p, const Lattice *L, int beta_index,
                     int Ltr, double mu, int replica_id,
                     unsigned long long seed, Profiler *prof,
                     ReplicaResult *result);

#endif
```

- [ ] **Step 3: Add `src/replica_run.c`**

Create `src/replica_run.c`:

```c
#include "replica_run.h"

#include "dqmc.h"
#include "field.h"
#include "measure.h"
#include "model.h"
#include "rng.h"

#include <stdlib.h>

int dqmc_run_replica(const Params *p, const Lattice *L, int beta_index,
                     int Ltr, double mu, int replica_id,
                     unsigned long long seed, Profiler *prof,
                     ReplicaResult *result)
{
    (void)beta_index;
    if (replica_result_alloc(result, p->nbin) != 0) {
        return 1;
    }
    result->replica_id = replica_id;
    result->seed = seed;
    result->status = 1;

    Profiler *old_prof = profiler_current();
    profiler_set_current(prof);

    profiler_phase_set(prof, PROF_PHASE_SETUP);
    Model m;
    PROF_BEGIN(prof, t_model_init);
    model_init(&m, L, p->U, p->dtau, 1, 0.0);
    PROF_END(prof, PROF_MODEL_INIT, t_model_init);

    Rng r;
    rng_seed(&r, seed);

    Field f;
    PROF_BEGIN(prof, t_field_init);
    field_init(&f, L->n, Ltr, p->U, p->dtau, &r);
    PROF_END(prof, PROF_FIELD_INIT, t_field_init);

    Dqmc D;
    PROF_BEGIN(prof, t_dqmc_init);
    dqmc_init(&D, &m, &f, &r, p->stab_interval, prof);
    PROF_END(prof, PROF_DQMC_INIT, t_dqmc_init);

    profiler_phase_set(prof, PROF_PHASE_WARMUP);
    for (int w = 0; w < p->nwarm; w++) {
        dqmc_sweep(&D);
    }

    const int per = p->nmeas / p->nbin;
    profiler_phase_set(prof, PROF_PHASE_MEASUREMENT);
    for (int bi = 0; bi < p->nbin; bi++) {
        for (int k = 0; k < per; k++) {
            dqmc_sweep(&D);
            PROF_BEGIN(prof, t_measure_sample);
            MeasSample s = measure_sample(L->n, L->t, p->U, D.Gu.g, D.Gd.g);
            PROF_END(prof, PROF_MEASURE_SAMPLE, t_measure_sample);
            replica_bin_add(&result->bins[bi], &s, mu, p->U, L->n, D.sign);
        }
    }

    result->status = 0;
    dqmc_free(&D);
    field_free(&f);
    model_free(&m);
    profiler_set_current(old_prof);
    return 0;
}
```

- [ ] **Step 4: Replace the per-beta body in `src/main.c` with single-replica runner**

Add includes:

```c
#include "replica.h"
#include "replica_run.h"
```

Inside the beta loop, after `T` is computed and after `profiler_beta_begin()`, replace the model/field/DQMC/warmup/measurement allocation block with:

```c
        const int total_bins = p.nbin;
        const unsigned long long seed = replica_seed(p.seed, b, 0);
        ReplicaResult result;
        if (dqmc_run_replica(&p, &L, b, Ltr, mu, 0, seed, &prof, &result) != 0) {
            fprintf(stderr, "ERROR: replica 0 failed\n");
            profiler_set_current(NULL);
            profiler_close(&prof);
            lattice_free(&L);
            return 1;
        }

        double *Ehub = calloc((size_t)total_bins, sizeof(double));
        double *Egc = calloc((size_t)total_bins, sizeof(double));
        double *Eph = calloc((size_t)total_bins, sizeof(double));
        double *Nbin = calloc((size_t)total_bins, sizeof(double));
        double *Dbin = calloc((size_t)total_bins, sizeof(double));
        double *Sbin = calloc((size_t)total_bins, sizeof(double));
        if (Ehub == NULL || Egc == NULL || Eph == NULL ||
            Nbin == NULL || Dbin == NULL || Sbin == NULL) {
            fprintf(stderr, "ERROR: failed to allocate measurement bins\n");
            free(Ehub);
            free(Egc);
            free(Eph);
            free(Nbin);
            free(Dbin);
            free(Sbin);
            replica_result_free(&result);
            profiler_set_current(NULL);
            profiler_close(&prof);
            lattice_free(&L);
            return 1;
        }

        for (int bi = 0; bi < p.nbin; bi++) {
            if (replica_bin_values(&result.bins[bi], &Ehub[bi], &Egc[bi],
                                   &Eph[bi], &Nbin[bi], &Dbin[bi],
                                   &Sbin[bi]) != 0) {
                fprintf(stderr, "ERROR: invalid zero-sign bin at beta=%g bin=%d\n",
                        beta, bi);
                free(Ehub);
                free(Egc);
                free(Eph);
                free(Nbin);
                free(Dbin);
                free(Sbin);
                replica_result_free(&result);
                profiler_set_current(NULL);
                profiler_close(&prof);
                lattice_free(&L);
                return 1;
            }
        }

        profiler_phase_set(&prof, PROF_PHASE_FINALIZE);
        PROF_BEGIN(&prof, t_jackknife);
        double Eh, Ehe, Eg, Ege, Ep, Epe, Nm, Ne, Dm, De, Sm, Se;
        jackknife(Ehub, total_bins, &Eh, &Ehe);
        jackknife(Egc, total_bins, &Eg, &Ege);
        jackknife(Eph, total_bins, &Ep, &Epe);
        jackknife(Nbin, total_bins, &Nm, &Ne);
        jackknife(Dbin, total_bins, &Dm, &De);
        jackknife(Sbin, total_bins, &Sm, &Se);
        PROF_END(&prof, PROF_JACKKNIFE, t_jackknife);
        printf("%.6g %.8g %.3g %.8g %.3g %.8g %.3g %.6g %.2g %.8g %.2g %.6g\n",
               T, Eh, Ehe, Eg, Ege, Ep, Epe, Nm, Ne, Dm, De, Sm);
        fflush(stdout);

        free(Ehub);
        free(Egc);
        free(Eph);
        free(Nbin);
        free(Dbin);
        free(Sbin);
        replica_result_free(&result);
        profiler_beta_end(&prof);
```

Remove the old per-beta `Model`, `Rng`, `Field`, `Dqmc`, warmup, measurement, bin array, and cleanup block so `profiler_beta_end(&prof);` is called exactly once per beta.

- [ ] **Step 5: Run full tests**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 6: Verify single-chain data row is unchanged**

Run:

```bash
make dqmc
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc /tmp/afqmc_replica_baseline.in > /tmp/afqmc_replica_baseline.after
grep -v '^#' /tmp/afqmc_replica_baseline.after > /tmp/afqmc_replica_baseline.after.data
diff -u /tmp/afqmc_replica_baseline.before.data /tmp/afqmc_replica_baseline.after.data
```

Expected: `diff` exits with code 0.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/replica_run.h src/replica_run.c src/main.c
git commit -m "refactor(replica): extract single replica runner" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 4: Serial Multi-Replica Combine and Replica Log

**Files:**
- Modify: `src/main.c`
- Modify: `tests/test_replica.c`

- [ ] **Step 1: Add a helper test for multiple bin conversion**

Append before `TEST_END();` in `tests/test_replica.c`:

```c
    ReplicaResult r0;
    ReplicaResult r1;
    CHECK(replica_result_alloc(&r0, 2) == 0);
    CHECK(replica_result_alloc(&r1, 2) == 0);
    replica_bin_add(&r0.bins[0], &sample, 2.0, 4.0, 4, 1.0);
    replica_bin_add(&r0.bins[1], &sample, 2.0, 4.0, 4, 1.0);
    replica_bin_add(&r1.bins[0], &sample, 2.0, 4.0, 4, 1.0);
    replica_bin_add(&r1.bins[1], &sample, 2.0, 4.0, 4, 1.0);
    CHECK(r0.nbin + r1.nbin == 4);
    replica_result_free(&r0);
    replica_result_free(&r1);
```

- [ ] **Step 2: Run replica test**

Run:

```bash
make tests/test_replica && ./tests/test_replica
```

Expected: `OK`

- [ ] **Step 3: Add reserved mode guards in `main.c`**

After profiler initialization and before the first stdout header:

```c
    if (strcmp(p.parallel, "mpi") == 0) {
        fprintf(stderr,
                "ERROR: parallel=mpi is reserved for future MPI implementation\n");
        profiler_set_current(NULL);
        profiler_close(&prof);
        lattice_free(&L);
        return 1;
    }
    if (strcmp(p.parallel, "hybrid") == 0) {
        fprintf(stderr,
                "ERROR: parallel=hybrid is reserved for future MPI implementation\n");
        profiler_set_current(NULL);
        profiler_close(&prof);
        lattice_free(&L);
        return 1;
    }
    if (strcmp(p.parallel, "omp") == 0) {
        fprintf(stderr, "ERROR: parallel=omp requires OpenMP-enabled build\n");
        profiler_set_current(NULL);
        profiler_close(&prof);
        lattice_free(&L);
        return 1;
    }
```

This serial-build guard will be refined in Task 7 for OpenMP builds.

- [ ] **Step 4: Extend stdout header**

Replace the first header `printf` with:

```c
    printf("# lattice=%s n=%d U=%g mu=%g dtau=%g bipartite=%d parallel=%s nrep=%d bins=%d\n",
           p.lattice, L.n, p.U, mu, p.dtau, L.is_bipartite, p.parallel,
           p.nrep, p.nrep * p.nbin);
```

- [ ] **Step 5: Add serial replica loop**

Inside the beta loop, replace the single `ReplicaResult result` path from Task 3 with this serial multi-replica skeleton:

```c
        const int total_bins = p.nrep * p.nbin;
        unsigned long long *seeds =
            calloc((size_t)p.nrep, sizeof(unsigned long long));
        ReplicaResult *results =
            calloc((size_t)p.nrep, sizeof(ReplicaResult));
        if (seeds == NULL || results == NULL) {
            fprintf(stderr, "ERROR: failed to allocate replica arrays\n");
            free(seeds);
            free(results);
            profiler_set_current(NULL);
            profiler_close(&prof);
            lattice_free(&L);
            return 1;
        }

        for (int r = 0; r < p.nrep; r++) {
            seeds[r] = replica_seed(p.seed, b, r);
        }
        if (replica_check_seed_unique(seeds, p.nrep) != 0) {
            fprintf(stderr, "ERROR: duplicate replica seed at beta=%g\n", beta);
            free(seeds);
            free(results);
            profiler_set_current(NULL);
            profiler_close(&prof);
            lattice_free(&L);
            return 1;
        }

        for (int r = 0; r < p.nrep; r++) {
            if (dqmc_run_replica(&p, &L, b, Ltr, mu, r, seeds[r], &prof,
                                 &results[r]) != 0) {
                fprintf(stderr, "ERROR: replica %d failed\n", r);
                for (int q = 0; q <= r; q++) {
                    replica_result_free(&results[q]);
                }
                free(seeds);
                free(results);
                profiler_set_current(NULL);
                profiler_close(&prof);
                lattice_free(&L);
                return 1;
            }
        }
```

Then fill `Ehub/Egc/Eph/Nbin/Dbin/Sbin` by deterministic replica id order:

```c
        for (int r = 0; r < p.nrep; r++) {
            for (int bi = 0; bi < p.nbin; bi++) {
                const int out = r * p.nbin + bi;
                if (replica_bin_values(&results[r].bins[bi], &Ehub[out],
                                       &Egc[out], &Eph[out], &Nbin[out],
                                       &Dbin[out], &Sbin[out]) != 0) {
                    fprintf(stderr,
                            "ERROR: invalid zero-sign bin at beta=%g replica=%d bin=%d\n",
                            beta, r, bi);
                    for (int q = 0; q < p.nrep; q++) {
                        replica_result_free(&results[q]);
                    }
                    free(seeds);
                    free(results);
                    free(Ehub);
                    free(Egc);
                    free(Eph);
                    free(Nbin);
                    free(Dbin);
                    free(Sbin);
                    profiler_set_current(NULL);
                    profiler_close(&prof);
                    lattice_free(&L);
                    return 1;
                }
            }
        }
```

After printing, free all replica results:

```c
        for (int r = 0; r < p.nrep; r++) {
            replica_result_free(&results[r]);
        }
        free(seeds);
        free(results);
```

- [ ] **Step 6: Add replica log output**

Before the beta loop, after stdout headers:

```c
    FILE *replica_fp = NULL;
    const int write_replica_log =
        strcmp(p.replica_log, "none") != 0 &&
        (p.replica_log[0] != '\0' || p.nrep > 1);
    const char *replica_log_path =
        (p.replica_log[0] != '\0') ? p.replica_log : "replicas.csv";
    if (write_replica_log) {
        replica_fp = fopen(replica_log_path, "w");
        if (replica_fp == NULL) {
            fprintf(stderr, "ERROR: failed to open replica_log %s\n",
                    replica_log_path);
            profiler_set_current(NULL);
            profiler_close(&prof);
            lattice_free(&L);
            return 1;
        }
        fprintf(replica_fp, "beta,T,replica_id,seed,nwarm,nmeas,nbin,status\n");
    }
```

After all replicas have completed and before converting bins, write the log in a separate serial loop. Do not write to `replica_fp` inside an OpenMP parallel region.

```c
        if (replica_fp != NULL) {
            for (int r = 0; r < p.nrep; r++) {
                const char *status = (results[r].status == 0) ? "ok" : "error";
                fprintf(replica_fp, "%.17g,%.17g,%d,%llu,%d,%d,%d,%s\n",
                        beta, T, r, seeds[r], p.nwarm, p.nmeas, p.nbin,
                        status);
            }
            if (ferror(replica_fp)) {
                fprintf(stderr, "ERROR: failed to write replica_log\n");
                for (int q = 0; q < p.nrep; q++) {
                    replica_result_free(&results[q]);
                }
                free(seeds);
                free(results);
                profiler_set_current(NULL);
                profiler_close(&prof);
                if (replica_fp != NULL) {
                    fclose(replica_fp);
                }
                lattice_free(&L);
                return 1;
            }
        }
```

At every existing successful exit path before `profiler_close(&prof)`:

```c
    if (replica_fp != NULL && fclose(replica_fp) != 0) {
        profiler_set_current(NULL);
        profiler_close(&prof);
        lattice_free(&L);
        return 1;
    }
```

On error paths after `replica_fp` is opened, close it before returning. If `fclose()` fails during an error path, keep returning nonzero.

- [ ] **Step 7: Run full tests**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 8: Verify serial nrep=1 data row is unchanged**

Run:

```bash
make dqmc
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc /tmp/afqmc_replica_baseline.in > /tmp/afqmc_replica_serial1.out
grep -v '^#' /tmp/afqmc_replica_serial1.out > /tmp/afqmc_replica_serial1.data
diff -u /tmp/afqmc_replica_baseline.before.data /tmp/afqmc_replica_serial1.data
```

Expected: `diff` exits with code 0.

- [ ] **Step 9: Verify serial nrep=2 and replica log**

Run:

```bash
cat > /tmp/afqmc_replica_serial2.in <<'EOF'
lattice=chain
Lx=4
pbc=1
U=4
dtau=0.1
beta_list=1.0
nwarm=10
nmeas=20
nbin=5
stab=8
seed=13579
nrep=2
parallel=serial
replica_log=/tmp/afqmc_replica_serial2_replicas.csv
EOF
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc /tmp/afqmc_replica_serial2.in > /tmp/afqmc_replica_serial2.out
rg "parallel=serial nrep=2 bins=10" /tmp/afqmc_replica_serial2.out
wc -l /tmp/afqmc_replica_serial2_replicas.csv
```

Expected: `rg` exits with code 0 and `wc -l` reports `3` lines.

- [ ] **Step 10: Verify reserved modes fail clearly**

Run:

```bash
cat > /tmp/afqmc_replica_mpi.in <<'EOF'
lattice=chain
nmeas=20
nbin=10
parallel=mpi
EOF
./dqmc /tmp/afqmc_replica_mpi.in > /tmp/afqmc_replica_mpi.out 2> /tmp/afqmc_replica_mpi.err && exit 1 || true
rg "parallel=mpi is reserved" /tmp/afqmc_replica_mpi.err
```

Expected: `rg` exits with code 0.

- [ ] **Step 11: Commit**

Run:

```bash
git add src/main.c tests/test_replica.c
git commit -m "feat(replica): combine serial replicas" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 5: Thread-Local Profiler and Mergeable Stats

**Files:**
- Modify: `src/profiler.h`
- Modify: `src/profiler.c`
- Modify: `tests/test_profiler.c`

- [ ] **Step 1: Write failing profiler merge test**

In `tests/test_profiler.c`, after the existing enabled-profiler block and before `TEST_END();`, add:

```c
    CHECK(file_contains(enabled_path, "wall_sec,thread_total_sec,nrep,parallel"));
    CHECK(file_contains(enabled_path, ",1,serial"));

    Profiler local;
    profiler_init_memory(&local, 1);
    profiler_beta_begin(&local, 2.0, 0.5, 0.1, 20);
    profiler_phase_set(&local, PROF_PHASE_MEASUREMENT);
    profiler_add(&local, PROF_DQMC_SWEEP, 1.25);

    Profiler merged;
    const char *merged_path = "/tmp/afqmc_profile_merged.csv";
    remove(merged_path);
    profiler_init(&merged, 1, merged_path);
    profiler_set_metadata(&merged, 4, "omp");
    profiler_beta_begin(&merged, 2.0, 0.5, 0.1, 20);
    profiler_merge(&merged, &local);
    profiler_beta_end(&merged);
    profiler_close(&merged);
    CHECK(!profiler_error(&merged));
    CHECK(file_contains(merged_path, "measurement,dqmc_sweep,1,1.25"));
    CHECK(file_contains(merged_path, ",4,omp"));
```

- [ ] **Step 2: Run profiler test to verify it fails**

Run:

```bash
make tests/test_profiler && ./tests/test_profiler
```

Expected: compile failure because `profiler_init_memory`, `profiler_set_metadata`, and `profiler_merge` do not exist.

- [ ] **Step 3: Extend `Profiler` API**

Modify `src/profiler.h`:

```c
typedef struct {
    int enabled;
    int error;
    FILE *fp;
    double beta;
    double T;
    double dtau;
    int Ltr;
    double beta_start_sec;
    int nrep;
    char parallel[16];
    ProfPhase phase;
    ProfStat stat[PROF_PHASE_COUNT][PROF_REGION_COUNT];
} Profiler;

void profiler_init_memory(Profiler *p, int enabled);
void profiler_set_metadata(Profiler *p, int nrep, const char *parallel);
void profiler_merge(Profiler *dst, const Profiler *src);
```

- [ ] **Step 4: Make current profiler thread-local and add memory init**

In `src/profiler.c`, replace the global current pointer with:

```c
static _Thread_local Profiler *g_current_profiler = NULL;
```

Add a shared initializer:

```c
static void init_common(Profiler *p, int enabled)
{
    memset(p, 0, sizeof(*p));
    p->enabled = enabled ? 1 : 0;
    p->phase = PROF_PHASE_SETUP;
    p->nrep = 1;
    strcpy(p->parallel, "serial");
}
```

Use it in `profiler_init()`:

```c
void profiler_init(Profiler *p, int enabled, const char *path)
{
    init_common(p, enabled);
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
            "beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta,wall_sec,thread_total_sec,nrep,parallel\n");
    if (ferror(p->fp)) {
        p->error = 1;
    }
}
```

Add memory init:

```c
void profiler_init_memory(Profiler *p, int enabled)
{
    init_common(p, enabled);
}
```

- [ ] **Step 5: Add metadata and merge**

Add to `src/profiler.c`:

```c
void profiler_set_metadata(Profiler *p, int nrep, const char *parallel)
{
    if (p == NULL) {
        return;
    }
    p->nrep = (nrep > 0) ? nrep : 1;
    if (parallel != NULL && parallel[0] != '\0') {
        strncpy(p->parallel, parallel, sizeof p->parallel - 1);
        p->parallel[sizeof p->parallel - 1] = '\0';
    }
}

void profiler_merge(Profiler *dst, const Profiler *src)
{
    if (dst == NULL || src == NULL || !dst->enabled || !src->enabled) {
        return;
    }
    for (int ph = 0; ph < PROF_PHASE_COUNT; ph++) {
        for (int rg = 0; rg < PROF_REGION_COUNT; rg++) {
            dst->stat[ph][rg].calls += src->stat[ph][rg].calls;
            dst->stat[ph][rg].total_sec += src->stat[ph][rg].total_sec;
        }
    }
}
```

- [ ] **Step 6: Extend CSV row writer**

In `profiler_beta_end()`, replace the `fprintf` for stat rows with:

```c
            const double thread_total = s.total_sec;
            const double wall_sec =
                (rg == PROF_BETA_TOTAL) ? beta_total : 0.0;
            fprintf(p->fp,
                    "%.17g,%.17g,%.17g,%d,%s,%s,%llu,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%s\n",
                    p->beta, p->T, p->dtau, p->Ltr,
                    phase_name((ProfPhase)ph), region_name((ProfRegion)rg),
                    s.calls, thread_total, avg, frac, wall_sec, thread_total,
                    p->nrep, p->parallel);
```

Keep the existing assignment:

```c
    p->stat[PROF_PHASE_ALL][PROF_BETA_TOTAL].calls = 1;
    p->stat[PROF_PHASE_ALL][PROF_BETA_TOTAL].total_sec = beta_total;
```

This makes `total_sec == thread_total_sec` for backward-compatible interpretation.

- [ ] **Step 7: Set profiler metadata in `main.c`**

After `profiler_beta_begin(&prof, beta, T, p.dtau, Ltr);` in `src/main.c`, add:

```c
        profiler_set_metadata(&prof, p.nrep, p.parallel);
```

- [ ] **Step 8: Run profiler test**

Run:

```bash
make tests/test_profiler && ./tests/test_profiler
```

Expected: `OK`

- [ ] **Step 9: Run full tests**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 10: Commit**

Run:

```bash
git add src/profiler.h src/profiler.c src/main.c tests/test_profiler.c
git commit -m "feat(profiler): support replica-local timing stats" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 6: Use Replica-Local Profilers in Serial Mode

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Update serial replica loop to use local profilers**

Inside the beta loop in `src/main.c`, allocate local profilers next to `results`:

```c
        Profiler *replica_profs =
            calloc((size_t)p.nrep, sizeof(Profiler));
        if (seeds == NULL || results == NULL || replica_profs == NULL) {
            fprintf(stderr, "ERROR: failed to allocate replica arrays\n");
            free(seeds);
            free(results);
            free(replica_profs);
            profiler_set_current(NULL);
            profiler_close(&prof);
            lattice_free(&L);
            return 1;
        }
```

Before each serial `dqmc_run_replica()` call:

```c
            profiler_init_memory(&replica_profs[r], p.profile);
            profiler_set_metadata(&replica_profs[r], 1, "serial");
            profiler_beta_begin(&replica_profs[r], beta, T, p.dtau, Ltr);
```

Pass `&replica_profs[r]` instead of `&prof`:

```c
            if (dqmc_run_replica(&p, &L, b, Ltr, mu, r, seeds[r],
                                 &replica_profs[r], &results[r]) != 0) {
```

After all replicas finish and before jackknife:

```c
        for (int r = 0; r < p.nrep; r++) {
            profiler_merge(&prof, &replica_profs[r]);
        }
```

Free `replica_profs` on all success and error paths:

```c
        free(replica_profs);
```

- [ ] **Step 2: Run full tests**

Run:

```bash
make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 3: Verify profiled serial nrep=2 CSV**

Run:

```bash
cat > /tmp/afqmc_replica_profile_serial2.in <<'EOF'
lattice=chain
Lx=4
pbc=1
U=4
dtau=0.1
beta_list=1.0
nwarm=10
nmeas=20
nbin=5
stab=8
seed=13579
nrep=2
parallel=serial
profile=1
profile_file=/tmp/afqmc_replica_profile_serial2.csv
replica_log=/tmp/afqmc_replica_profile_serial2_replicas.csv
EOF
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc /tmp/afqmc_replica_profile_serial2.in > /tmp/afqmc_replica_profile_serial2.out
head -n 1 /tmp/afqmc_replica_profile_serial2.csv
rg ",2,serial" /tmp/afqmc_replica_profile_serial2.csv
rg "measurement,dqmc_sweep" /tmp/afqmc_replica_profile_serial2.csv
```

Expected: header contains `wall_sec,thread_total_sec,nrep,parallel`; both `rg` commands exit with code 0.

- [ ] **Step 4: Commit**

Run:

```bash
git add src/main.c
git commit -m "feat(replica): use local profiler stats" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 7: OpenMP Build Target and Replica Loop

**Files:**
- Modify: `Makefile`
- Modify: `src/main.c`

- [ ] **Step 1: Add OpenMP Makefile targets with separate objects**

Replace the object/test section in `Makefile` with:

```make
SRC  = $(wildcard src/*.c)
OBJ  = $(SRC:.c=.o)
OMP_OBJ = $(SRC:.c=.omp.o)
TESTS = $(wildcard tests/test_*.c)
TESTBIN = $(TESTS:.c=)
TESTBIN_OMP = $(TESTS:tests/test_%.c=tests/test_%_omp)

LIBOMP_PREFIX ?= /opt/homebrew/opt/libomp
ifeq ($(UNAME_S),Darwin)
  OMP_CFLAGS = -Xpreprocessor -fopenmp -I$(LIBOMP_PREFIX)/include -DAFQMC_USE_OPENMP
  OMP_LDLIBS = -L$(LIBOMP_PREFIX)/lib -lomp
else
  OMP_CFLAGS = -fopenmp -DAFQMC_USE_OPENMP
  OMP_LDLIBS = -fopenmp
endif

dqmc: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.omp.o: %.c
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -c -o $@ $<

dqmc_omp: $(OMP_OBJ)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -o $@ $(OMP_OBJ) $(LDLIBS) $(OMP_LDLIBS)

LIBSRC = $(filter-out src/main.c,$(SRC))
LIBSRC_OMP = $(filter-out src/main.omp.o,$(OMP_OBJ))
tests/test_%: tests/test_%.c $(LIBSRC)
	$(CC) $(CFLAGS) -o $@ $< $(LIBSRC) $(LDLIBS)

tests/test_%_omp: tests/test_%.c $(LIBSRC_OMP)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -o $@ $< $(LIBSRC_OMP) $(LDLIBS) $(OMP_LDLIBS)

test: $(TESTBIN)
	@fail=0; for t in $(TESTBIN); do printf "%-28s " $$t; ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi; echo "ALL TESTS PASSED"

test_omp: $(TESTBIN_OMP)
	@fail=0; for t in $(TESTBIN_OMP); do printf "%-28s " $$t; ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME OMP TESTS FAILED"; exit 1; fi; echo "ALL OMP TESTS PASSED"

clean:
	rm -f src/*.o src/*.omp.o dqmc dqmc_omp $(TESTBIN) $(TESTBIN_OMP)

.PHONY: test test_omp clean
```

- [ ] **Step 2: Run serial tests after Makefile change**

Run:

```bash
make clean && make test
```

Expected: `ALL TESTS PASSED`

- [ ] **Step 3: Replace serial-build omp guard with conditional guard**

In `src/main.c`, replace the unconditional `parallel=omp` error block from Task 4 with:

```c
#ifndef AFQMC_USE_OPENMP
    if (strcmp(p.parallel, "omp") == 0) {
        fprintf(stderr, "ERROR: parallel=omp requires OpenMP-enabled build\n");
        profiler_set_current(NULL);
        profiler_close(&prof);
        lattice_free(&L);
        return 1;
    }
#endif
```

- [ ] **Step 4: Add OpenMP include and loop pragma**

At the top of `src/main.c`, after standard includes:

```c
#ifdef AFQMC_USE_OPENMP
#include <omp.h>
#endif
```

Replace the serial replica execution loop with:

```c
        int *replica_failed = calloc((size_t)p.nrep, sizeof(int));
        if (replica_failed == NULL) {
            fprintf(stderr, "ERROR: failed to allocate replica status array\n");
            for (int q = 0; q < p.nrep; q++) {
                replica_result_free(&results[q]);
            }
            free(seeds);
            free(results);
            free(replica_profs);
            profiler_set_current(NULL);
            profiler_close(&prof);
            lattice_free(&L);
            return 1;
        }

#ifdef AFQMC_USE_OPENMP
        if (strcmp(p.parallel, "omp") == 0) {
#pragma omp parallel for schedule(static)
            for (int r = 0; r < p.nrep; r++) {
                profiler_init_memory(&replica_profs[r], p.profile);
                profiler_set_metadata(&replica_profs[r], 1, "omp");
                profiler_beta_begin(&replica_profs[r], beta, T, p.dtau, Ltr);
                if (dqmc_run_replica(&p, &L, b, Ltr, mu, r, seeds[r],
                                     &replica_profs[r], &results[r]) != 0) {
                    replica_failed[r] = 1;
                }
            }
        } else
#endif
        {
            for (int r = 0; r < p.nrep; r++) {
                profiler_init_memory(&replica_profs[r], p.profile);
                profiler_set_metadata(&replica_profs[r], 1, "serial");
                profiler_beta_begin(&replica_profs[r], beta, T, p.dtau, Ltr);
                if (dqmc_run_replica(&p, &L, b, Ltr, mu, r, seeds[r],
                                     &replica_profs[r], &results[r]) != 0) {
                    replica_failed[r] = 1;
                    break;
                }
            }
        }

        int any_failed = 0;
        for (int r = 0; r < p.nrep; r++) {
            any_failed |= replica_failed[r];
        }
        if (replica_fp != NULL) {
            for (int r = 0; r < p.nrep; r++) {
                const char *status = replica_failed[r] ? "error" : "ok";
                fprintf(replica_fp, "%.17g,%.17g,%d,%llu,%d,%d,%d,%s\n",
                        beta, T, r, seeds[r], p.nwarm, p.nmeas, p.nbin,
                        status);
            }
            if (ferror(replica_fp)) {
                fprintf(stderr, "ERROR: failed to write replica_log\n");
                any_failed = 1;
            }
        }
        if (any_failed) {
            fprintf(stderr, "ERROR: at least one replica failed at beta=%g\n",
                    beta);
            for (int q = 0; q < p.nrep; q++) {
                replica_result_free(&results[q]);
            }
            free(seeds);
            free(results);
            free(replica_profs);
            free(replica_failed);
            profiler_set_current(NULL);
            profiler_close(&prof);
            lattice_free(&L);
            return 1;
        }
```

Free `replica_failed` on the success path after all `ReplicaResult` values have been converted and freed:

```c
        free(replica_failed);
```

In every error branch after `replica_failed` allocation, add the same `free(replica_failed);` before `profiler_set_current(NULL);`.

- [ ] **Step 5: Build OpenMP target**

Run:

```bash
make dqmc_omp
```

Expected on this macOS machine with Homebrew libomp installed: build succeeds. If `LIBOMP_PREFIX` is not `/opt/homebrew/opt/libomp`, run:

```bash
make dqmc_omp LIBOMP_PREFIX=/path/to/libomp
```

- [ ] **Step 6: Run OpenMP tests**

Run:

```bash
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 make test_omp
```

Expected: `ALL OMP TESTS PASSED`

- [ ] **Step 7: Verify serial build rejects `parallel=omp`**

Run:

```bash
cat > /tmp/afqmc_replica_omp_guard.in <<'EOF'
lattice=chain
nmeas=20
nbin=10
parallel=omp
nrep=2
EOF
make dqmc
./dqmc /tmp/afqmc_replica_omp_guard.in > /tmp/afqmc_replica_omp_guard.out 2> /tmp/afqmc_replica_omp_guard.err && exit 1 || true
rg "parallel=omp requires OpenMP-enabled build" /tmp/afqmc_replica_omp_guard.err
```

Expected: `rg` exits with code 0.

- [ ] **Step 8: Verify OpenMP and serial replica outputs match for fixed seeds**

Run:

```bash
cat > /tmp/afqmc_replica_compare.in <<'EOF'
lattice=chain
Lx=4
pbc=1
U=4
dtau=0.1
beta_list=1.0
nwarm=20
nmeas=40
nbin=10
stab=8
seed=13579
nrep=2
parallel=serial
replica_log=none
EOF
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc /tmp/afqmc_replica_compare.in > /tmp/afqmc_replica_compare_serial.out
sed 's/^parallel=serial$/parallel=omp/' /tmp/afqmc_replica_compare.in > /tmp/afqmc_replica_compare_omp.in
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc_omp /tmp/afqmc_replica_compare_omp.in > /tmp/afqmc_replica_compare_omp.out
grep -v '^#' /tmp/afqmc_replica_compare_serial.out > /tmp/afqmc_replica_compare_serial.data
grep -v '^#' /tmp/afqmc_replica_compare_omp.out > /tmp/afqmc_replica_compare_omp.data
diff -u /tmp/afqmc_replica_compare_serial.data /tmp/afqmc_replica_compare_omp.data
```

Expected: `diff` exits with code 0.

- [ ] **Step 9: Commit**

Run:

```bash
git add Makefile src/main.c
git commit -m "feat(replica): add OpenMP replica execution" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 8: End-to-End Profiling Run and Documentation

**Files:**
- Modify: `LOG.md`
- Optional create: `data/profiling_runs/L6_U4_beta4_dtau0.1_nrep*_profile.*`

- [ ] **Step 1: Run final serial and OpenMP verification**

Run:

```bash
make clean
make test
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 make test_omp
```

Expected:

```text
ALL TESTS PASSED
ALL OMP TESTS PASSED
```

- [ ] **Step 2: Run a short L=6 U=4 serial-vs-OMP comparison**

Run:

```bash
cat > /tmp/afqmc_L6_U4_replica_serial.in <<'EOF'
lattice=chain
Lx=6
pbc=1
U=4
dtau=0.1
beta_list=4.0
nwarm=500
nmeas=2000
nbin=20
stab=8
seed=246813579
nrep=4
parallel=serial
replica_log=/tmp/afqmc_L6_U4_replica_serial_replicas.csv
EOF
cp /tmp/afqmc_L6_U4_replica_serial.in /tmp/afqmc_L6_U4_replica_omp.in
perl -0pi -e 's/parallel=serial/parallel=omp/' /tmp/afqmc_L6_U4_replica_omp.in
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc /tmp/afqmc_L6_U4_replica_serial.in > /tmp/afqmc_L6_U4_replica_serial.out
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc_omp /tmp/afqmc_L6_U4_replica_omp.in > /tmp/afqmc_L6_U4_replica_omp.out
grep -v '^#' /tmp/afqmc_L6_U4_replica_serial.out > /tmp/afqmc_L6_U4_replica_serial.data
grep -v '^#' /tmp/afqmc_L6_U4_replica_omp.out > /tmp/afqmc_L6_U4_replica_omp.data
diff -u /tmp/afqmc_L6_U4_replica_serial.data /tmp/afqmc_L6_U4_replica_omp.data
```

Expected: `diff` exits with code 0.

- [ ] **Step 3: Run a representative profiled OpenMP job**

Run:

```bash
mkdir -p data/profiling_runs
cat > data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.in <<'EOF'
lattice=chain
Lx=6
pbc=1
U=4
dtau=0.1
beta_list=4.0
nwarm=1000
nmeas=20000
nbin=100
stab=8
seed=246813579
nrep=4
parallel=omp
replica_log=data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_replicas.csv
profile=1
profile_file=data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.csv
EOF
/usr/bin/time -p env VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc_omp data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.in > data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.dat 2> data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.time
```

Expected:

- `.dat` contains one physical output row.
- `.csv` contains `wall_sec,thread_total_sec,nrep,parallel`.
- `replicas.csv` contains header plus 4 replica rows.

- [ ] **Step 4: Summarize profiler top rows**

Run:

```bash
python3 - <<'PY'
import csv
from pathlib import Path
p = Path('data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.csv')
rows = list(csv.DictReader(p.open()))
for r in sorted(rows, key=lambda x: float(x['thread_total_sec']), reverse=True)[:12]:
    print(f"{r['phase']:11s} {r['region']:22s} calls={int(r['calls']):10d} "
          f"thread={float(r['thread_total_sec']):10.6f}s "
          f"wall={float(r['wall_sec']):10.6f}s frac={float(r['frac_beta']):.6f} "
          f"nrep={r['nrep']} parallel={r['parallel']}")
PY
```

Expected: top rows include `beta_total`, `dqmc_sweep`, `green_from_scratch`, and `udv_lmul`.

- [ ] **Step 5: Update `LOG.md`**

Add a new top entry with current JST time from `date '+%Y-%m-%d %H:%M JST'`. Use this structure:

```markdown
---
date: 2026-06-25
datetime: YYYY-MM-DD HH:MM JST
model: Codex (GPT-5)
summary: |
  DQMC replica 並列化を実装した。
  `parallel=serial|omp`, `nrep=N` で同一温度の独立 replica を実行し、`nrep * nbin` の bin を合成して jackknife できる。
  serial/OMP の固定 seed 比較、replica log、profiler wall/thread timing、既存 test suite を確認した。
handoff: |
  次は L=6 U=4 で nrep scaling を取り、wall time と thread_total_sec の scaling を確認する。
---

## 2026-06-25: DQMC replica 並列化を実装

### やったこと・なぜ
- 目的: 同一温度点の統計計算を独立 Markov chain replica で並列化する。
- `parallel`, `nrep`, `replica_log` を入力に追加した。
- `ReplicaBin` と `dqmc_run_replica()` を追加し、single-chain 実行を replica 単位へ切り出した。
- `parallel=serial` で複数 replica を決定的順序で合成するようにした。
- profiler current を `_Thread_local` にし、replica local stats を merge して `wall_sec` と `thread_total_sec` を CSV 出力するようにした。
- `dqmc_omp` と `test_omp` を追加し、OpenMP build では replica loop を並列化した。

### 確認
- `make test` → `ALL TESTS PASSED`
- `VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 make test_omp` → `ALL OMP TESTS PASSED`
- `parallel=serial`, `nrep=1` の物理量 data row が実装前と一致。
- `parallel=serial`, `nrep=4` と `parallel=omp`, `nrep=4` の物理量 data row が固定 seed で一致。
- `parallel=mpi|hybrid` は予約 mode として明示エラー。
- OpenMP 無効 build の `parallel=omp` は明示エラー。
- L=6 U=4 beta=4 の profiled OpenMP run で replica log と profiler CSV を確認。
```

- [ ] **Step 6: Commit final implementation log and selected profiling data**

If Step 3 generated representative profiling data that should be tracked, add those files explicitly. Do not add temporary `/tmp` files.

Run:

```bash
git add LOG.md
git add data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.in \
        data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.dat \
        data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.csv \
        data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_profile.time \
        data/profiling_runs/L6_U4_beta4_dtau0.1_nrep4_n20k_replicas.csv
git commit -m "docs: log replica parallelization implementation" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Self-Review Checklist

- Spec coverage:
  - `parallel=serial|omp|mpi|hybrid`: Task 1 parses, Task 4/7 enforce runtime behavior.
  - `nmeas`/`nbin` per replica: Task 3 runner uses existing per-replica values; Task 4 combines `nrep * nbin`.
  - `ReplicaBin` numerator/denominator: Task 2 implements and tests it.
  - legacy seed for `replica_id=0`: Task 2 implements and tests it.
  - replica seed log: Task 4 implements and verifies it.
  - profiler TLS and wall/thread timing: Task 5/6 implement and verify it.
  - optional OpenMP build: Task 7 implements and verifies it.
  - deterministic serial/OMP comparison with BLAS thread count fixed: Task 7/8 verify it.
- Placeholder scan:
  - This plan has been scanned for placeholder markers and incomplete task steps.
- Type consistency:
  - `ReplicaBin`, `ReplicaResult`, `replica_seed`, `replica_bin_add`, `replica_bin_values`, and `dqmc_run_replica` names are introduced before use.
  - `Profiler` additions are introduced in Task 5 before OpenMP use in Task 7.
