---
date: 2026-08-21
datetime: 2026-08-21 22:10 JST
model: Codex (GPT-5)
status: implemented
topic: equal-time longitudinal spin structure factor Szz(q)
summary: |
  S^{zz}(q) を TDD で追加する実装計画。物理 estimator と momentum plan を
  unit test で固定してから、replica bin、serial/OpenMP、MPI/hybrid、TSV 出力へ
  段階統合する。既定無効時の stdout・乱数列・scalar observables の後方互換を
  各段階の gate とする。設計レビューの High 5 件を入力、call site、MPI cleanup、
  負変位、Gatherv integrity の実装・テスト項目として反映した。
---

# Szz Structure Factor Implementation Plan

## Implementation Result (2026-08-21 22:10 JST)

Milestone 1（serial/OpenMP）と Milestone 2（MPI/hybrid）を完了した。既定無効時の
stdout 後方互換、物理 oracle、sum rule、PH mapping、MPI packing/Gatherv integrity、
4 build variant の回帰を確認した。実行結果と残る production-scale 検証は
`docs/2026-08-21-szz-structure-factor-validation.md` に記録した。

## Revision Note (2026-08-21 21:31 JST)

`docs/reviews/2026-08-21-szz-structure-factor-design-plan-review.md` の High 5 件を
着手前条件として反映した。

- H-1: `%255s` を使わず、manual parser で空値、255 文字超、内部空白、trailing
  junk、512-byte buffer を超える物理行を fail-fast する。
- H-2: `dqmc_run_replica()` の main 3 箇所と直接 test 2 箇所を同じ task で更新する。
- H-3: MPI beta block の新規 pointer は block 冒頭で `NULL` 初期化し、先行
  `goto mpi_beta_cleanup` から安全に cleanup できる形を gate にする。
- H-4: 負の `%` を使わず、条件加算で変位を canonical 化する test と実装を追加する。
- H-5: root receive buffer の `NAN` sentinel、全要素 finite 検査、all-q の bin-wise
  sum rule、serial/MPI fixed-seed 比較で Gatherv 欠落を検出する。全ゼロ値そのものは
  reject しない。
- serial/OpenMP を Milestone 1、MPI/hybrid を Milestone 2 とし、前者を完了させて
  から通信層へ進む。

## 1. Reference Design

設計の正本:

- `docs/superpowers/specs/2026-08-21-szz-structure-factor-design.md`

物理式のローカル参照:

- `references/格子上の電子系における乱れ及び相互作用の効果【大塚雄一】[博士論文_200203].md`
  の式 (2.36)、(A.53)、(A.55)、(A.56)。
- `docs/2026-07-02-ph-symmetry-spin-correlations-note.md`。

この計画では実装順と gate を固定する。物理 normalization、input/output schema、
parallel scope を変更する場合は、先に設計書を更新してから実装へ進む。

## 2. Acceptance Boundary

最終完了条件:

- `chain`/`square` で `szz_q=af|all|mx:my,...` が動作する。
- q ごとの `Szz`, `dSzz` が long-form TSV に出る。
- full $S^{zz}$、$1/N$ normalization、factor 3 なし。
- PH/two-spin、serial/OpenMP/MPI/hybrid が同じ estimator と bin 規約を使う。
- default `szz_q=none` の既存 stdout が baseline と一致する。
- physics oracle、sum rule、U=0、PH mapping、MPI packing の tests が通る。
- four build variants の default tests が通る。

段階的な受入境界:

- Milestone 1: serial/OpenMP の estimator、sign/bin、TSV、物理 oracle、後方互換が
  完了し、MPI 以外の focused/default tests が通る。
- Milestone 2: MPI/hybrid の dynamic-width gather、collective-safe error path、
  Gatherv integrity、全 build variant の回帰が完了する。

対象外は設計書 §3.2 に従う。途中で arbitrary q、file-lattice coordinates、動的相関へ
scope を広げない。

## 3. Pre-Implementation Baseline

### Step 0.1: Preserve the Dirty Worktree

作業開始時に次を確認する。

```sh
git status --short
git diff -- LOG.md
```

現時点で `LOG.md` と DQMC 基礎ノートに user-owned 未コミット変更がある。
これらを上書き、reset、cleanup しない。Szz 実装は該当箇所への最小 patch とする。

### Step 0.2: Capture Disabled Baseline

代表 input で pre-change output を保存する。長期成果物ではなく同一実装作業中の
diff oracle なので temporary directory を使ってよい。

```sh
baseline_dir=$(mktemp -d)
make dqmc
./dqmc input/1d_L4_U0.txt > "$baseline_dir/1d_L4_U0.out"
./dqmc input/1d_L4_U4.txt > "$baseline_dir/1d_L4_U4.out"
```

記録するもの:

- stdout。
- exit status。
- `hopping_used.txt` は比較後に user data と混同しないよう対象を確認する。
- `git diff --stat`。

### Step 0.3: Run Current Tests

```sh
make test
```

MPI/OpenMP toolchain が利用可能なら実装前にも各 variant を実行する。既存 failure が
あれば Szz の regression と混同せず記録する。

## 4. Task 1: Lattice Metadata and Input Contract

Files:

- Modify: `src/lattice.h`, `src/lattice.c`
- Modify: `src/io.h`, `src/io.c`
- Modify: `tests/test_lattice.c`, `tests/test_lattice_file.c`
- Modify: `tests/test_io.c`

### Step 1.1: Write Failing Lattice Tests

Tests first:

- chain: `type=LAT_CHAIN`, `Lx=<input>`, `Ly=1`, coordinates available。
- square: `type=LAT_SQUARE`, dimensions preserved。
- file: `type=LAT_FILE`, coordinates unavailable。
- degenerate square `1xL` / `Lx1` metadata remains exact。
- `lattice_free()` resets the new fields。

### Step 1.2: Add Lattice Metadata

- Add `LAT_FILE` to `LatType`。
- Add `type`, `Lx`, `Ly`, `has_coordinates` to `Lattice`。
- Initialize every constructor explicitly; do not depend on zeroed stack memory。
- Preserve hopping/bipartite construction and its arithmetic path。

Gate:

```sh
make tests/test_lattice tests/test_lattice_file
./tests/test_lattice
./tests/test_lattice_file
```

### Step 1.3: Write Failing Parser Tests

Cases:

- default `szz_q=none`, default `szz_file=szz.tsv`。
- valid `af`, `all`, and raw list strings。
- explicit `szz_file`。
- a selector of exactly 255 bytes is accepted and remains NUL terminated。
- selector/file values longer than 255 bytes reject instead of truncating。
- empty `szz_q=` or `szz_file=`, internal whitespace, trailing junk, and a physical input line
  that does not fit the 512-byte parser buffer reject。A v1 `szz_file` path therefore cannot
  contain whitespace。
- blank/comment-only lines remain ignored; `szz_q=` does not silently become `none`。
- `szz_file=none` is parsed literally here and rejected by enabled-plan setup; disabling is
  expressed only by `szz_q=none`。
- unknown key behavior stays fail-fast。

Lattice-dimension-dependent validation is not placed in `params_read()`; it belongs to
`szz_plan_init()` in Task 2。

### Step 1.4: Add Params Fields

- Add `char szz_q[256]`, `char szz_file[256]`。
- Parse only `szz_q` and `szz_file` keys。For these string keys, read the complete value after
  `=`, trim only outer whitespace, validate its length before copying, and do not use `%255s`。
- Detect an input line whose terminating newline is missing because the 512-byte input buffer
  filled; consume nothing silently and report a parse error。
- Set defaults without changing existing defaults。

No Makefile change is planned: the current wildcard source/test discovery already covers the
new files。The review's existing `lattice_alloc()` partial-allocation behavior is not broadened into
this feature; Szz-owned allocations still receive checked, idempotent cleanup。

Gate:

```sh
make tests/test_io
./tests/test_io
```

Suggested commit boundary:

```text
feat(lattice): expose regular-grid metadata for momentum observables
```

## 5. Task 2: Momentum Plan

Files:

- Create: `src/structure_factor.h`, `src/structure_factor.c`
- Create: `tests/test_structure_factor.c`

### Step 2.1: Write Failing Selector Tests

Cover:

- `none` creates a disabled zero-allocation plan。
- chain/square `af` indices and q/pi values。
- `all` ordering: `my` outer, `mx` inner。
- explicit list preserves input order。
- malformed token, missing colon, non-integer, negative, out-of-range, duplicate。
- chain `my!=0` reject。
- odd active extent with `af` reject。
- `lattice=file` reject only when enabled。
- `Lx=1`/`Ly=1` is inactive and uses momentum index 0, including `af`。
- enabled plan with `szz_file=none` rejects at setup integration; empty `szz_file` already fails
  parsing in Task 1。
- allocation-size overflow checks。

### Step 2.2: Implement `SzzPlan`

API:

```c
int szz_plan_init(SzzPlan *plan, const Lattice *L, const char *selector);
void szz_plan_free(SzzPlan *plan);
```

Implementation rules:

- Start from a fully zeroed/disabled plan so cleanup is always safe。
- Parse with `strtol` and full end-pointer/range checks; do not use `atoi`。
- Compute `phase_cos[disp + q*n]` once with `acos(-1.0)` for pi。
- Check all generated phases are finite。
- Do not store mutable scratch in the plan。

### Step 2.3: Test Phase Table

- q=0 phases all 1。
- q and negative-q indices have equal cosine table。
- AF phases equal `(-1)^(dx+dy)` on even square lattices。
- selected entries agree with direct `cos()` evaluation。

Gate:

```sh
make tests/test_structure_factor
./tests/test_structure_factor
```

Suggested commit boundary:

```text
feat(measure): add finite-lattice momentum plan for Szz
```

## 6. Task 3: Physics Estimator and Optimized Measurement

Files:

- Modify: `src/structure_factor.h`, `src/structure_factor.c`
- Modify: `tests/test_structure_factor.c`
- Modify: `tests/test_integration.c`

### Step 3.1: Add a Test-Local Direct Oracle

The oracle implements the boxed estimator and literal `q,i,j` sum independently of the
production displacement algorithm. Keep it in the test file so production and oracle do not
share the indexing code that is under test。

### Step 3.2: Write Failing Physics Tests

- diagonal `G_up=G_down=0.5 I`: all q exactly 1/8。
- fully polarized diagonal Green: q=0 N/4 and other q zero。
- Néel product Green: AF N/4 and q=0 zero。
- deterministic dense `G_up/G_down`: optimized result matches direct oracle。
- chain, square, rectangular square, OBC metadata cases。
- an explicit pair with `(xi-xj)=-1` reaches displacement `Lx-1` without a negative index。
- deterministic non-symmetric dense Green: directly verify $C^{zz}_{ij}=C^{zz}_{ji}$ and
  verify the sine-weighted imaginary Fourier sum cancels to machine precision。
- `Szz(q)=Szz(-q)` as a secondary consequence test, not the sole justification for dropping
  the imaginary part。
- all-q sum equals both direct onsite $\sum_i C^{zz}_{ii}$ and the scalar
  $\tfrac14(N_e-2ND)$ expression at sample/bin level。
- all output finite; null/size mismatch inputs fail。

### Step 3.3: Implement Workspace and Estimator

API sketch:

```c
int szz_workspace_init(SzzWorkspace *work, const SzzPlan *plan);
void szz_workspace_free(SzzWorkspace *work);
int measure_szz_sample(const SzzPlan *plan, SzzWorkspace *work,
                       const double *g_up, const double *g_dn,
                       double *out);
```

Implementation sequence:

1. zero `corr_disp`。
2. loop over ordered `(i,j)` pairs。
3. use column-major `g[i+j*n]`/`g[j+i*n]` exactly as specified。
4. canonicalize each difference with `dx=xi-xj; if (dx<0) dx+=Lx` and likewise for y;
   do not index with C's negative remainder operator。
5. add `Czz_ij` to the canonical displacement bin。
6. dot each q phase with `corr_disp` and divide once by `N`。
7. reject non-finite intermediate/final values。

No allocation and no `cos()` call in the per-sample hot path。
For OBC, `corr_disp` is only a Fourier-sum intermediate; do not expose or document it as a
translation-invariant real-space $C(r)$。

### Step 3.4: Add U=0 Integration Check

Extend `tests/test_integration.c` or add a focused integration test:

- construct the U=0 DQMC Green as current test does。
- build at least q=0, AF, all-q plan。
- compare DQMC measurement with both the direct Green oracle and the independently evaluated
  momentum-space formula
  $S^{zz}(q)=(2N)^{-1}\sum_k f_k(1-f_{k+q})$。
- exercise at least one periodic chain and one periodic square lattice, including
  momentum-index wrapping in $k+q$ and the production hopping-sign convention。
- repeat after sweeps and confirm seed/field independence at U=0。

Gate:

```sh
make tests/test_structure_factor tests/test_integration
./tests/test_structure_factor
./tests/test_integration
```

Suggested commit boundary:

```text
feat(measure): compute equal-time longitudinal spin structure factors
```

## 7. Task 4: Replica Bin Storage and Sign Reweighting

Files:

- Modify: `src/replica.h`, `src/replica.c`
- Modify: `src/structure_factor.h`, `src/structure_factor.c`
- Modify: `tests/test_replica.c`
- Modify: `tests/test_structure_factor.c`

### Step 4.1: Write Failing Storage Tests

- disabled `ReplicaResult` has `nq=0`, null vector storage。
- enabling `nq>0` allocates `nbin*nq` zeroed doubles。
- repeated positive-sign samples sum in `[q+nq*bin]` order。
- negative-sign sample contributes with its sign。
- bin values divide by the scalar bin's `sum_sign` through the common `szz_bin_ratio()` helper。
- NaN sample and NaN sign are rejected before mutation。
- out-of-range bin and caller `nq != result->nq` shape mismatch reject for both add/read APIs。
- zero-sign bin fails。
- free resets pointer and dimensions; repeated cleanup is safe。

### Step 4.2: Implement Optional Vector Storage

- Keep `ReplicaBin` unchanged。
- Add `sum_sign_szz` and `nq` to `ReplicaResult`。
- Keep `replica_result_alloc(result, nbin)` signature。
- Add enable/add/bin-values helpers from the design; vector-taking APIs carry an explicit `nq`
  and reject mismatches before indexing。
- Add one `szz_bin_ratio(num, nq, sum_sign, out)` implementation shared by serial/OpenMP
  result finalization and MPI root raw-buffer finalization。
- Use checked multiplication before `calloc()`。

### Step 4.3: Enforce Transactional Runtime Use

Before calling scalar or vector add:

- validate `MeasSample`。
- validate every Szz q value。
- validate sign and transformed energies。

Only then mutate both accumulators. Add a helper if needed to make this ordering hard to misuse。

Gate:

```sh
make tests/test_replica
./tests/test_replica
```

Suggested commit boundary:

```text
feat(replica): accumulate sign-weighted Szz vectors per bin
```

## 8. Task 5: Replica Measurement and Profiling Integration

Files:

- Modify: `src/replica_run.h`, `src/replica_run.c`
- Modify: `src/profiler.h`, `src/profiler.c`
- Modify: `src/main.c`
- Modify: `tests/test_profiler.c`
- Modify: `tests/test_ph_symmetry.c`
- Modify: `tests/test_dqmc_alternating.c`
- Modify: `tests/test_sign_regression_slow.c`

### Step 5.1: Pass an Immutable Plan into Each Replica

Update signature:

```c
int dqmc_run_replica(..., const SzzPlan *szz_plan,
                     Profiler *prof, ReplicaResult *result);
```

All call sites pass the same rank-local immutable plan. A disabled plan is valid and avoids
nullable branching at every caller; inside the replica the enabled branch is explicit。

Update all five direct call sites in the same patch so no build variant retains the old ABI:

- `src/main.c`: MPI range helper, OpenMP loop, serial loop。
- `tests/test_dqmc_alternating.c` and `tests/test_sign_regression_slow.c`。

Pass the plan through `run_replica_range()` as well。Direct-call tests that do not exercise Szz
construct an explicitly zero-initialized disabled plan rather than passing `NULL`。

### Step 5.2: Allocate Replica-Local Workspace Once

- after `ReplicaResult` allocation, enable Szz storage if needed。
- allocate workspace before the measurement loop。
- free it on every success/error exit。Prefer a single cleanup path if this reduces missed frees,
  but do not rewrite unrelated DQMC setup logic。

### Step 5.3: Measure after Scalar Sample

Per measured sweep:

```text
dqmc_sweep
finite Green/sign check
measure_sample
if Szz enabled: measure_szz_sample
validate the complete sample
add scalar and Szz to the same bin
```

The Szz call must not mutate Green, field, RNG, or sign。

### Step 5.4: Add Profiler Region

- Add `PROF_MEASURE_SZZ` and name `measure_szz`。
- Update enum/name-array alignment tests。
- Timer exists only inside `if (plan->enabled)`。

### Step 5.5: PH Equality Test

Extend `tests/test_ph_symmetry.c`:

- compute direct `G_down` and PH-mapped `G_down` for the same field。
- evaluate selected/all Szz with both。
- compare every q for chain and square。

Gate:

```sh
make dqmc tests/test_profiler tests/test_ph_symmetry tests/test_integration \
  tests/test_dqmc_alternating tests/test_sign_regression_slow
./tests/test_profiler
./tests/test_ph_symmetry
./tests/test_integration
./tests/test_dqmc_alternating
```

The slow sign regression must compile at this gate; execute it in the slow-test matrix in Task 9。

Suggested commit boundary:

```text
feat(dqmc): measure Szz in replica runs
```

## 9. Task 6: Serial/OpenMP Finalization and TSV Output

This completes the Milestone 1 implementation。Before starting Task 7, execute the serial/OpenMP
parts of Task 8 and the usage/serial-regression parts of Task 9 as the Milestone 1 checkpoint。
MPI/hybrid-enabled Szz is accepted only after Task 7。

Files:

- Modify: `src/main.c`
- Create or Modify: focused output integration test/script under `tests/`
- Add: sample input under `input/` only after schema is stable

### Step 6.1: Initialize Plan before Replica Execution

- construct plan after lattice construction and validation。
- disabled plan is accepted for every current lattice type。
- enabled file lattice, malformed selector, and `szz_file=none` fail before replicas start;
  empty output paths already fail parsing。
- every MPI rank constructs the same rank-local plan; combine setup status with the existing
  `mpi_any_failed()` startup ordering before any rank enters replica work。
- root alone opens `szz_file` in `w` mode and sets `setup_failed` on failure; other MPI ranks do
  not open it and no rank takes an early return before the startup allreduce。
- extend `close_outputs()` to accept the Szz stream, null the pointer after close, and propagate
  root close failure through the collective-safe shutdown path。
- include plan/file cleanup in every startup and beta-loop error path。

Keep startup ordering explicit: lattice construction/validation → `szz_plan_init()` on every
rank → synchronized plan status → stdout metadata (root, now able to print `szz_nq`) → root
`szz_file` open → synchronized file-open status → replica execution。

### Step 6.2: Build q-major Jackknife Input

After replicas complete:

```text
for global replica id:
    for bin id:
        ratio[q] = sum_sign_szz / scalar sum_sign
        szz_bins[q*total_bins + flat_bin] = ratio[q]
```

Call the shared `szz_bin_ratio()` helper for both local `ReplicaResult` and, later, MPI raw
numerators。Validate all values before calling `jackknife()` per q。For `szz_q=all`, also validate
the bin-wise local-moment sum rule before final statistics。

### Step 6.3: Write the Output Contract

- comments and exact TSV header from design §6, including lattice/model metadata,
  `szz_q`, seed, parallel mode, `nrep`, bin count, and beta count。
- beta rows in requested q order。
- write distinct `beta_requested=p.beta_list[b]`, effective `beta=Ltr*dtau`, and
  `T=1/beta`; never derive T from the unrounded requested beta。
- include zero-origin `q_index`, raw `qx_over_pi/qy_over_pi`, and folded
  `qx_folded_over_pi/qy_folded_over_pi` in $(-\pi,\pi]$; length-1 directions are zero。
- print every double column with `%.17g` and indices as decimal integers。
- `fflush()` after each beta so a completed beta is durable。
- propagate open/write/flush/close failures to nonzero process exit。
- standard scalar data columns remain unchanged。

### Step 6.4: Output Tests

Run a tiny `nwarm/nmeas/nbin` case and assert:

- disabled: no `szz.tsv` created, stdout matches baseline。
- enabled: comments/header and exact column count。
- `af`: one row per beta。
- `all`: N rows per beta and deterministic order。
- every data field parses and is finite; a momentum above $\pi$ has the expected negative
  folded coordinate and all double text round-trips at `%.17g` precision。
- requested beta deliberately requiring integer-slice rounding records both requested and
  effective beta, with `T*beta` equal to one within roundoff。
- existing scalar stdout for the same seed is unchanged。
- invalid selector/file lattice/empty or `none` output path fails before data rows。

Gate:

```sh
make dqmc dqmc_omp
make test
make test_omp
```

Suggested commit boundary:

```text
feat(output): write jackknifed Szz momentum data
```

## 10. Task 7: MPI and Hybrid Vector Gather

This is Milestone 2。Do not declare MPI/hybrid support complete until every integrity and
collective-safety gate below passes。

Files:

- Modify: `src/replica_mpi.h`, `src/replica_mpi.c`
- Modify: `tests/test_mpi_replica.c`
- Modify: `src/main.c`

### Step 7.1: Write Failing Pack Tests

- two replicas, multiple bins, multiple q round-trip。
- q-fastest ordering `[replica][bin][q]`。
- `nq=0` disabled path。
- zero local replicas。
- `size_t` products that cannot fit MPI `int` counts/displacements reject before MPI calls。
- root buffers prefilled with `NAN` fail validation if any expected slot remains untouched。
- physically valid all-zero Szz values pass once every slot was overwritten。
- scalar `REPLICA_MPI_BIN_DOUBLES` remains 8 and existing pack tests unchanged。

### Step 7.2: Implement Separate Szz Pack

- add `replica_mpi_pack_szz()` only; root may use gathered raw buffer directly。
- reuse `replica_mpi_gatherv_layout(nrep, nbin, nranks, nq, ...)`。
- compute products and displacements in `size_t`; reject multiplication overflow and any value
  exceeding `INT_MAX` before conversion to MPI `int`。
- do not cast Szz dimensions through floating point。

### Step 7.3: Integrate MPI Gatherv

- allocate local buffer only when `local_bins*nq>0`。
- root allocates `total_bins*nq` and fills every element with `NAN` before the collective。
- gather scalar bins first, Szz numerators second。
- preserve collective call order on every rank, including no-replica ranks。
- after `MPI_Gatherv`, root verifies every expected element is finite before building ratios;
  a remaining sentinel is a hard error。
- use scalar `sum_sign` for q ratios through the common `szz_bin_ratio()` helper。
- for `szz_q=all`, verify every gathered bin against
  $\sum_q S^{zz}_b(q)=\tfrac14(N_{e,b}-2ND_b)$ with
  `abs(diff) <= 1e-12 + 1e-10*max(1,abs(rhs))`。
- for selected q, use the sentinel check plus fixed-seed serial/MPI equality; do not invent a
  partial-q sum rule。
- do not reject an all-zero Szz vector, and do not rely on a second Gatherv checksum as the sole
  integrity check。
- root alone performs jackknife and writes TSV。
- any rank allocation/finalization failure is combined with existing allreduce failure path。

At the beginning of the existing MPI beta block, declare every new Szz pointer alongside the
current cleanup-owned declarations and initialize it to `NULL`。No declaration with an initializer
may be bypassed by an earlier `goto mpi_beta_cleanup`。The common cleanup label frees all of them
unconditionally, and rank-local errors set a flag and join the established collective sequence
instead of returning early。

### Step 7.4: Cross-Mode Reproducibility

With single-threaded BLAS:

```sh
make dqmc dqmc_omp dqmc_mpi dqmc_hybrid
# prepare equivalent serial/omp/mpi/hybrid inputs with the same base seed/nrep
./dqmc serial.in
./dqmc_omp omp.in
mpirun -np 2 ./dqmc_mpi mpi.in
mpirun -np 2 ./dqmc_hybrid hybrid.in
```

Compare:

- scalar stdout data。
- Szz row ordering, means, and errors。
- replica log global ids/seeds。
- no deadlock when `nranks>nrep`。
- `szz_q=all` bin-wise sum rule after gather and selected-q receive completeness。
- root `szz_file` open/flush/close failure propagates to all ranks without a hang (use a local
  deterministic failing path test where supported)。

Gate:

```sh
make test_mpi
make test_hybrid
```

Kugui/remote validation is not part of local implementation and requires the separate explicit
remote/job approval defined in `AGENTS.md`。

Suggested commit boundary:

```text
feat(mpi): gather dynamic Szz bin vectors
```

## 11. Task 8: Physics Validation Ladder

Run Steps 8.1--8.5 first against serial/OpenMP immediately after Task 6 and record the Milestone 1
checkpoint。After Task 7, append MPI/hybrid receive-integrity and cross-mode evidence; do not
repeat expensive physics work unless the communication integration changes values。

Files:

- Add or Modify: `tests/test_structure_factor.c`
- Add optional slow validation under `tests/test_*_slow.c`
- Create: `docs/<date>-szz-structure-factor-validation.md`

### Step 8.1: Exact Algebraic Tests

Required in default suite:

- product-state normalizations。
- direct-oracle equivalence。
- direct $C^{zz}_{ij}=C^{zz}_{ji}$ and sine-imaginary cancellation, with q inversion as a
  secondary check。
- negative displacement canonicalization。
- all-q local-moment sum rule at sample and bin level on a default-suite DQMC run。
- U=0 deterministic DQMC tests for chain and square against the independent occupation formula
  $S^{zz}(q)=(2N)^{-1}\sum_k f_k(1-f_{k+q})$。
- PH direct-vs-mapped estimator equality。

### Step 8.2: Small Interacting Reference

Use an independently constructed finite-temperature exact diagonalization reference for a small
chain and square cluster。Record:

- lattice/BC, U, beta, dtau, nrep, nmeas, nbin, seeds。
- exact definition and normalization。
- DQMC Szz and jackknife error。
- Trotter comparison at two or more dtau values if the difference is not negligible。

Do not make a noisy Monte Carlo comparison a tight default unit test。It may be a slow test or a
documented validation run with a stated statistical criterion, for example agreement within the
combined statistical error plus observed Trotter drift。

### Step 8.3: Sum-Rule Validation on Real Runs

For `szz_q=all`, compare per beta:

$$
\sum_q S^{zz}(q)
\quad\text{and}\quad
\frac14(N_\mathrm{tot}-2ND).
$$

Because both are measured from the same configurations, also inspect the bin-wise difference;
do not compare only independently rounded final means。Use the same absolute/relative tolerance as
the MPI integrity check so serial and MPI acceptance cannot drift apart。

### Step 8.4: Bin-Width Stability

For a representative interacting AF case, recombine adjacent raw bins to at least two larger bin
widths and record `dSzz(Q_AF)`。The error estimate must reach a statistical plateau within its own
noise before the validation report recommends production settings。If it does not, lengthen
`nmeas` or document that the run is under-binned; do not silently treat the smallest-bin error as
final。

### Step 8.5: Performance Measurement

Profile representative local cases:

```text
square L=8:  szz_q=none, af, all
square L=16: szz_q=none, af, all
```

Record `dqmc_sweep`, `measure_sample`, `measure_szz`, beta wall time, and memory estimate。
Confirm absence of per-sample allocation and behavior compatible with $O(N^2+NN_q)$。
This is a measurement/reporting item, not a numerical acceptance threshold for landing the
feature。

No HPC submission is authorized by this plan。

Suggested commit boundary:

```text
test(physics): validate Szz normalization and finite-size sum rules
```

## 12. Task 9: Documentation and Final Regression

Files:

- Modify: user-facing input documentation if present。
- Modify: `docs/2026-07-02-afqmc-code-reference.html` only if maintaining that
  historical snapshot is desired; otherwise create a current Markdown usage note。
- Modify: `LOG.md` according to project rules。

### Step 9.1: Add Usage Examples

Document:

- `szz_q=none|af|all|mx:my,...`。
- momentum-index to q conversion。
- explicit-list 255-byte limit, strict whitespace behavior, and recommendation to use `all` for
  many momenta。
- `szz_file=none` is invalid; disable only with `szz_q=none`。
- output schema, `beta_requested` vs effective `beta`, folded momentum columns, and `%.17g`
  precision。
- full $S^{zz}$ normalization and factor-3 convention。
- `Szz(AF)/N` post-processing for size scaling。
- `lattice=file` limitation。
- OBC Fourier-sample interpretation; internal `corr_disp` is not an output $C(r)$。

### Step 9.2: Full Local Matrix

```sh
make clean
make test
make test_omp
make test_mpi
make test_hybrid
make test_slow
```

`make clean` is allowed only after confirming it removes generated binaries/objects listed in the
Makefile and no user source/data。Do not remove unrelated untracked documents。

### Step 9.3: Backward-Compatibility Diff

Re-run the Step 0 inputs and diff against captured stdout。

Expected:

- disabled stdout diff empty。
- enabled scalar data rows identical to disabled rows。
- only enabled metadata may add `szz_file/szz_nq` comment fields。
- same replica seeds and statuses。

### Step 9.4: Validation Report and Log

Create a validation document with:

- code revision/worktree context。
- commands and toolchains。
- test matrix results。
- exact/analytic comparison tables。
- PH and parallel comparison。
- Gatherv sentinel and bin-wise sum-rule evidence。
- bin-width plateau and performance measurements。
- known limitations and next steps。

Prepend a new `LOG.md` entry without altering user-owned prior entries。

Suggested final commit boundary:

```text
docs: document Szz measurement and validation
```

## 13. Stop Conditions

Pause implementation and revise the design before proceeding if any of the following occurs:

- the estimator fails the direct Wick oracle or local-moment sum rule。
- an overlong/empty Szz value or overlong physical input line can be silently truncated or
  mistaken for `none`。
- PH-mapped and directly computed down Green give different Szz beyond Green reconstruction
  tolerance。
- Szz measurement changes scalar observables or RNG trajectories when enabled。
- disabled output differs from baseline。
- MPI ordering differs from global replica-id/bin-id/q-id order。
- any MPI receive slot retains its `NAN` sentinel, any count/displacement exceeds `INT_MAX`, or
  the gathered all-q bin sum rule exceeds
  `1e-12 + 1e-10*max(1,abs(rhs))`。
- a new MPI cleanup-owned pointer cannot be proven `NULL`-initialized before every possible
  `goto mpi_beta_cleanup`。
- `all` implementation accidentally scales as $O(N^3)$ per sample。
- supporting a required lattice needs guessing site coordinates。

## 14. Recommended Execution Order

```text
Milestone 1 — serial/OpenMP
  1. lattice metadata + strict parser
  2. SzzPlan + selector tests
  3. direct oracle + optimized estimator
  4. replica sign-weighted vector storage + common ratio helper
  5. replica/PH/profiler integration + all five call sites
  6. serial/OpenMP finalization + precise TSV
  8a. serial/OpenMP exact, sum-rule, bin-width, and performance validation
  9a. usage docs + serial/OpenMP regression checkpoint

Milestone 2 — MPI/hybrid and final acceptance
  7. overflow-safe gather + cleanup invariants + receive integrity
  8b. append MPI/hybrid integrity and cross-mode evidence
  9b. finalize docs + full four-variant regression
```

Each step must leave its focused tests green before the next integration layer is changed。
