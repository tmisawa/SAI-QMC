---
date: 2026-07-02
datetime: 2026-07-02 22:20 JST
model: GPT-5 Codex
status: baseline
topic: PH 後 single-spin stabilization work の baseline 固定
summary: |
  single-spin stabilization performance plan の Task 0 として、PH 対称性実装後の
  kugui i2cpu baseline を固定した。実装 baseline は 211d4d6、現在 HEAD
  51b5fff は docs 追加のみ。16x16 beta=16 serial は 1.5007 s/sweep、MPI np16 は
  1.5060 s/sweep。非重複の安定化系 bucket は
  green_stack_build + left udv_lmul + green_from_stack = 約0.945 s/sweep、
  全体の約63%。local validation ladder は make test / test_slow / test_omp /
  test_mpi / test_hybrid すべて通過。
---

# Post-PH Stabilization Baseline

## Scope

この文書は `docs/superpowers/plans/2026-07-02-dqmc-single-spin-stabilization-performance.md`
の Task 0 baseline lock である。以後の `stab` scan、交互スイープ、`udv_inv_one_plus`
削減、rank/thread scan はこの baseline と比較する。

- Implementation baseline: `211d4d6 feat: exploit PH symmetry at half filling`
- Current HEAD when locked: `51b5fff docs: plan single-spin stabilization work`
- Current HEAD の 211d4d6 以降の差分は docs/review/plan のみで、コード挙動は同じ。
- Primary data source: `data/profiling_runs/kugui_i2cpu_ph_symmetry_211d4d6_20260702/`
- Validation report: `docs/2026-07-02-kugui-i2cpu-ph-symmetry-validation.html`

## Validation Ladder

Task 0 の local validation:

| command | result |
|---|---|
| `make test` | PASS |
| `make test_slow` | PASS |
| `make test_omp` | PASS |
| `make test_mpi` | PASS |
| `make test_hybrid` | PASS |

## Baseline Speed

`summary_speedup.tsv` の PH 後 new rows:

| mode | beta | sweep avg | real time | note |
|---|---:|---:|---:|---|
| serial | 4 | 0.3617 s | 1.01 s | old b77afcb から 1.989x |
| serial | 8 | 0.7401 s | 1.94 s | old b77afcb から 1.993x |
| serial | 16 | 1.5007 s | 3.84 s | old b77afcb から 1.975x |
| MPI np16 | 16 | 1.5060 s | 4.83 s | old b77afcb から sweep 1.997x / wall 2.199x |

以後の performance work は serial beta=16 の 1.5007 s/sweep と、
MPI np16 beta=16 の 1.5060 s/sweep を主基準にする。

## Serial Beta=16 Decomposition

Data: `profile_new_serial_b16.csv`, phase `measurement`, 2 sweeps.

Profiler regions are nested. The table below uses call-tree buckets intended
for planning:

| bucket | total / 2 sweeps | sec/sweep | fraction of sweep | meaning |
|---|---:|---:|---:|---|
| `green_stack_build` | 0.5596 s | 0.2798 s | 18.6% | sweep 冒頭の右 stack 構築 |
| left `udv_lmul` | 0.3581 s | 0.1790 s | 11.9% | 更新済み block の左 UDV 延長 |
| `green_from_stack` | 0.9729 s | 0.4865 s | 32.4% | `udv_combine` + `udv_inv_one_plus` を含む再構成 |
| `green_wrap` | 0.5224 s | 0.2612 s | 17.4% | 160 wraps/sweep |
| `green_update` | 0.3635 s | 0.1817 s | 12.1% | delayed update + PH 後の更新 |
| `measure_sample` | 0.0003 s | 0.0001 s | <0.1% | measurement |
| residual | ~0.2247 s | ~0.1124 s | 7.5% | loop/profiler overhead and unprofiled work |
| `dqmc_sweep` | 3.0014 s | 1.5007 s | 100% | total |

Planning bucket:

`green_stack_build + left udv_lmul + green_from_stack = 0.9453 s/sweep`

This is 63.0% of the current serial beta=16 sweep. It is the target surface for
the single-spin stabilization plan.

Nested rows for context:

- `udv_rmul`: 0.3485 s / 2 sweeps, mostly inside `green_stack_build`
- `udv_combine`: 0.4110 s / 2 sweeps, inside `green_from_stack`
- `udv_inv_one_plus`: 0.5619 s / 2 sweeps, inside `green_from_stack`
- `la_gemm`: 1.7532 s / 2 sweeps, nested across stack/wrap/reconstruction/update

## MPI np16 Beta=16 Check

Data: `profile_new_mpi_np16_b16.csv`, phase `measurement`, 32 aggregate sweeps.
Per-sweep values are consistent with serial:

| bucket | total / 32 sweeps | sec/sweep |
|---|---:|---:|
| `green_stack_build` | 8.9915 s | 0.2810 s |
| left `udv_lmul` | 5.7478 s | 0.1796 s |
| `green_from_stack` | 15.5954 s | 0.4874 s |
| `green_wrap` | 8.4299 s | 0.2634 s |
| `green_update` | 5.8345 s | 0.1823 s |
| `dqmc_sweep` | 48.1927 s | 1.5060 s |

The same stabilization bucket is the main target under MPI as well.

## Interpretation

The next optimization work should not target `green_update` first: after delayed
update and PH, it is about 12% of the sweep. The highest-value path is:

1. `stab` interval scan with `stab=2` reference and wrap-drift residuals.
2. PH-only alternating sweep with carried prefix/suffix stacks, opt-in.
3. Low-risk `udv_inv_one_plus_work` reductions.
4. Production rank/thread scan judged by replicas/hour.

`sign=1` and `dN=0` remain useful PH sanity checks, but they are not stability
canaries for the `stab` scan because PH enforces both structurally.
