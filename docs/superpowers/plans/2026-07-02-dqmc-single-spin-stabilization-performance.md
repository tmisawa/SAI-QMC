---
date: 2026-07-02
datetime: 2026-07-02 22:05 JST
model: GPT-5 Codex
status: plan
topic: DQMC 高速化 — PH 後に残った片スピン安定化系の削減
summary: |
  UDV stack・delayed update・半充填 PH 対称性の導入後、16x16 beta=16 の
  kugui i2cpu serial sweep は 1.50 s/sweep まで短縮された。残った主戦場は
  片スピン側の安定化系で、測定 sweep の約 63% を
  green_stack_build + 左 udv_lmul + green_from_stack が占める。本計画は
  (1) stab 間隔再評価、(2) 交互スイープによる stack_build amortization、
  (3) udv_inv_one_plus 内部削減、(4) production rank/thread 配分スキャンを、
  TDD と実機検証の順で進める。交互スイープの carried stack 設計は
  独立レビューで有効性窓を確認済みだが、実装の off-by-one リスクが高いため、
  固定場での境界 stack 検証を先に行い、デフォルト挙動は変えない。
---

# DQMC Single-Spin Stabilization Performance Plan

> **For agentic workers:** 各 Task は `- [ ]` チェックボックスで追跡する。
> コード変更は小さく分け、各 Task 末尾で `make test` 系の確認と commit を行う。

## Goal

半充填・二部格子の PH 経路で、16x16, U=4, dtau=0.1, beta=16,
stab=4 の current baseline を **1.50 s/sweep から 1.0-1.2 s/sweep 程度**へ
短縮する。最終的な production 判断は kugui i2cpu の replicas/hour で行う。

この plan は「単一スピン化後に残った安定化系」を対象にする。UDV stack の
O(L) 化、delayed update、PH 対称性自体は実装済みであり、再実装しない。

## Current Baseline

基準データ:

- Commit: `211d4d6 feat: exploit PH symmetry at half filling`
- Validation: `data/profiling_runs/kugui_i2cpu_ph_symmetry_211d4d6_20260702/`
- Case: kugui i2cpu, serial, square 16x16, U=4, dtau=0.1, beta=16, stab=4,
  nmeas=2, nbin=2, nwarm=0

Measurement phase, per sweep:

| bucket | sec/sweep | fraction | note |
|---|---:|---:|---|
| `green_stack_build` | 0.280 | 18.6% | sweep 冒頭の右 stack 構築 |
| left `udv_lmul` | 0.179 | 11.9% | 更新済み block の左 UDV 延長 |
| `green_from_stack` | 0.486 | 32.4% | `udv_combine` + `udv_inv_one_plus` を含む |
| `green_wrap` | 0.261 | 17.4% | 160 wraps/sweep |
| `green_update` | 0.182 | 12.1% | delayed update + PH 後 |
| other | ~0.113 | 7.5% | measurement/profiler/loop overhead 等 |
| total `dqmc_sweep` | 1.501 | 100% |  |

したがって安定化系
`green_stack_build + left udv_lmul + green_from_stack` は約 0.945 s/sweep、
全体の約 63%。この内側を削る。

## Scope

この plan で扱うもの:

1. `stab=4 -> 8` などの安定化間隔再評価と validation ladder。
2. PH 経路向けの交互スイープと境界 stack 再利用。
3. `udv_inv_one_plus_work` の内部コスト削減。
4. kugui production 向け rank/thread 配分スキャン。

この plan で扱わないもの:

- PH 対称性そのものの再設計。
- non-PH/two-spin 経路の交互スイープ本格対応。初期実装は PH 経路限定にする。
- delayed update capacity tuning / flush layout。残り `green_update` は 12% なので
  並行の小改善扱い。
- 観測量追加、スピン相関実装、物理 model の変更。

## Design Overview

### 1. stab interval first

現在の stack 方式では、`stab` を大きくすると以下が減る:

- periodic/end rebuild 回数
- left `udv_lmul` 回数
- `green_from_stack` / `udv_combine` / `udv_inv_one_plus` 回数
- stack entry 数と `udv_rmul` 回数

一方で block 内の稠密積が長くなり、U=12 などでは安定性リスクがある。
L4-U12 で `stab=8` 汚染の前歴があるため、性能だけで採用しない。
まず `stab=2/4/8/16` の scan を実測し、U=4 16x16 production 候補と
U=12 低温 stress を分けて判定する。

### 2. alternating sweep with carried boundary stacks

現行は全 sweep が forward (`l=0..L-1`) で、毎回冒頭に右 stack
`R[j] = B(L, b[j])` を構築する。ここが 0.280 s/sweep。

交互スイープでは:

- forward sweep 中に、更新済み prefix
  `P[j] = B(b[j], 0)` を境界ごとに保存する。
- 次の backward sweep は、この prefix stack を sweep-start の未更新 lower part
  として再利用し、high slice から更新した suffix
  `R[j] = B(L, b[j])` をその場で伸ばす。
- backward sweep 中に suffix stack を保存する。
- 次の forward sweep は、その suffix stack を再利用する。

これにより、初回または invalidation 後を除いて `green_stack_build` を
steady-state sweep から消せる見込み。期待上限は current stab=4 で
`1.501 / (1.501 - 0.280) = 1.23x`。

独立レビュー
`docs/reviews/2026-07-02-single-spin-stabilization-plan-review.md`
では、この carried stack 設計は数理的に妥当と判定された。理由は、
forward sweep 中に保存した prefix は次の backward sweep がその境界より
下へ降りるまで変更されず、backward sweep 中に保存した suffix も次の
forward sweep がその境界へ到達するまで変更されないためである。
したがって Task 3 の固定場テストは、設計証明というより実装の境界・添字・
鮮度管理を検証するための必須テストである。

重要: これは MC 更新順を forward-only から交互に変えるため、同一 seed の
出力一致は要求しない。正しさは「各境界で現在場に対する G が
`green_from_scratch` と一致すること」と、物理量が統計誤差内で一致することで
検証する。

### 2b. carried stack memory policy

交互スイープでは prefix/suffix の 2 系統の boundary stack が必要になる。
PH 経路限定なので Gu のみを保持する。

- 16x16, L=160, stab=4: `2 x 41 x (2n^2+n) x 8B` ≈ 86 MB/replica。
  現行 PH の 1 stack ≈ 43 MB から倍増するが、pre-PH two-spin stack と同水準。
- 16x16, L=800, stab=4: `2 x 201 x (2n^2+n) x 8B` ≈ 420 MB/replica。
  120 rank/node では約 50 GB 級になる。i2cpu では収まる見込みだが、
  将来は stack 粒度を粗くして中間境界を再計算する fallback を用意する。

実装では **既存 `stack_u` を prefix/suffix の片方として再利用し、3 系統目を
増やさない**。3 系統にすると L=160 で約 129 MB/replica、L=800 で約
630 MB/replica になり、不要なメモリ膨張を招く。

### 3. `udv_inv_one_plus_work` internal reduction

current `udv_inv_one_plus_work` は `green_from_stack` の主要部で、
current beta=16 では 0.281 s/sweep 程度を占める。低リスク候補:

- `M` の inverse と logdet を同じ LU 分解から取得し、`la_logdet_work(M)` の
  追加 LU を消す。
- unit upper triangular の `T` に対して full `dgetrf+dgetri` ではなく
  triangular inverse (`dtrtri`) または専用 back substitution を使う。
- `la_inverse_work` / `la_logdet_work` を profiler 上で見えるようにし、
  実測で効いたものだけ採用する。

`det_sign` の fail-fast 不変条件は絶対に変えない。QR の Householder 情報から
`det(U)` を追跡する最適化は高リスクなので、低リスク候補の後に optional 扱い。

## File Map

Expected code touch points:

- `src/io.{h,c}`
  - `sweep_order=forward|alternating` を追加。default は `forward`。
- `src/dqmc.{h,c}`
  - `DqmcSweepMode`, `DqmcSweepDir` を追加。
  - `dqmc_init_mode(...)` を追加し、既存 `dqmc_init(...)` は forward wrapper として残す。
  - PH 経路限定で alternating sweep を実装。
  - carried prefix/suffix stack の valid flag を管理。
  - 既存 `stack_u` を carried prefix/suffix の一方として再利用し、PH 経路で
    boundary stack が 3 系統に増えないようにする。
- `src/green.{h,c}`
  - `green_wrap_backward` または `green_wrap_dir` を追加。
  - `green_build_Binv` helper で forward/backward wrap の重複を減らす。
  - prefix/suffix stack build/store/reconstruct helper を追加。
  - `green_from_boundary_factors` は既存 profiler region
    `PROF_GREEN_FROM_STACK` を使い、新 region は増やさない。
- `src/linalg.{h,c}`
  - `la_inverse_logdet_work`、`la_unit_upper_inverse_work` などを追加。
  - 必要なら profiler region 追加。
- `src/profiler.{h,c}`
  - `la_logdet` または `la_inverse_work` の内部計測を追加する場合のみ更新。
- `tests/`
  - `test_green_wrap.c`, `test_green_stack.c`, `test_dqmc_stack.c`,
    `test_ph_symmetry.c`, `test_io.c` を拡張。
- `docs/`, `data/profiling_runs/`
  - stab scan、alternating validation、rank/thread scan の結果を保存。

## Task 0: baseline lock and decomposition

- [x] `211d4d6` / current HEAD の PH 経路で、kugui i2cpu baseline を再確認する。
      少なくとも beta=16 serial と MPI np16 の `measurement` rows を抽出し、
      `green_stack_build + left udv_lmul + green_from_stack` の非重複分解を
      README または review note に固定する。
- [x] local で `make test`, `make test_slow`, `make test_omp`, `make test_mpi`,
      `make test_hybrid` が通ることを確認する。
- [x] `git diff --check`。
- [x] Commit: `docs: lock post-PH stabilization baseline`

Acceptance:

- 現 baseline の数値が `docs/reviews/2026-07-02-ph-symmetry-kugui-validation-review.md`
  と矛盾しない。
- 以後の speedup はこの baseline と比較する。

## Task 1: stab interval scan before behavior changes

- [x] Current production behavior のまま、`stab=2,4,8,16` を scan する PBS script を作る。
      Cases:
      - square 16x16, U=4, dtau=0.1, beta=4/8/16, PH enabled
      - 1D low-temp stress: L4/L8, U=12, dtau=0.1, beta=4 以上
- [x] 既存 `tests/test_green_stack_lowtemp_slow.c` を必要なら拡張し、
      `stab=8/16` の fixed-field `green_from_stack` vs `green_from_scratch`
      誤差を確認する。
- [x] stab 健全性の判定用に、安定化直前の wrapped G と厳密再構成 G の
      `||G_wrap - G_rebuild||_inf` を測る診断を追加または一時 helper として作る。
      これは PH で消える sign canary の代替であり、VALIDATION.md の
      安定化残差チェックを定量化するもの。
- [x] serial profile で、`green_from_stack`, `udv_inv_one_plus`, left `udv_lmul`,
      `green_stack_build` の回数と時間が期待どおり減るか確認する。
- [x] 出力検証:
      - half filling `sign=1` と PH path の `dN=0` は sanity として記録するが、
        stab 健全性の判定には使わない。
      - 観測量バイアス検査は `stab=2` を参照にする。`stab=4` 基準だけでは
        `stab=4` 自体が疑わしい場合を検出できない。
      - U=12 stress は `stab=2` 参照 + 十分な統計 + wrap drift 残差で判定し、
        numerical breakdown / biased drift がないことを確認する。
      - 補助として、U=12 stress の一部を two-spin 参照
        （テスト経由 `ph_symmetric=0`）でも走らせ、sign canary が復活する条件で
        破綻がないことを確認する。
- [x] 結果を `docs/` に HTML または markdown で保存し、生データを
      `data/profiling_runs/` に保存する。
- [x] Commit: `docs: record post-PH stab interval scan`

Decision gate:

- `stab=8` が 16x16 U=4 で安定かつ >=1.15x 程度効くなら、以後の
  alternating 実装は `stab=4` と `stab=8` の両方で測る。
- U=12 stress で怪しい場合、production default は変えず、stab=8 は
  U=4 16x16 専用 candidate として扱う。
- `stab=16` は scan data point として残すが、採用候補には入れない。

## Task 2: directional wrap primitives (TDD)

- [x] `green_build_Binv(const Green *G, int l, double *out)` を追加し、
      現行 `green_wrap` の inline inverse-B build を置き換える。
- [x] `green_wrap_backward(Green *G)` を追加する。
      Semantics:
      - input `G.cur_l = tau`
      - output `G.cur_l = (tau - 1 + L) % L`
      - operation: `G(tau-1) = B_{tau-1}^{-1} G(tau) B_{tau-1}`
      - pending delayed updates are flushed before wrapping
- [x] `green_wrap` は forward wrapper として維持する。
- [x] Tests:
      - for each `tau`, `green_from_scratch(tau)` -> forward wrap equals
        `green_from_scratch(tau+1)`.
      - `green_from_scratch(tau)` -> backward wrap equals
        `green_from_scratch(tau-1)`.
      - forward then backward returns to original G within tolerance.
      - delayed pending updates are flushed on backward wrap.
- [x] `make test` and `make test_slow`.
- [x] Commit: `feat(green): add backward imaginary-time wrap`

Acceptance:

- No change to forward-only behavior or profiler output except possible helper refactor.

## Task 3: prefix/suffix boundary stack primitives (TDD)

- [x] Extend `GreenStack` semantics or add helpers so both orientations are explicit:
      - suffix: `S[j] = B(L, b[j])` (existing forward right stack)
      - prefix: `S[j] = B(b[j], 0)` (needed for backward sweep)
- [x] Add build helpers for tests:
      - `green_stack_build_suffix(Green *G, GreenStack *st)` as existing behavior
      - `green_stack_build_prefix(Green *G, GreenStack *st)` for fixed-field validation
- [x] Add store/copy helper:
      - `green_stack_store(GreenStack *st, int j, const UDV *factor)`
      - copies factor-only UDV at a boundary after a block is updated
- [x] Add reconstruction helper:
      - `green_from_boundary_factors(Green *G, const UDV *prefix,
        const UDV *suffix, UDV *combined, int cur_l)`
      - computes `(I + prefix * suffix)^-1`
      - uses `PROF_GREEN_FROM_STACK`; no new profiler region.
- [x] Tests in `test_green_stack.c`:
      - prefix and suffix stacks match brute-force products for
        `L % stab != 0`, `L < stab`, `L == stab`, `stab=1`.
      - `green_from_boundary_factors(prefix[j], suffix[j])` matches
        `green_from_scratch(b[j])` for all boundaries.
      - After simulating block updates and copying boundary factors, the carried
        stack matches a freshly built prefix/suffix stack for the final field.
- [x] `make test` and `make test_slow`.
- [x] Commit: `feat(green): add prefix/suffix boundary stack helpers`

Acceptance:

- Stack reuse implementation is verified on fixed fields before touching
  stochastic sweep order.
- Existing `green_stack_build` API remains available for forward mode.

## Task 4: alternating sweep implementation behind opt-in flag

- [x] Add input key `sweep_order`:
      - `forward` (default, current behavior)
      - `alternating` (initially allowed only for `m.ph_symmetric`)
      Invalid values fail during input validation.
- [x] Add `dqmc_init_mode(...)`; keep existing `dqmc_init(...)` as forward wrapper
      so existing tests and helper programs do not churn unnecessarily.
- [x] Add PH-only alternating state to `Dqmc`:
      - current direction (`forward` or `backward`)
      - valid flags for carried prefix/suffix stacks
      - carried stack storage for `Gu`; reuse existing `stack_u` for one side
        and add at most one additional Gu boundary stack.
      - scratch UDV for suffix accumulation in backward direction
      - invalidation rules:
        - after `dqmc_init`, both carried stacks are invalid.
        - when `D.status != 0`, all carried stacks are invalid.
        - any future external field mutation or checkpoint restore must mark
          carried stacks invalid before the next sweep.
- [x] Forward sweep behavior:
      - if a valid suffix stack exists, reuse it; otherwise build it once.
      - as each block is completed, copy the updated prefix UDV into the
        carried prefix stack for the next backward sweep.
      - end at `cur_l=0` with exact rebuild and `D.sign=1` under PH.
- [x] Backward sweep behavior:
      - start from `cur_l=0`, wrap backward once to `L-1`.
      - iterate `l=L-1..0`; updates use the same local ratio formulas at
        the current `cur_l`.
      - accumulate updated suffix by right-multiplying completed blocks.
      - reconstruct from `prefix_start[j] * suffix_updated[j]` at boundaries.
      - copy suffix boundaries for the next forward sweep.
      - after slice 0, do not wrap to `L-1`; rebuild exact G(0), map PH down,
        set `D.sign=1`.
- [x] If alternating is requested for non-PH/two-spin, reject the run after
      lattice/model setup has determined `m.ph_symmetric`, with a clear
      message for this first implementation.
- [x] Tests:
      - small chain and 2x4 square PH cases, `sweep_order=alternating`,
        several sweeps, final `Gu/Gd` equals `green_from_scratch`/PH map for
        the final field within tolerance.
      - a debug test helper checks every stabilization boundary against
        `green_from_scratch` on the current field.
      - `Ltr < stab`, `Ltr % stab != 0`, `Ltr == stab`, `stab=1`.
      - invalidation fallback: force carried stack invalid before a sweep and
        confirm the build path is used and the final G/sign are unchanged.
      - parser tests for `sweep_order`.
- [x] `make test`, `make test_slow`, `make test_omp`, `make test_mpi`,
      `make test_hybrid`.
- [x] Commit: `perf(dqmc): add PH alternating sweep with carried stacks`

Acceptance:

- Default `forward` output and tests remain unchanged.
- Alternating mode is opt-in and PH-only.
- Fixed-field and per-boundary G reconstruction checks pass.

## Task 5: alternating sweep performance and statistical validation

- [x] Measure forward vs alternating with enough sweeps to amortize the first
      stack build (`nmeas >= 20`, `nbin >= 2`; for production smoke use larger).
- [x] Cases:
      - Mac serial: square 16x16, U=4, beta=4/8/16
      - kugui i2cpu serial: same
      - kugui i2cpu MPI np16 smoke: beta=16
      - stab candidates from Task 1 (`stab=4` and, if accepted, `stab=8`)
- [x] Profile acceptance:
      - steady-state `green_stack_build` calls per sweep drop near zero after
        warmup/first sweep amortization.
      - `dqmc_sweep` speedup is at least 1.15x for stab=4 beta=16, or the
        measured reason for missing it is documented.
- [x] Correctness/statistics:
      - half filling `sign=1`, `dN=0`
      - short old/new output rows differ only within jackknife error
      - ED reference: 1D L=4, U=4 with `sweep_order=alternating` matches the
        exact-diagonalization E(T) reference to the same tolerance as forward mode.
      - low-temp PH stress still passes
- [x] Save raw data and write HTML under `docs/`.
- [x] Commit: `docs: record alternating sweep validation`

Result note:

- Mac serial/statistical validation is recorded in `LOG.md` 23:36.
- kugui i2cpu PBS `844092.kugui-pbs` on `cpu121` confirmed beta=16 speedup:
  serial `1.479 -> 1.193 s/sweep` (`1.240x`) at stab=4 and
  `1.084 -> 0.889 s/sweep` (`1.219x`) at stab=8; MPI np16 smoke gave
  `1.232x` / `1.228x`.
- In every alternating profile, measurement-phase `green_stack_build` calls
  dropped to zero.
- Drift smoke did not fail, but beta=16/stab=8 alternating had larger
  pre-stabilization wrapped-G drift (`max_inf=1.47` vs forward `0.083`).
  Keep alternating opt-in and monitor drift in longer production validation
  before changing defaults.

Decision gate:

- If alternating is clean and faster, make it the recommended production mode
  for PH half-filled 16x16 runs, but keep default `forward` until a separate
  production decision commit.
- If alternating is correct but speedup is marginal after `stab=8`, keep it
  opt-in and prioritize `udv_inv_one_plus` + rank/thread scan.

## Task 6: `udv_inv_one_plus_work` low-risk internal reductions

- [ ] Add profiler visibility inside work routines:
      - wrap `la_inverse_work` with `PROF_LA_INVERSE`
      - add `PROF_LA_LOGDET` only if needed, or document logdet under
        `udv_inv_one_plus` if region count churn is not worth it
- [ ] Add `la_inverse_logdet_work(...)`:
      - computes inverse and determinant sign/logabs from one LU factorization
      - used for `M` inside `udv_inv_one_plus_work`
      - removes the separate `la_logdet_work(M)` LU pass
- [ ] Add unit-upper-triangular inverse for `T`:
      - first try LAPACK `dtrtri_("U", "U", ...)` because `T` is unit upper
        triangular; use a small back-substitution routine only if needed.
      - verify because `T` is constructed as unit upper triangular by UDV code
- [ ] Keep `det_sign` fail-fast semantics:
      - any singular/inversion/logdet failure returns nonzero
      - `*det_sign = 0`
      - caller sets `D.status`
- [ ] Tests:
      - `test_linalg`, `test_udv`, `test_green_stack`, `test_sign_regression_slow`
      - random UDV factors with D ranges up to at least `1e-50..1e50`
      - brute-force sign for small n
- [ ] Profile acceptance:
      - `udv_inv_one_plus` time drops by >=15% in beta=16 serial,
        or the change is not kept.
- [ ] Commit: `perf(linalg): reduce UDV inverse/logdet work`

Optional follow-up, not part of first pass:

- Track `det(U)` sign through QR factorization to avoid LU on orthogonal `U`.
  This requires a separate proof and stress tests because it changes sign
  accounting internals.

## Task 7: production rank/thread scan

- [ ] Use the best validated local mode from Tasks 1/5/6.
- [ ] Run kugui i2cpu throughput scan:
      - `120 ranks x 1 thread`
      - `60 ranks x 2 threads`
      - `30 ranks x 4 threads`
      - optional `16 ranks x 8 threads`
      Keep `MKL_DYNAMIC=FALSE` and record `OMP_NUM_THREADS`, `MKL_NUM_THREADS`.
- [ ] Metric:
      - replicas/hour for fixed `nrep`, `nmeas`, `nbin`
      - wall time
      - `dqmc_sweep` avg
      - BLAS-heavy regions (`la_gemm`, `udv_*`, `green_from_stack`)
- [ ] Save raw data under `data/profiling_runs/`.
- [ ] Write HTML report under `docs/`.
- [ ] Commit: `docs: record production rank-thread scan`

Acceptance:

- Production recommendation is based on replicas/hour, not serial sweep time.
- If threaded MKL hurts throughput, keep rank-dense sequential MKL as default.

## Task 8: documentation and default policy

- [ ] Update `LOG.md` with final before/after summary.
- [ ] Update relevant docs:
      - performance analysis
      - kugui validation HTML/README
      - any production run instructions that mention `stab` or sweep mode
- [ ] Decide default policy:
      - `sweep_order=forward` remains code default unless production validation
        clearly justifies changing it.
      - If `stab=8` is accepted only for U=4 16x16, document it as a
        run-configuration recommendation, not a global default.
- [ ] Run final validation ladder:
      - `make test`
      - `make test_slow`
      - `make test_omp`
      - `make test_mpi`
      - `make test_hybrid`
      - selected kugui smoke
- [ ] Commit: `docs: summarize single-spin stabilization performance work`

## Risks and Mitigations

| risk | mitigation |
|---|---|
| `stab=8` reintroduces low-temperature bias | Task 1 separates U=4 production candidate from U=12 stress; no global default change |
| alternating sweep stack reuse proof is wrong | Task 3 fixed-field prefix/suffix stack tests before stochastic integration |
| alternating changes Markov trajectory | Expected; validate G/current-field consistency plus statistical agreement, not same-seed equality |
| backward wrap has off-by-one time index | Task 2 tests every `tau` against `green_from_scratch(tau-1)` |
| PH-only implementation accidentally affects non-PH | `sweep_order=alternating` is rejected after model setup unless `m.ph_symmetric`; default forward path unchanged |
| `udv_inv_one_plus` sign optimization breaks fail-fast | Low-risk LU fusion first; QR sign tracking explicitly postponed |
| profiler nested regions cause misleading sums | Reports must use call-tree buckets: stack_build, left lmul, green_from_stack, wrap, update |

## Expected Outcome

Conservative expected sequence on kugui i2cpu, serial beta=16:

| step | expected sweep | speedup vs current | confidence |
|---|---:|---:|---|
| current PH baseline | 1.50 s | 1.00x | measured |
| accepted `stab=8` if stable | 1.25-1.35 s | 1.1-1.2x | medium |
| alternating carried stacks | 1.05-1.20 s | 1.25-1.4x | medium, design-dependent |
| `udv_inv_one_plus` low-risk reductions | ~1.0-1.1 s | 1.35-1.5x | medium-low |

The main deliverable is not just a lower serial number; it is a validated
production recipe for 16x16 low-temperature runs, including rank/thread layout.
