---
date: 2026-06-26
datetime: 2026-06-26 08:43 JST
model: Codex (GPT-5)
status: plan
topic: DQMC MPI replica parallelization implementation plan
summary: |
  DQMC MPI replica 並列化の実装計画。
  optional MPI build、rank 分配 helper、MPI 専用 profiler metadata、rank0 I/O、status/bin/profiler gather を段階的に実装する。
  collective hang を避けるため、rank0 限定 failure も含めて beta loop の各境界で fail flag を全 rank 共有する。
  plan review の High 3 / Medium 3 / Low 4 を反映し、MPI beta branch を per-beta 完結ブロックとして明示した。
---

# DQMC MPI Replica Parallelization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `parallel=mpi` で global replica id を MPI rank に分配し、rank 数に依存しない seed・bin 順序・物理量出力・replica log・profiler CSV を得る。

**Architecture:** 物理 kernel と `dqmc_run_replica()` は変更せず、MPI は replica loop の外側だけに入れる。`src/replica_mpi.c` には MPI 非依存の rank 分配・pack/unpack helper を置き、`src/main.c` では MPI bootstrap、rank0 I/O、status/bin/profiler collectives を担当する。失敗時は rank0-only failure も `MPI_Allreduce(MAX)` で共有し、全 rank が同じ cleanup path に入る。

**Tech Stack:** C11, GNU Make, optional MPI via `mpicc`/`mpirun`, existing LAPACK/BLAS linkage, existing standalone `tests/test_*.c` pattern, existing `Profiler` and `ReplicaResult` data structures.

**Reference:** `docs/superpowers/specs/2026-06-25-dqmc-mpi-replica-parallelization-design.md`

---

## Scope Check

This plan implements only replica-level MPI parallelism for finite-temperature DQMC.

In scope:

- `make dqmc_mpi` and `make test_mpi`.
- `parallel=mpi` in MPI-enabled build.
- `nrep` as global replica count.
- Contiguous global replica id assignment per rank.
- Rank-count-independent `replica_seed(base_seed, beta_index, global_replica_id)`.
- Rank0-only stdout, `replica_log`, `profile_file`, and `hopping_used.txt`.
- `MPI_Gatherv` for status and bins.
- `MPI_Reduce(SUM)` for profiler stats.
- Runtime checks for `np=1/2/3/4`, including idle ranks.

Out of scope:

- Site/slice parallelism inside `dqmc_sweep()`.
- Distributed Green matrices or distributed BLAS/LAPACK.
- MPI temperature splitting.
- `parallel=hybrid`; it remains a reserved mode.
- Per-rank profiler CSV files.

Commit examples use `Codex (GPT-5)` because this plan was written for the current Codex session. If another model executes the plan, replace the trailer with the actual model used, following `AGENTS.md`.

## Plan Review Fixes

`docs/reviews/2026-06-26-dqmc-mpi-replica-parallelization-plan-review.md` の指摘を実装手順へ反映済み。

- H1: MPI branch は beta 内で status/bin/profiler/root output/cleanup まで完結し、末尾で `continue` する。既存 serial/OpenMP 後処理 tail は MPI path から到達しない。
- H2: rank0-only setup failure は beta loop 前に、rank0-only close failure は beta loop 後に `mpi_any_failed()` で全 rank 共有する。
- H3: 非 root rank の global `Profiler prof` は `profiler_init_memory(&prof, p.profile)` で必ず初期化する。
- M1: `alloc_replica_arrays(0, ...)` は idle rank 用の成功ケースとして扱う。
- M2/M3: MPI beta branch の全リソースと failure flag は分岐先頭で NULL/0 初期化し、cleanup label が未初期化変数を触らないようにする。
- L1/L2/L3/L4: OpenMP capability error の優先順位を簡素化し、`profile=0` では profiler reduce を実行せず、Task 11 の CSV 出力先は run directory に固定し、不要な `local_seeds` コピーを使わない。

## File Map

- Modify: `.gitignore`
  - Add `/dqmc_mpi`; existing `*.o` and `tests/test_*` already cover MPI object and test binaries.
- Modify: `Makefile`
  - Add `MPICC`, `MPIRUN`, `MPI_CFLAGS`, `MPI_OBJ`, `TESTBIN_MPI`, `dqmc_mpi`, `tests/test_%_mpi`, `test_mpi`, and MPI clean targets.
- Create: `src/replica_mpi.h`
  - Declare MPI-independent helper functions for rank assignment, Gatherv layout, failed status, and `ReplicaBin` pack/unpack.
- Create: `src/replica_mpi.c`
  - Implement helper functions without including `mpi.h`, so serial builds stay MPI-free.
- Create: `tests/test_mpi_replica.c`
  - Unit test rank ranges, idle ranks, Gatherv layout, failed status composition, and bin pack/unpack.
- Modify: `src/profiler.h`
  - Add `nranks` metadata and `profiler_set_mpi_metadata()`.
- Modify: `src/profiler.c`
  - Keep serial/OpenMP CSV schema unchanged; append `nranks` only in MPI build rows.
- Modify: `tests/test_profiler.c`
  - Add `#ifdef AFQMC_USE_MPI` assertions for the MPI profiler schema.
- Modify: `src/main.c`
  - Add MPI bootstrap/finalize helpers, rank0 I/O guards, mode validation priority, MPI replica execution, status gather, bin gather, profiler reduce, and collective failure handling.
- Modify: `LOG.md`
  - Record plan creation now; record implementation and validation results after execution.
- Create during verification: `/tmp/afqmc_mpi_*.in`, `/tmp/afqmc_mpi_*.out`, `/tmp/afqmc_mpi_*.csv`
  - These temporary files are not committed.
- Create during final performance smoke: `data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626/`
  - Commit only after the user asks to preserve run data.

## Task 1: Add Optional MPI Build Targets

**Files:**
- Modify: `.gitignore`
- Modify: `Makefile`

- [ ] **Step 1: Add the ignored executable name**

Append this line under the existing build artifacts in `.gitignore`:

```gitignore
/dqmc_mpi
```

- [ ] **Step 2: Add MPI variables to `Makefile`**

Insert these definitions after the OpenMP variable block and before `SRC`:

```make
MPICC ?= mpicc
MPIRUN ?= mpirun
MPI_CFLAGS = -DAFQMC_USE_MPI
```

Extend the object and test variable block:

```make
SRC  = $(wildcard src/*.c)
OBJ  = $(SRC:.c=.o)
OMP_OBJ = $(SRC:.c=.omp.o)
MPI_OBJ = $(SRC:.c=.mpi.o)
TESTS = $(wildcard tests/test_*.c)
TESTBIN = $(TESTS:.c=)
TESTBIN_OMP = $(patsubst tests/test_%.c,tests/test_%_omp,$(TESTS))
TESTBIN_MPI = $(patsubst tests/test_%.c,tests/test_%_mpi,$(TESTS))
```

Add the MPI object rule and executable:

```make
src/%.mpi.o: src/%.c
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) -c -o $@ $<

dqmc_mpi: $(MPI_OBJ)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) -o $@ $(MPI_OBJ) $(LDLIBS)
```

Add MPI test linkage after the OpenMP test linkage:

```make
LIBSRC_MPI = $(filter-out src/main.mpi.o,$(MPI_OBJ))
tests/test_%_mpi: tests/test_%.c $(LIBSRC_MPI)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) -o $@ $< $(LIBSRC_MPI) $(LDLIBS)
```

Add the MPI test target:

```make
test_mpi: $(TESTBIN_MPI)
	@fail=0; for t in $(TESTBIN_MPI); do printf "%-32s " $$t; $(MPIRUN) -np 1 ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME MPI TESTS FAILED"; exit 1; fi; echo "ALL MPI TESTS PASSED"
```

Extend `clean` and `.PHONY`:

```make
clean:
	rm -f src/*.o src/*.omp.o src/*.mpi.o dqmc dqmc_omp dqmc_mpi $(TESTBIN) $(TESTBIN_OMP) $(TESTBIN_MPI)

.PHONY: test test_omp test_mpi clean
```

- [ ] **Step 3: Verify serial targets still work**

Run:

```bash
make clean
make test
```

Expected final line:

```text
ALL TESTS PASSED
```

- [ ] **Step 4: Verify MPI target compiles before MPI logic is added**

Run:

```bash
command -v mpicc
command -v mpirun
make dqmc_mpi
make test_mpi
```

Expected:

- `command -v mpicc` prints an absolute path.
- `command -v mpirun` prints an absolute path.
- `make dqmc_mpi` exits 0.
- `make test_mpi` prints `ALL MPI TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add .gitignore Makefile
git commit -m "build: add optional MPI target for DQMC" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 2: Add MPI Replica Helper Unit Tests

**Files:**
- Create: `src/replica_mpi.h`
- Create: `src/replica_mpi.c`
- Create: `tests/test_mpi_replica.c`

- [ ] **Step 1: Create the failing helper test**

Create `tests/test_mpi_replica.c`:

```c
#include "test_util.h"
#include "measure.h"
#include "replica.h"
#include "replica_mpi.h"

#include <math.h>
#include <stdlib.h>

static void fill_bin(ReplicaBin *bin, double scale)
{
    MeasSample s;
    s.ekin = -2.0 * scale;
    s.eint = 0.5 * scale;
    s.ntot = 4.0 * scale;
    s.doublon = 0.125 * scale;
    s.E = -1.5 * scale;
    replica_bin_add(bin, &s, 2.0, 4.0, 4, 1.0);
}

int main(void)
{
    int first = -1;
    int count = -1;

    replica_mpi_rank_range(10, 3, 0, &first, &count);
    CHECK(first == 0);
    CHECK(count == 4);
    replica_mpi_rank_range(10, 3, 1, &first, &count);
    CHECK(first == 4);
    CHECK(count == 3);
    replica_mpi_rank_range(10, 3, 2, &first, &count);
    CHECK(first == 7);
    CHECK(count == 3);

    replica_mpi_rank_range(2, 4, 0, &first, &count);
    CHECK(first == 0);
    CHECK(count == 1);
    replica_mpi_rank_range(2, 4, 1, &first, &count);
    CHECK(first == 1);
    CHECK(count == 1);
    replica_mpi_rank_range(2, 4, 2, &first, &count);
    CHECK(first == 2);
    CHECK(count == 0);
    replica_mpi_rank_range(2, 4, 3, &first, &count);
    CHECK(first == 2);
    CHECK(count == 0);

    CHECK(replica_mpi_rank_for_replica(10, 3, 0) == 0);
    CHECK(replica_mpi_rank_for_replica(10, 3, 3) == 0);
    CHECK(replica_mpi_rank_for_replica(10, 3, 4) == 1);
    CHECK(replica_mpi_rank_for_replica(10, 3, 7) == 2);
    CHECK(replica_mpi_rank_for_replica(2, 4, 0) == 0);
    CHECK(replica_mpi_rank_for_replica(2, 4, 1) == 1);

    int recvcounts[4] = {0};
    int displs[4] = {0};
    replica_mpi_gatherv_layout(5, 2, 3, REPLICA_MPI_BIN_DOUBLES,
                               recvcounts, displs);
    CHECK(recvcounts[0] == 4 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(recvcounts[1] == 4 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(recvcounts[2] == 2 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(displs[0] == 0);
    CHECK(displs[1] == 2 * 2 * REPLICA_MPI_BIN_DOUBLES);
    CHECK(displs[2] == 4 * 2 * REPLICA_MPI_BIN_DOUBLES);

    ReplicaResult ok = {0};
    ok.status = 0;
    ReplicaResult bad = {0};
    bad.status = 7;
    CHECK(replica_mpi_local_failed(0, &ok) == 0);
    CHECK(replica_mpi_local_failed(1, &ok) == 1);
    CHECK(replica_mpi_local_failed(0, &bad) == 1);
    CHECK(replica_mpi_local_failed(1, &bad) == 1);
    CHECK(replica_mpi_local_failed(0, NULL) == 1);

    ReplicaResult results[2] = {0};
    CHECK(replica_result_alloc(&results[0], 2) == 0);
    CHECK(replica_result_alloc(&results[1], 2) == 0);
    fill_bin(&results[0].bins[0], 1.0);
    fill_bin(&results[0].bins[1], 2.0);
    fill_bin(&results[1].bins[0], 3.0);
    fill_bin(&results[1].bins[1], 4.0);

    double values[2 * 2 * REPLICA_MPI_BIN_DOUBLES] = {0.0};
    int counts[2 * 2] = {0};
    replica_mpi_pack_bins(results, 2, 2, values, counts);

    ReplicaBin unpacked[4] = {{0}};
    replica_mpi_unpack_bins(values, counts, 4, unpacked);
    for (int i = 0; i < 4; i++) {
        double eh = 0.0;
        double eg = 0.0;
        double ep = 0.0;
        double nn = 0.0;
        double dd = 0.0;
        double ss = 0.0;
        CHECK(replica_bin_values(&unpacked[i], &eh, &eg, &ep, &nn, &dd, &ss) == 0);
        CHECK(counts[i] == 1);
        CHECK(fabs(ss - 1.0) < 1e-12);
    }
    CHECK(fabs(unpacked[0].sum_sign_Ehub - results[0].bins[0].sum_sign_Ehub) < 1e-12);
    CHECK(fabs(unpacked[3].sum_sign_D - results[1].bins[1].sum_sign_D) < 1e-12);

    replica_result_free(&results[0]);
    replica_result_free(&results[1]);

    TEST_END();
}
```

- [ ] **Step 2: Run the new test to verify it fails**

Run:

```bash
make tests/test_mpi_replica
```

Expected: compile failure with `replica_mpi.h` not found.

- [ ] **Step 3: Add the helper header**

Create `src/replica_mpi.h`:

```c
#ifndef REPLICA_MPI_H
#define REPLICA_MPI_H

#include "replica.h"

#define REPLICA_MPI_BIN_DOUBLES 6

void replica_mpi_rank_range(int nrep, int nranks, int rank, int *first_replica,
                            int *local_nrep);
int replica_mpi_rank_for_replica(int nrep, int nranks, int replica_id);
void replica_mpi_gatherv_layout(int nrep, int nbin, int nranks,
                                int item_width, int *recvcounts,
                                int *displs);
int replica_mpi_local_failed(int run_failed, const ReplicaResult *result);
void replica_mpi_pack_bins(const ReplicaResult *results, int local_nrep,
                           int nbin, double *values, int *counts);
void replica_mpi_unpack_bins(const double *values, const int *counts,
                             int total_bins, ReplicaBin *bins);

#endif
```

- [ ] **Step 4: Add the helper implementation**

Create `src/replica_mpi.c`:

```c
#include "replica_mpi.h"

static int min_int(int a, int b)
{
    return (a < b) ? a : b;
}

void replica_mpi_rank_range(int nrep, int nranks, int rank, int *first_replica,
                            int *local_nrep)
{
    if (first_replica != NULL) {
        *first_replica = 0;
    }
    if (local_nrep != NULL) {
        *local_nrep = 0;
    }
    if (nrep < 0 || nranks <= 0 || rank < 0 || rank >= nranks) {
        return;
    }

    const int base = nrep / nranks;
    const int rem = nrep % nranks;
    const int count = base + (rank < rem ? 1 : 0);
    const int first = rank * base + min_int(rank, rem);

    if (first_replica != NULL) {
        *first_replica = first;
    }
    if (local_nrep != NULL) {
        *local_nrep = count;
    }
}

int replica_mpi_rank_for_replica(int nrep, int nranks, int replica_id)
{
    if (nrep <= 0 || nranks <= 0 || replica_id < 0 || replica_id >= nrep) {
        return -1;
    }
    for (int rank = 0; rank < nranks; rank++) {
        int first = 0;
        int count = 0;
        replica_mpi_rank_range(nrep, nranks, rank, &first, &count);
        if (replica_id >= first && replica_id < first + count) {
            return rank;
        }
    }
    return -1;
}

void replica_mpi_gatherv_layout(int nrep, int nbin, int nranks,
                                int item_width, int *recvcounts,
                                int *displs)
{
    if (recvcounts == NULL || displs == NULL || nranks <= 0 || nbin < 0 ||
        item_width < 0) {
        return;
    }
    for (int rank = 0; rank < nranks; rank++) {
        int first = 0;
        int count = 0;
        replica_mpi_rank_range(nrep, nranks, rank, &first, &count);
        recvcounts[rank] = count * nbin * item_width;
        displs[rank] = first * nbin * item_width;
    }
}

int replica_mpi_local_failed(int run_failed, const ReplicaResult *result)
{
    if (run_failed) {
        return 1;
    }
    if (result == NULL) {
        return 1;
    }
    return result->status != 0;
}

void replica_mpi_pack_bins(const ReplicaResult *results, int local_nrep,
                           int nbin, double *values, int *counts)
{
    if (results == NULL || values == NULL || counts == NULL || local_nrep < 0 ||
        nbin < 0) {
        return;
    }
    for (int r = 0; r < local_nrep; r++) {
        for (int bi = 0; bi < nbin; bi++) {
            const int flat = r * nbin + bi;
            const ReplicaBin *bin = &results[r].bins[bi];
            values[flat * REPLICA_MPI_BIN_DOUBLES + 0] = bin->sum_sign_Ehub;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 1] = bin->sum_sign_Egc;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 2] = bin->sum_sign_Eph;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 3] = bin->sum_sign_N;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 4] = bin->sum_sign_D;
            values[flat * REPLICA_MPI_BIN_DOUBLES + 5] = bin->sum_sign;
            counts[flat] = bin->count;
        }
    }
}

void replica_mpi_unpack_bins(const double *values, const int *counts,
                             int total_bins, ReplicaBin *bins)
{
    if (values == NULL || counts == NULL || bins == NULL || total_bins < 0) {
        return;
    }
    for (int i = 0; i < total_bins; i++) {
        ReplicaBin *bin = &bins[i];
        bin->sum_sign_Ehub = values[i * REPLICA_MPI_BIN_DOUBLES + 0];
        bin->sum_sign_Egc = values[i * REPLICA_MPI_BIN_DOUBLES + 1];
        bin->sum_sign_Eph = values[i * REPLICA_MPI_BIN_DOUBLES + 2];
        bin->sum_sign_N = values[i * REPLICA_MPI_BIN_DOUBLES + 3];
        bin->sum_sign_D = values[i * REPLICA_MPI_BIN_DOUBLES + 4];
        bin->sum_sign = values[i * REPLICA_MPI_BIN_DOUBLES + 5];
        bin->count = counts[i];
    }
}
```

- [ ] **Step 5: Run helper tests**

Run:

```bash
make tests/test_mpi_replica
./tests/test_mpi_replica
make test
```

Expected:

```text
OK
ALL TESTS PASSED
```

- [ ] **Step 6: Run MPI test target**

Run:

```bash
make test_mpi
```

Expected final line:

```text
ALL MPI TESTS PASSED
```

- [ ] **Step 7: Commit**

```bash
git add src/replica_mpi.h src/replica_mpi.c tests/test_mpi_replica.c
git commit -m "test: add MPI replica helper coverage" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 3: Add MPI Profiler Metadata Without Changing Serial Schema

**Files:**
- Modify: `src/profiler.h`
- Modify: `src/profiler.c`
- Modify: `tests/test_profiler.c`

- [ ] **Step 1: Add the failing MPI profiler assertions**

In `tests/test_profiler.c`, after the existing merged profiler assertions, add:

```c
#ifdef AFQMC_USE_MPI
    Profiler mpi_prof;
    const char *mpi_path = "/tmp/afqmc_profile_mpi.csv";
    remove(mpi_path);
    profiler_init(&mpi_prof, 1, mpi_path);
    profiler_set_mpi_metadata(&mpi_prof, 5, "mpi", 3);
    profiler_beta_begin(&mpi_prof, 4.0, 0.25, 0.1, 40);
    profiler_phase_set(&mpi_prof, PROF_PHASE_MEASUREMENT);
    profiler_add(&mpi_prof, PROF_DQMC_SWEEP, 2.0);
    profiler_beta_end(&mpi_prof);
    profiler_close(&mpi_prof);
    CHECK(!profiler_error(&mpi_prof));
    CHECK(file_contains(mpi_path,
                        "wall_sec,thread_total_sec,nrep,parallel,nranks"));
    CHECK(file_contains(mpi_path, ",5,mpi,3"));
#endif
```

- [ ] **Step 2: Run MPI profiler test to verify it fails**

Run:

```bash
make tests/test_profiler_mpi
```

Expected: compile failure because `profiler_set_mpi_metadata` is not declared.

- [ ] **Step 3: Extend the profiler API**

In `src/profiler.h`, add `nranks` after `nrep` in `Profiler`:

```c
    int nrep;
    int nranks;
    char parallel[16];
```

Add the function declaration after `profiler_set_metadata()`:

```c
void profiler_set_mpi_metadata(Profiler *p, int nrep, const char *parallel,
                               int nranks);
```

- [ ] **Step 4: Implement MPI metadata and schema selection**

In `src/profiler.c`, initialize `nranks` in `init_common()`:

```c
    p->nrep = 1;
    p->nranks = 1;
    strcpy(p->parallel, "serial");
```

Add the setter after `profiler_set_metadata()`:

```c
void profiler_set_mpi_metadata(Profiler *p, int nrep, const char *parallel,
                               int nranks)
{
    profiler_set_metadata(p, nrep, parallel);
    if (p != NULL) {
        p->nranks = (nranks > 0) ? nranks : 1;
    }
}
```

Replace the profiler header write in `profiler_init()` with:

```c
#ifdef AFQMC_USE_MPI
    fprintf(p->fp,
            "beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta,wall_sec,thread_total_sec,nrep,parallel,nranks\n");
#else
    fprintf(p->fp,
            "beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta,wall_sec,thread_total_sec,nrep,parallel\n");
#endif
```

Replace the row write in `profiler_beta_end()` with:

```c
#ifdef AFQMC_USE_MPI
            fprintf(p->fp,
                    "%.17g,%.17g,%.17g,%d,%s,%s,%llu,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%s,%d\n",
                    p->beta, p->T, p->dtau, p->Ltr,
                    phase_name((ProfPhase)ph), region_name((ProfRegion)rg),
                    s.calls, thread_total, avg, frac, wall_sec, thread_total,
                    p->nrep, p->parallel, p->nranks);
#else
            fprintf(p->fp,
                    "%.17g,%.17g,%.17g,%d,%s,%s,%llu,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%s\n",
                    p->beta, p->T, p->dtau, p->Ltr,
                    phase_name((ProfPhase)ph), region_name((ProfRegion)rg),
                    s.calls, thread_total, avg, frac, wall_sec, thread_total,
                    p->nrep, p->parallel);
#endif
```

- [ ] **Step 5: Verify serial schema is unchanged and MPI schema appends `nranks`**

Run:

```bash
make tests/test_profiler && ./tests/test_profiler
head -1 /tmp/afqmc_profile_enabled.csv
make tests/test_profiler_mpi && ./tests/test_profiler_mpi
head -1 /tmp/afqmc_profile_mpi.csv
```

Expected:

```text
OK
beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta,wall_sec,thread_total_sec,nrep,parallel
OK
beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta,wall_sec,thread_total_sec,nrep,parallel,nranks
```

- [ ] **Step 6: Run full test sets**

Run:

```bash
make test
make test_mpi
```

Expected:

```text
ALL TESTS PASSED
ALL MPI TESTS PASSED
```

- [ ] **Step 7: Commit**

```bash
git add src/profiler.h src/profiler.c tests/test_profiler.c
git commit -m "feat: add MPI profiler metadata" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 4: Add MPI Bootstrap, Rank0 Guards, and Mode Validation

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Add MPI include and small helpers**

Add after the OpenMP include block in `src/main.c`:

```c
#ifdef AFQMC_USE_MPI
#include <mpi.h>
#endif

typedef struct {
    int enabled;
    int rank;
    int nranks;
} MpiEnv;

static int mpi_is_root(const MpiEnv *env)
{
    return env == NULL || !env->enabled || env->rank == 0;
}

static int mpi_any_failed(const MpiEnv *env, int local_failed)
{
#ifdef AFQMC_USE_MPI
    if (env != NULL && env->enabled) {
        int global_failed = 0;
        MPI_Allreduce(&local_failed, &global_failed, 1, MPI_INT, MPI_MAX,
                      MPI_COMM_WORLD);
        return global_failed;
    }
#else
    (void)env;
#endif
    return local_failed;
}
```

- [ ] **Step 2: Initialize and finalize MPI in every path**

At the top of `main()` before the `argc < 2` check, add:

```c
    MpiEnv mpi_env;
    mpi_env.enabled = 0;
    mpi_env.rank = 0;
    mpi_env.nranks = 1;
#ifdef AFQMC_USE_MPI
    MPI_Init(&argc, &argv);
    mpi_env.enabled = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_env.rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_env.nranks);
#endif
```

Before every existing `return 1;` or `return 0;` in `main()`, finalize MPI in MPI builds:

```c
#ifdef AFQMC_USE_MPI
        MPI_Finalize();
#endif
        return 1;
```

For the final success path:

```c
#ifdef AFQMC_USE_MPI
    MPI_Finalize();
#endif
    return 0;
```

During implementation, do this mechanically and run `rg "return [01];" src/main.c` afterward. Each return in `main()` must be preceded by `MPI_Finalize()` under `#ifdef AFQMC_USE_MPI`.

- [ ] **Step 3: Guard stderr/stdout and rank0-only files**

Replace direct error prints with root-guarded prints in deterministic setup errors:

```c
    if (argc < 2) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr, "usage: %s input.txt\n", argv[0]);
        }
#ifdef AFQMC_USE_MPI
        MPI_Finalize();
#endif
        return 1;
    }
```

Guard `lattice_dump`:

```c
    if (mpi_is_root(&mpi_env)) {
        (void)lattice_dump(&L, "hopping_used.txt");
    }
```

Guard the initial stdout header:

```c
    if (mpi_is_root(&mpi_env)) {
        printf("# lattice=%s n=%d U=%g mu=%g dtau=%g bipartite=%d parallel=%s nrep=%d bins=%d",
               p.lattice, L.n, p.U, mu, p.dtau, L.is_bipartite, p.parallel,
               p.nrep, p.nrep * p.nbin);
        if (strcmp(p.parallel, "mpi") == 0) {
            printf(" nranks=%d", mpi_env.nranks);
        }
        printf("\n");
        printf("# T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign\n");
    }
```

Guard `profiler_init()` and `replica_log` open so only rank 0 opens output files. Non-root ranks must still initialize `prof` as a memory profiler; never leave `prof` uninitialized.

```c
    Profiler prof;
    int setup_failed = 0;
    if (mpi_is_root(&mpi_env)) {
        profiler_init(&prof, p.profile, p.profile_file);
        if (profiler_error(&prof)) {
            fprintf(stderr, "ERROR: failed to initialize profiler output\n");
            setup_failed = 1;
        }
    } else {
        profiler_init_memory(&prof, p.profile);
    }
    profiler_set_current(&prof);
```

Open `replica_log` only on rank 0 and turn open failure into the same `setup_failed` flag:

```c
    FILE *replica_fp = NULL;
    const int write_replica_log =
        strcmp(p.replica_log, "none") != 0 &&
        (p.replica_log[0] != '\0' || p.nrep > 1);
    const char *replica_log_path =
        (p.replica_log[0] != '\0') ? p.replica_log : "replicas.csv";
    if (write_replica_log && mpi_is_root(&mpi_env)) {
        replica_fp = fopen(replica_log_path, "w");
        if (replica_fp == NULL) {
            fprintf(stderr, "ERROR: failed to open replica_log %s\n",
                    replica_log_path);
            setup_failed = 1;
        }
    }
```

Before entering the beta loop, share setup failures collectively:

```c
    if (mpi_any_failed(&mpi_env, setup_failed)) {
        profiler_set_current(NULL);
        profiler_close(&prof);
        lattice_free(&L);
#ifdef AFQMC_USE_MPI
        MPI_Finalize();
#endif
        return 1;
    }
```

- [ ] **Step 4: Implement validation priority**

Replace the existing `parallel=mpi` and `parallel=hybrid` guards with this ordering:

```c
    if (strcmp(p.parallel, "hybrid") == 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: parallel=hybrid is reserved for future hybrid implementation\n");
        }
        /* cleanup and finalize */
        return 1;
    }
#ifndef AFQMC_USE_OPENMP
    if (strcmp(p.parallel, "omp") == 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr, "ERROR: parallel=omp requires OpenMP-enabled build\n");
        }
        /* cleanup and finalize */
        return 1;
    }
#endif
#ifndef AFQMC_USE_MPI
    if (strcmp(p.parallel, "mpi") == 0) {
        fprintf(stderr, "ERROR: parallel=mpi requires MPI-enabled build\n");
        /* cleanup */
        return 1;
    }
#else
    if (mpi_env.nranks > 1 && strcmp(p.parallel, "serial") == 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: parallel=serial under MPI requires mpirun -np 1\n");
        }
        /* cleanup and finalize */
        return 1;
    }
#ifdef AFQMC_USE_OPENMP
    if (mpi_env.nranks > 1 && strcmp(p.parallel, "omp") == 0) {
        if (mpi_is_root(&mpi_env)) {
            fprintf(stderr,
                    "ERROR: parallel=omp under MPI requires mpirun -np 1\n");
        }
        /* cleanup and finalize */
        return 1;
    }
#endif
#endif
```

When writing the actual code, keep one concrete cleanup block per location because C has no defer. The exact cleanup must match the objects already allocated at that point.

- [ ] **Step 5: Share close failures after the beta loop**

At shutdown, only rank 0 closes file-backed outputs. Convert close errors into a root flag and share it before `MPI_Finalize()`:

```c
    int close_failed = 0;
    profiler_set_current(NULL);
    if (mpi_is_root(&mpi_env)) {
        if (replica_fp != NULL && fclose(replica_fp) != 0) {
            close_failed = 1;
        }
        profiler_close(&prof);
        if (profiler_error(&prof)) {
            close_failed = 1;
        }
    } else {
        profiler_close(&prof);
    }
    lattice_free(&L);
    if (mpi_any_failed(&mpi_env, close_failed)) {
#ifdef AFQMC_USE_MPI
        MPI_Finalize();
#endif
        return 1;
    }
#ifdef AFQMC_USE_MPI
    MPI_Finalize();
#endif
    return 0;
```

- [ ] **Step 6: Verify mode errors and startup failure**

Create temporary inputs:

```bash
cat > /tmp/afqmc_parallel_mpi.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=2
nmeas=4
nbin=2
parallel=mpi
EOF

cat > /tmp/afqmc_parallel_serial.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=2
nmeas=4
nbin=2
parallel=serial
EOF

cat > /tmp/afqmc_parallel_hybrid.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=2
nmeas=4
nbin=2
parallel=hybrid
EOF
```

Run:

```bash
make dqmc
./dqmc /tmp/afqmc_parallel_mpi.in > /tmp/afqmc_serial_mpi.out 2> /tmp/afqmc_serial_mpi.err; test $? -ne 0
grep 'ERROR: parallel=mpi requires MPI-enabled build' /tmp/afqmc_serial_mpi.err

make dqmc_mpi
mpirun -np 2 ./dqmc_mpi /tmp/afqmc_parallel_serial.in > /tmp/afqmc_np2_serial.out 2> /tmp/afqmc_np2_serial.err; test $? -ne 0
grep 'ERROR: parallel=serial under MPI requires mpirun -np 1' /tmp/afqmc_np2_serial.err

mpirun -np 1 ./dqmc_mpi /tmp/afqmc_parallel_hybrid.in > /tmp/afqmc_hybrid.out 2> /tmp/afqmc_hybrid.err; test $? -ne 0
grep 'ERROR: parallel=hybrid is reserved for future hybrid implementation' /tmp/afqmc_hybrid.err

cat > /tmp/afqmc_mpi_bad_log.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=2
nmeas=4
nbin=2
parallel=mpi
nrep=4
replica_log=/no_such_dir/afqmc_replicas.csv
EOF
mpirun -np 2 ./dqmc_mpi /tmp/afqmc_mpi_bad_log.in > /tmp/afqmc_bad_log.out 2> /tmp/afqmc_bad_log.err; test $? -ne 0
grep 'ERROR: failed to open replica_log' /tmp/afqmc_bad_log.err
```

Expected: all `grep` commands print one matching line and no MPI command hangs.

- [ ] **Step 7: Run tests**

```bash
make test
make test_mpi
```

Expected:

```text
ALL TESTS PASSED
ALL MPI TESTS PASSED
```

- [ ] **Step 8: Commit**

```bash
git add src/main.c
git commit -m "feat: add MPI bootstrap and mode validation" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 5: Run Rank-Local Replicas in MPI Mode

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Add local allocation helpers for MPI mode**

Add this static helper near `free_replica_arrays()`:

```c
static int alloc_replica_arrays(int nrep, unsigned long long **seeds,
                                ReplicaResult **results,
                                Profiler **replica_profs,
                                int **replica_failed)
{
    if (seeds != NULL) {
        *seeds = NULL;
    }
    *results = NULL;
    *replica_profs = NULL;
    *replica_failed = NULL;
    if (nrep < 0) {
        return 1;
    }
    if (nrep == 0) {
        return 0;
    }
    if (seeds != NULL) {
        *seeds = calloc((size_t)nrep, sizeof(unsigned long long));
    }
    *results = calloc((size_t)nrep, sizeof(ReplicaResult));
    *replica_profs = calloc((size_t)nrep, sizeof(Profiler));
    *replica_failed = calloc((size_t)nrep, sizeof(int));
    if ((seeds != NULL && *seeds == NULL) || *results == NULL ||
        *replica_profs == NULL || *replica_failed == NULL) {
        free_replica_arrays(seeds != NULL ? *seeds : NULL, *results,
                            *replica_profs, *replica_failed,
                            nrep);
        if (seeds != NULL) {
            *seeds = NULL;
        }
        *results = NULL;
        *replica_profs = NULL;
        *replica_failed = NULL;
        return 1;
    }
    return 0;
}
```

Use it in the existing serial/OpenMP path as well, replacing the repeated `calloc()` block.

For MPI-local arrays, pass `NULL` for `seeds` because local execution reads from the global seed table:

```c
alloc_replica_arrays(local_nrep, NULL, &local_results,
                     &local_replica_profs, &local_replica_failed);
```

- [ ] **Step 2: Add a rank-local execution helper**

Add a helper that executes local global replica ids:

```c
static int run_replica_range(const Params *p, const Lattice *L, int beta_index,
                             int Ltr, double mu, double beta, double T,
                             int first_replica, int local_nrep,
                             const unsigned long long *global_seeds,
                             const char *parallel_label,
                             ReplicaResult *results,
                             Profiler *replica_profs,
                             int *replica_failed)
{
    for (int local = 0; local < local_nrep; local++) {
        const int gid = first_replica + local;
        profiler_init_memory(&replica_profs[local], p->profile);
        profiler_set_metadata(&replica_profs[local], 1, parallel_label);
        profiler_beta_begin(&replica_profs[local], beta, T, p->dtau, Ltr);
        if (dqmc_run_replica(p, L, beta_index, Ltr, mu, gid,
                             global_seeds[gid], &replica_profs[local],
                             &results[local]) != 0) {
            replica_failed[local] = 1;
        }
    }
    return 0;
}
```

For OpenMP, keep the existing `#pragma omp parallel for` loop because Task 5 is only about MPI path. A later cleanup may use the helper for serial and OpenMP, but this implementation does not require that refactor.

- [ ] **Step 3: Add MPI branch inside each beta as a complete block**

Inside the beta loop, after global `seeds` are generated and uniqueness is checked, branch on `parallel=mpi`. The MPI branch owns all MPI postprocessing and cleanup for that beta, then ends with `continue;` so the existing serial/OpenMP tail cannot process empty global `results`.

Declare every MPI beta resource and failure flag at the top of the branch. Later tasks will fill in status gather, bin gather, and profiler reduce using these already-initialized variables.

```c
#ifdef AFQMC_USE_MPI
        if (strcmp(p.parallel, "mpi") == 0) {
            int mpi_beta_failed = 0;
            int local_setup_failed = 0;
            int first_replica = 0;
            int local_nrep = 0;
            int local_bins = 0;
            int total_bins = p.nrep * p.nbin;
            int any_failed = 0;
            int status_alloc_failed = 0;
            int gather_alloc_failed = 0;
            int root_post_failed = 0;

            ReplicaResult *local_results = NULL;
            Profiler *local_replica_profs = NULL;
            int *local_replica_failed = NULL;
            int *local_failed = NULL;
            int *all_failed = NULL;
            int *status_counts = NULL;
            int *status_displs = NULL;
            double *local_values = NULL;
            int *local_counts = NULL;
            double *all_values = NULL;
            int *all_counts = NULL;
            int *value_counts = NULL;
            int *value_displs = NULL;
            int *count_counts = NULL;
            int *count_displs = NULL;
            ReplicaBin *global_bins = NULL;
            double *Ehub = NULL;
            double *Egc = NULL;
            double *Eph = NULL;
            double *Nbin = NULL;
            double *Dbin = NULL;
            double *Sbin = NULL;
            Profiler rank_prof;
            profiler_init_memory(&rank_prof, p.profile);
            profiler_set_mpi_metadata(&rank_prof, p.nrep, "mpi",
                                      mpi_env.nranks);
            profiler_beta_begin(&rank_prof, beta, T, p.dtau, Ltr);

            replica_mpi_rank_range(p.nrep, mpi_env.nranks, mpi_env.rank,
                                   &first_replica, &local_nrep);
            local_bins = local_nrep * p.nbin;

            if (alloc_replica_arrays(local_nrep, NULL, &local_results,
                                     &local_replica_profs,
                                     &local_replica_failed) != 0) {
                local_setup_failed = 1;
            }
            if (!local_setup_failed) {
                run_replica_range(&p, &L, b, Ltr, mu, beta, T, first_replica,
                                  local_nrep, seeds, "mpi", local_results,
                                  local_replica_profs, local_replica_failed);
                for (int local = 0; local < local_nrep; local++) {
                    profiler_merge(&rank_prof, &local_replica_profs[local]);
                }
            }
            /* status gather is added in Task 6 */

            if (mpi_is_root(&mpi_env)) {
                fprintf(stderr,
                        "ERROR: MPI status gather is not active in this build step\n");
            }
            mpi_beta_failed = 1;

        mpi_beta_cleanup:
            free(local_failed);
            free(all_failed);
            free(status_counts);
            free(status_displs);
            free(local_values);
            free(local_counts);
            free(all_values);
            free(all_counts);
            free(value_counts);
            free(value_displs);
            free(count_counts);
            free(count_displs);
            free(global_bins);
            free(Ehub);
            free(Egc);
            free(Eph);
            free(Nbin);
            free(Dbin);
            free(Sbin);
            free_replica_arrays(NULL, local_results, local_replica_profs,
                                local_replica_failed, local_nrep);
            if (mpi_any_failed(&mpi_env, mpi_beta_failed)) {
                free_replica_arrays(seeds, results, replica_profs,
                                    replica_failed, p.nrep);
                if (replica_fp != NULL) {
                    fclose(replica_fp);
                }
                profiler_set_current(NULL);
                profiler_close(&prof);
                lattice_free(&L);
#ifdef AFQMC_USE_MPI
                MPI_Finalize();
#endif
                return 1;
            }
            free_replica_arrays(seeds, results, replica_profs, replica_failed,
                                p.nrep);
            continue;
        } else
#endif
```

Use `seeds` as the global seed table for `dqmc_run_replica()`; do not create or copy a local seed table. The non-MPI legacy tail must remain reachable only through the `else` branch.

- [ ] **Step 4: Compile and run `np=1` until the planned status-gather error point**

During this task, `parallel=mpi` may run local replicas but can still return before physical output because Task 6 and Task 7 have not gathered status and bins. Verify that it does not hang:

```bash
make dqmc_mpi
mpirun -np 1 ./dqmc_mpi /tmp/afqmc_parallel_mpi.in > /tmp/afqmc_mpi_task5.out 2> /tmp/afqmc_mpi_task5.err; test $? -ne 0
```

Expected: exit code is nonzero with a clear error message from the incomplete MPI branch, and no hang. The error message should identify the next missing stage, for example:

```text
ERROR: MPI status gather is not active in this build step
```

- [ ] **Step 5: Run tests**

```bash
make test
make test_mpi
```

Expected:

```text
ALL TESTS PASSED
ALL MPI TESTS PASSED
```

- [ ] **Step 6: Commit**

```bash
git add src/main.c
git commit -m "feat: run rank-local replicas in MPI mode" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 6: Gather Status and Write MPI Replica Log

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Open MPI replica log on rank 0 with `rank` column**

When `write_replica_log` is true and `parallel=mpi`, rank 0 writes:

```c
fprintf(replica_fp,
        "beta,T,replica_id,seed,nwarm,nmeas,nbin,status,rank\n");
```

Keep the existing 8-column header for serial/OpenMP runs:

```c
fprintf(replica_fp, "beta,T,replica_id,seed,nwarm,nmeas,nbin,status\n");
```

- [ ] **Step 2: Gather local failed flags**

In the MPI branch after rank-local replicas finish:

```c
            local_failed = calloc((size_t)local_nrep, sizeof(int));
            status_alloc_failed = (local_failed == NULL && local_nrep > 0);
            if (mpi_env.rank == 0) {
                all_failed = calloc((size_t)p.nrep, sizeof(int));
                status_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
                status_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
                if (all_failed == NULL || status_counts == NULL ||
                    status_displs == NULL) {
                    status_alloc_failed = 1;
                }
            } else {
                status_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
                status_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
                if (status_counts == NULL || status_displs == NULL) {
                    status_alloc_failed = 1;
                }
            }
            if (mpi_any_failed(&mpi_env, status_alloc_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }

            replica_mpi_gatherv_layout(p.nrep, 1, mpi_env.nranks, 1,
                                       status_counts, status_displs);
            for (int local = 0; local < local_nrep; local++) {
                local_failed[local] = replica_mpi_local_failed(
                    local_replica_failed[local], &local_results[local]);
            }
            MPI_Gatherv(local_failed, local_nrep, MPI_INT, all_failed,
                        status_counts, status_displs, MPI_INT, 0,
                        MPI_COMM_WORLD);
```

This uses `replica_mpi_local_failed(run_failed, result)` so `replica_failed || result.status != 0` is fixed by test coverage.

- [ ] **Step 3: Write `replica_log` rows on rank 0 in global id order**

After status gather:

```c
            any_failed = 0;
            if (mpi_env.rank == 0) {
                for (int r = 0; r < p.nrep; r++) {
                    const int failed = all_failed[r] != 0;
                    const char *status = failed ? "error" : "ok";
                    const int owner =
                        replica_mpi_rank_for_replica(p.nrep, mpi_env.nranks, r);
                    if (replica_fp != NULL) {
                        fprintf(replica_fp,
                                "%.17g,%.17g,%d,%llu,%d,%d,%d,%s,%d\n",
                                beta, T, r, seeds[r], p.nwarm, p.nmeas,
                                p.nbin, status, owner);
                    }
                    if (failed) {
                        fprintf(stderr, "ERROR: replica %d failed\n", r);
                        any_failed = 1;
                    }
                }
                if (replica_fp != NULL && ferror(replica_fp)) {
                    fprintf(stderr, "ERROR: failed to write replica_log\n");
                    any_failed = 1;
                }
            }
            if (mpi_any_failed(&mpi_env, any_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }
```

- [ ] **Step 4: Verify replica log ordering and rank assignment**

Create a slightly larger input:

```bash
cat > /tmp/afqmc_mpi_nrep5.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=2
nmeas=4
nbin=2
parallel=mpi
nrep=5
replica_log=/tmp/afqmc_mpi_nrep5_replicas.csv
seed=246813579
EOF
```

Run after Task 7 is also active if this task still exits before bin gather:

```bash
make dqmc_mpi
mpirun -np 3 ./dqmc_mpi /tmp/afqmc_mpi_nrep5.in > /tmp/afqmc_mpi_nrep5.out
head -1 /tmp/afqmc_mpi_nrep5_replicas.csv
tail -n +2 /tmp/afqmc_mpi_nrep5_replicas.csv | cut -d, -f3,9
```

Expected header:

```text
beta,T,replica_id,seed,nwarm,nmeas,nbin,status,rank
```

Expected id/rank pairs:

```text
0,0
1,0
2,1
3,1
4,2
```

- [ ] **Step 5: Run tests**

```bash
make test
make test_mpi
```

Expected:

```text
ALL TESTS PASSED
ALL MPI TESTS PASSED
```

- [ ] **Step 6: Commit**

```bash
git add src/main.c
git commit -m "feat: gather MPI replica status and log ranks" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 7: Gather Bins and Produce MPI Physics Rows

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Pack local bins and gather flat arrays**

After status success in the MPI branch:

```c
            if (local_bins > 0) {
                local_values = calloc((size_t)local_bins * REPLICA_MPI_BIN_DOUBLES,
                                      sizeof(double));
                local_counts = calloc((size_t)local_bins, sizeof(int));
            }
            value_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
            value_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
            count_counts = calloc((size_t)mpi_env.nranks, sizeof(int));
            count_displs = calloc((size_t)mpi_env.nranks, sizeof(int));
            gather_alloc_failed =
                (local_bins > 0 && (local_values == NULL || local_counts == NULL)) ||
                value_counts == NULL || value_displs == NULL ||
                count_counts == NULL || count_displs == NULL;
            if (mpi_env.rank == 0) {
                all_values = calloc((size_t)total_bins * REPLICA_MPI_BIN_DOUBLES,
                                    sizeof(double));
                all_counts = calloc((size_t)total_bins, sizeof(int));
                if (all_values == NULL || all_counts == NULL) {
                    gather_alloc_failed = 1;
                }
            }
            if (mpi_any_failed(&mpi_env, gather_alloc_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }

            replica_mpi_pack_bins(local_results, local_nrep, p.nbin,
                                  local_values, local_counts);
            replica_mpi_gatherv_layout(p.nrep, p.nbin, mpi_env.nranks,
                                       REPLICA_MPI_BIN_DOUBLES,
                                       value_counts, value_displs);
            replica_mpi_gatherv_layout(p.nrep, p.nbin, mpi_env.nranks, 1,
                                       count_counts, count_displs);
            MPI_Gatherv(local_values,
                        local_bins * REPLICA_MPI_BIN_DOUBLES, MPI_DOUBLE,
                        all_values, value_counts, value_displs, MPI_DOUBLE, 0,
                        MPI_COMM_WORLD);
            MPI_Gatherv(local_counts, local_bins, MPI_INT, all_counts,
                        count_counts, count_displs, MPI_INT, 0,
                        MPI_COMM_WORLD);
```

- [ ] **Step 2: Reconstruct bins on rank 0 and reuse the existing jackknife code**

On rank 0:

```c
            root_post_failed = 0;
            if (mpi_env.rank == 0) {
                global_bins = calloc((size_t)total_bins, sizeof(ReplicaBin));
                Ehub = calloc((size_t)total_bins, sizeof(double));
                Egc = calloc((size_t)total_bins, sizeof(double));
                Eph = calloc((size_t)total_bins, sizeof(double));
                Nbin = calloc((size_t)total_bins, sizeof(double));
                Dbin = calloc((size_t)total_bins, sizeof(double));
                Sbin = calloc((size_t)total_bins, sizeof(double));
                root_post_failed =
                    global_bins == NULL || Ehub == NULL || Egc == NULL ||
                    Eph == NULL || Nbin == NULL || Dbin == NULL ||
                    Sbin == NULL;
                if (!root_post_failed) {
                    replica_mpi_unpack_bins(all_values, all_counts, total_bins,
                                            global_bins);
                    for (int out = 0; out < total_bins; out++) {
                        if (replica_bin_values(&global_bins[out], &Ehub[out],
                                               &Egc[out], &Eph[out],
                                               &Nbin[out], &Dbin[out],
                                               &Sbin[out]) != 0) {
                            fprintf(stderr,
                                    "ERROR: invalid zero-sign bin at beta=%g bin=%d\n",
                                    beta, out);
                            root_post_failed = 1;
                            break;
                        }
                    }
                }
            }
            if (mpi_any_failed(&mpi_env, root_post_failed)) {
                mpi_beta_failed = 1;
                goto mpi_beta_cleanup;
            }
```

Then run the existing jackknife and stdout code on rank 0 only. Keep the loop order as global replica id major, bin id minor:

```text
global flat index = replica_id * nbin + bin_id
```

- [ ] **Step 3: Verify `np=1` agrees with legacy serial for `nrep=1`**

Create matching serial and MPI inputs:

```bash
cat > /tmp/afqmc_serial_nrep1.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=4
nmeas=8
nbin=2
parallel=serial
nrep=1
seed=246813579
EOF

cat > /tmp/afqmc_mpi_nrep1.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=4
nmeas=8
nbin=2
parallel=mpi
nrep=1
seed=246813579
EOF
```

Run:

```bash
make dqmc dqmc_mpi
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ./dqmc /tmp/afqmc_serial_nrep1.in > /tmp/afqmc_serial_nrep1.out
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 mpirun -np 1 ./dqmc_mpi /tmp/afqmc_mpi_nrep1.in > /tmp/afqmc_mpi_nrep1.out
grep -v '^#' /tmp/afqmc_serial_nrep1.out > /tmp/afqmc_serial_nrep1.data
grep -v '^#' /tmp/afqmc_mpi_nrep1.out > /tmp/afqmc_mpi_nrep1.data
diff -u /tmp/afqmc_serial_nrep1.data /tmp/afqmc_mpi_nrep1.data
```

Expected: `diff` exits 0.

- [ ] **Step 4: Verify rank-count independence for `nrep=4`**

```bash
cat > /tmp/afqmc_mpi_nrep4.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=4
nmeas=8
nbin=2
parallel=mpi
nrep=4
seed=246813579
EOF

for np in 1 2 4; do
  VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
    mpirun -np "$np" ./dqmc_mpi /tmp/afqmc_mpi_nrep4.in \
    > "/tmp/afqmc_mpi_nrep4_np${np}.out"
  grep -v '^#' "/tmp/afqmc_mpi_nrep4_np${np}.out" \
    > "/tmp/afqmc_mpi_nrep4_np${np}.data"
done
diff -u /tmp/afqmc_mpi_nrep4_np1.data /tmp/afqmc_mpi_nrep4_np2.data
diff -u /tmp/afqmc_mpi_nrep4_np1.data /tmp/afqmc_mpi_nrep4_np4.data
```

Expected: both `diff` commands exit 0.

- [ ] **Step 5: Verify idle rank case**

```bash
cat > /tmp/afqmc_mpi_nrep2.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=2
nmeas=4
nbin=2
parallel=mpi
nrep=2
seed=246813579
EOF

VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
  mpirun -np 4 ./dqmc_mpi /tmp/afqmc_mpi_nrep2.in \
  > /tmp/afqmc_mpi_nrep2_np4.out
grep -v '^#' /tmp/afqmc_mpi_nrep2_np4.out
```

Expected: exactly one physical data row and no hang.

- [ ] **Step 6: Run tests**

```bash
make test
make test_mpi
```

Expected:

```text
ALL TESTS PASSED
ALL MPI TESTS PASSED
```

- [ ] **Step 7: Commit**

```bash
git add src/main.c
git commit -m "feat: gather MPI replica bins" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 8: Reduce MPI Profiler Stats

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Add a profiler reduce helper**

Add under the MPI helper functions:

```c
#ifdef AFQMC_USE_MPI
static int mpi_reduce_profiler_stats(const MpiEnv *env, const Profiler *local,
                                     Profiler *root)
{
    const int nstat = PROF_PHASE_COUNT * PROF_REGION_COUNT;
    unsigned long long *local_calls =
        calloc((size_t)nstat, sizeof(unsigned long long));
    unsigned long long *root_calls =
        mpi_is_root(env) ? calloc((size_t)nstat, sizeof(unsigned long long))
                         : NULL;
    double *local_sec = calloc((size_t)nstat, sizeof(double));
    double *root_sec =
        mpi_is_root(env) ? calloc((size_t)nstat, sizeof(double)) : NULL;
    int failed = local_calls == NULL || local_sec == NULL ||
                 (mpi_is_root(env) && (root_calls == NULL || root_sec == NULL));
    if (mpi_any_failed(env, failed)) {
        free(local_calls);
        free(root_calls);
        free(local_sec);
        free(root_sec);
        return 1;
    }

    int idx = 0;
    for (int ph = 0; ph < PROF_PHASE_COUNT; ph++) {
        for (int rg = 0; rg < PROF_REGION_COUNT; rg++) {
            local_calls[idx] = local->stat[ph][rg].calls;
            local_sec[idx] = local->stat[ph][rg].total_sec;
            idx++;
        }
    }

    MPI_Reduce(local_calls, root_calls, nstat, MPI_UNSIGNED_LONG_LONG, MPI_SUM,
               0, MPI_COMM_WORLD);
    MPI_Reduce(local_sec, root_sec, nstat, MPI_DOUBLE, MPI_SUM, 0,
               MPI_COMM_WORLD);

    if (mpi_is_root(env) && root != NULL) {
        idx = 0;
        for (int ph = 0; ph < PROF_PHASE_COUNT; ph++) {
            for (int rg = 0; rg < PROF_REGION_COUNT; rg++) {
                root->stat[ph][rg].calls = root_calls[idx];
                root->stat[ph][rg].total_sec = root_sec[idx];
                idx++;
            }
        }
    }

    free(local_calls);
    free(root_calls);
    free(local_sec);
    free(root_sec);
    return 0;
}
#endif
```

- [ ] **Step 2: Keep rank-local profiler initialized before local replicas**

Task 5 already declares `Profiler rank_prof` and initializes it before local replicas:

```c
            Profiler rank_prof;
            profiler_init_memory(&rank_prof, p.profile);
            profiler_set_mpi_metadata(&rank_prof, p.nrep, "mpi",
                                      mpi_env.nranks);
            profiler_beta_begin(&rank_prof, beta, T, p.dtau, Ltr);
```

After each local replica completes, merge into `rank_prof`:

```c
            for (int local = 0; local < local_nrep; local++) {
                profiler_merge(&rank_prof, &local_replica_profs[local]);
            }
```

- [ ] **Step 3: Reduce only after successful status and bin gather**

After successful bin conversion and before `profiler_beta_end(&prof)` on rank 0:

```c
            if (p.profile) {
                if (mpi_reduce_profiler_stats(&mpi_env, &rank_prof, &prof) != 0) {
                    mpi_beta_failed = 1;
                    goto mpi_beta_cleanup;
                }
                if (mpi_env.rank == 0) {
                    profiler_set_mpi_metadata(&prof, p.nrep, "mpi",
                                              mpi_env.nranks);
                    profiler_beta_end(&prof);
                }
            }
```

Keep the design invariant: if status gather found any failed replica, skip bin gather and skip profiler reduce on every rank. `p.profile` is input-wide and identical on all ranks, so using it to skip the profiler collective is safe.

- [ ] **Step 4: Verify profiler CSV has `nranks` and aggregated regions**

```bash
cat > /tmp/afqmc_mpi_profile.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=4
nmeas=8
nbin=2
parallel=mpi
nrep=4
profile=1
profile_file=/tmp/afqmc_mpi_profile.csv
seed=246813579
EOF

make dqmc_mpi
VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
  mpirun -np 2 ./dqmc_mpi /tmp/afqmc_mpi_profile.in \
  > /tmp/afqmc_mpi_profile.out
head -1 /tmp/afqmc_mpi_profile.csv
grep ',dqmc_sweep,' /tmp/afqmc_mpi_profile.csv
grep ',green_from_scratch,' /tmp/afqmc_mpi_profile.csv
grep ',4,mpi,2' /tmp/afqmc_mpi_profile.csv
```

Expected:

- Header ends with `nrep,parallel,nranks`.
- At least one `dqmc_sweep` row exists.
- At least one `green_from_scratch` row exists.
- Rows contain `,4,mpi,2`.

- [ ] **Step 5: Run tests**

```bash
make test
make test_mpi
```

Expected:

```text
ALL TESTS PASSED
ALL MPI TESTS PASSED
```

- [ ] **Step 6: Commit**

```bash
git add src/main.c
git commit -m "feat: reduce MPI profiler stats" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 9: Audit Collective Failure Paths

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Audit the single cleanup label inside the MPI beta branch**

Task 5 introduced one local cleanup path for MPI beta resources. Confirm the branch still uses this shape after Tasks 6-8 have filled in the status/bin/profiler work:

```c
            int mpi_beta_failed = 0;
            /* all resources initialized to NULL at branch top */

            /* work */

        mpi_beta_cleanup:
            free(local_failed);
            free(all_failed);
            free(status_counts);
            free(status_displs);
            free(local_values);
            free(local_counts);
            free(all_values);
            free(all_counts);
            free(value_counts);
            free(value_displs);
            free(count_counts);
            free(count_displs);
            free(global_bins);
            free(Ehub);
            free(Egc);
            free(Eph);
            free(Nbin);
            free(Dbin);
            free(Sbin);
            free_replica_arrays(NULL, local_results, local_replica_profs,
                                local_replica_failed, local_nrep);
            if (mpi_any_failed(&mpi_env, mpi_beta_failed)) {
                /* close root files, free lattice, finalize MPI, return 1 */
            }
```

This pattern ensures every rank reaches the same number of collectives after:

- local allocation failure,
- status gather allocation failure,
- replica failure,
- `replica_log` write failure,
- bin gather allocation failure,
- rank0 post-gather measurement-bin allocation failure,
- zero-sign bin failure,
- profiler reduce allocation failure.

- [ ] **Step 2: Audit with text search**

Run:

```bash
rg -n "return 1|return 0|MPI_Gatherv|MPI_Reduce|MPI_Allreduce|ferror|calloc|fclose" src/main.c
```

Check these invariants:

- No `return` appears inside the MPI beta branch before local cleanup.
- Every rank0-only `calloc`/`ferror`/`fclose` failure becomes a local flag followed by `mpi_any_failed()`.
- Failed replica status is shared before bin gather and profiler reduce.
- Failed beta writes `replica_log` status rows but does not print a physical data row.
- Failed beta does not call `profiler_beta_end(&prof)`.

- [ ] **Step 3: Verify startup open failure does not hang**

```bash
cat > /tmp/afqmc_mpi_bad_log.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=2
nmeas=4
nbin=2
parallel=mpi
nrep=4
replica_log=/no_such_dir/afqmc_replicas.csv
seed=246813579
EOF

mpirun -np 2 ./dqmc_mpi /tmp/afqmc_mpi_bad_log.in \
  > /tmp/afqmc_mpi_bad_log.out 2> /tmp/afqmc_mpi_bad_log.err; test $? -ne 0
grep 'ERROR: failed to open replica_log' /tmp/afqmc_mpi_bad_log.err
```

Expected: command exits nonzero quickly, and `grep` prints one matching line.

- [ ] **Step 4: Verify `parallel=serial` under `np=2` does not hang**

```bash
mpirun -np 2 ./dqmc_mpi /tmp/afqmc_parallel_serial.in \
  > /tmp/afqmc_np2_serial.out 2> /tmp/afqmc_np2_serial.err; test $? -ne 0
grep 'ERROR: parallel=serial under MPI requires mpirun -np 1' /tmp/afqmc_np2_serial.err
```

Expected: command exits nonzero quickly, and `grep` prints one matching line.

- [ ] **Step 5: Verify failed output close handling is not ignored**

Run:

```bash
rg -n "profiler_close|fclose\\(replica_fp\\)" src/main.c
```

Expected: close failures in MPI mode are converted to a root flag and shared with `mpi_any_failed()` before `MPI_Finalize()`.

- [ ] **Step 6: Run full test sets**

```bash
make test
make test_mpi
```

Expected:

```text
ALL TESTS PASSED
ALL MPI TESTS PASSED
```

- [ ] **Step 7: Commit**

```bash
git add src/main.c
git commit -m "fix: harden MPI collective failure paths" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

## Task 10: End-to-End MPI Reproducibility Checks

**Files:**
- No source changes expected.
- Temporary files under `/tmp`.

- [ ] **Step 1: Build all variants**

```bash
make clean
make dqmc
make dqmc_mpi
make test
make test_mpi
```

Expected:

```text
ALL TESTS PASSED
ALL MPI TESTS PASSED
```

- [ ] **Step 2: Verify `nrep=5` rank-count independence**

```bash
cat > /tmp/afqmc_mpi_nrep5_full.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1
nwarm=4
nmeas=10
nbin=5
parallel=mpi
nrep=5
replica_log=/tmp/afqmc_mpi_nrep5_full_replicas.csv
seed=246813579
EOF

for np in 1 2 3; do
  VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
    mpirun -np "$np" ./dqmc_mpi /tmp/afqmc_mpi_nrep5_full.in \
    > "/tmp/afqmc_mpi_nrep5_full_np${np}.out"
  grep -v '^#' "/tmp/afqmc_mpi_nrep5_full_np${np}.out" \
    > "/tmp/afqmc_mpi_nrep5_full_np${np}.data"
done
diff -u /tmp/afqmc_mpi_nrep5_full_np1.data /tmp/afqmc_mpi_nrep5_full_np2.data
diff -u /tmp/afqmc_mpi_nrep5_full_np1.data /tmp/afqmc_mpi_nrep5_full_np3.data
```

Expected: both `diff` commands exit 0.

- [ ] **Step 3: Verify multiple beta rows**

```bash
cat > /tmp/afqmc_mpi_multibeta.in <<'EOF'
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=1,2
nwarm=4
nmeas=10
nbin=5
parallel=mpi
nrep=3
replica_log=/tmp/afqmc_mpi_multibeta_replicas.csv
profile=1
profile_file=/tmp/afqmc_mpi_multibeta_profile.csv
seed=246813579
EOF

VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
  mpirun -np 2 ./dqmc_mpi /tmp/afqmc_mpi_multibeta.in \
  > /tmp/afqmc_mpi_multibeta.out
grep -v '^#' /tmp/afqmc_mpi_multibeta.out | wc -l
tail -n +2 /tmp/afqmc_mpi_multibeta_replicas.csv | wc -l
grep ',3,mpi,2' /tmp/afqmc_mpi_multibeta_profile.csv | head
```

Expected:

- Physical data row count is `2`.
- Replica log data row count is `6`.
- Profile rows contain `,3,mpi,2`.

- [ ] **Step 4: Verify MPI stdout header carries `nranks`**

```bash
head -1 /tmp/afqmc_mpi_multibeta.out
```

Expected:

```text
# lattice=chain n=4 U=4 mu=2 dtau=0.1 bipartite=1 parallel=mpi nrep=3 bins=15 nranks=2
```

- [ ] **Step 5: Confirm no tracked files changed**

Task 10 should only produce `/tmp` files.

Run:

```bash
git status --short
```

Expected: no output. If tracked files changed, inspect the diff and create a separate fix task before continuing.

## Task 11: L6 U4 MPI Scaling Smoke

**Files:**
- Create: `data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626/README.md`
- Create: `data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626/input.in`
- Create: `data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626/summary.tsv`
- Create: `data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626/*.out`
- Create: `data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626/*.csv`
- Modify: `LOG.md`

- [ ] **Step 1: Create the input**

```bash
mkdir -p data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626
cat > data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626/input.in <<'EOF'
lattice=chain
Lx=6
U=4
dtau=0.1
beta_list=4
nwarm=500
nmeas=5000
nbin=50
parallel=mpi
nrep=4
profile=1
profile_file=profile.csv
replica_log=replicas.csv
seed=246813579
EOF
```

- [ ] **Step 2: Run `np=1/2/4` with BLAS threads fixed**

Use per-run copied inputs so each profile/log path is unique:

```bash
run_dir=data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626
for np in 1 2 4; do
  sed -e "s|profile_file=profile.csv|profile_file=${run_dir}/profile_np${np}.csv|" \
      -e "s|replica_log=replicas.csv|replica_log=${run_dir}/replicas_np${np}.csv|" \
    "$run_dir/input.in" > "$run_dir/input_np${np}.in"
  /usr/bin/time -p env VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
    mpirun -np "$np" ./dqmc_mpi "$run_dir/input_np${np}.in" \
    > "$run_dir/out_np${np}.dat" 2> "$run_dir/time_np${np}.txt"
done
```

- [ ] **Step 3: Create `summary.tsv`**

Generate the summary from `/usr/bin/time -p` output:

```bash
run_dir=data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626
real1=$(awk '/^real /{print $2}' "$run_dir/time_np1.txt")
real2=$(awk '/^real /{print $2}' "$run_dir/time_np2.txt")
real4=$(awk '/^real /{print $2}' "$run_dir/time_np4.txt")
awk -v r1="$real1" -v r2="$real2" -v r4="$real4" 'BEGIN {
  print "np\tnrep\treal_sec\tthroughput_speedup\tnote";
  printf "1\t4\t%.6g\t%.3f\tbaseline\n", r1, 1.0;
  printf "2\t4\t%.6g\t%.3f\treplica MPI\n", r2, r1 / r2;
  printf "4\t4\t%.6g\t%.3f\treplica MPI\n", r4, r1 / r4;
}' > "$run_dir/summary.tsv"
cat "$run_dir/summary.tsv"
```

- [ ] **Step 4: Create a README for the run**

Create `data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626/README.md`:

```markdown
---
date: 2026-06-26
datetime: 2026-06-26 08:21 JST
model: Codex (GPT-5)
summary: |
  DQMC MPI replica scaling smoke for L=6, U=4, beta=4, dtau=0.1.
  nrep=4, nwarm=500, nmeas=5000, nbin=50, BLAS threads fixed to 1.
---

# DQMC MPI Replica Scaling Smoke

Condition:

- lattice: 1D chain, L=6
- U: 4
- beta: 4
- dtau: 0.1
- nrep: 4
- nwarm: 500
- nmeas: 5000
- nbin: 50
- BLAS threads: `VECLIB_MAXIMUM_THREADS=1`, `OPENBLAS_NUM_THREADS=1`

Files:

- `input.in`: template input.
- `input_np*.in`: per-rank inputs with unique profile and replica log paths.
- `out_np*.dat`: stdout data.
- `time_np*.txt`: `/usr/bin/time -p` output.
- `profile_np*.csv`: profiler CSV.
- `replicas_np*.csv`: replica assignment log.
- `summary.tsv`: wall time and throughput summary.
```

If this task is executed on a later date, run `date '+%Y-%m-%d %H:%M %Z'` before editing and use that exact value in the frontmatter.

- [ ] **Step 5: Update `LOG.md`**

Add a reverse-chronological entry that records:

- implementation completion commit hashes,
- `make test` and `make test_mpi` result,
- `np=1/2/4` smoke condition,
- `summary.tsv` path,
- any remaining limitation, especially that `parallel=hybrid` is still reserved.

- [ ] **Step 6: Commit the implementation log and scaling smoke if the user wants data committed**

```bash
git add LOG.md data/profiling_runs/mpi_replica_scaling_L6_U4_beta4_dtau0.1_20260626
git commit -m "data: record MPI replica scaling smoke" -m "Co-Authored-By: Codex (GPT-5) <noreply@openai.com>"
```

If the user has not asked to commit run data, commit only `LOG.md` or leave data uncommitted and report the path.

## Self-Review Result

Spec coverage:

- Optional MPI build target: Task 1.
- Contiguous rank assignment and idle ranks: Task 2, Task 7.
- Rank-count-independent seed: Task 5 and Task 10.
- Struct-padding-free bin gather: Task 2 and Task 7.
- `local_failed = replica_failed || result.status != 0`: Task 2 and Task 6.
- Rank0-only stdout, `replica_log`, `profile_file`, `hopping_used.txt`: Task 4, Task 6, Task 8.
- Existing serial/OpenMP CSV schema preserved: Task 3.
- MPI profiler `nranks` column: Task 3 and Task 8.
- Mode validation priority: Task 4.
- Collective failure handling: Task 4, Task 6, Task 7, Task 8, Task 9.
- Plan-review blockers: H1 is addressed by the Task 5 per-beta MPI block and `continue`; H2 by Task 4 setup/close `mpi_any_failed()` barriers; H3 by Task 4 non-root `profiler_init_memory()`.
- Plan-review cleanup issues: M1 is addressed by `alloc_replica_arrays(0, ...)` success; M2/M3 by branch-top NULL/0 declarations and scoped failure flags; L3 by run-directory profile/log paths.
- Reproducibility and scaling checks: Task 10 and Task 11.

Placeholder scan:

- Runtime data files are generated by commands that write concrete numeric values.
- Failure-handling steps name the exact failure flags, collectives, and verification commands.
- Commit commands name concrete tracked files for implementation tasks.

Type consistency:

- Helper names use `replica_mpi_*` consistently.
- Bin pack width uses `REPLICA_MPI_BIN_DOUBLES`.
- Profiler MPI metadata uses `profiler_set_mpi_metadata(Profiler *, int, const char *, int)`.
- MPI helper code uses `MpiEnv` consistently.
