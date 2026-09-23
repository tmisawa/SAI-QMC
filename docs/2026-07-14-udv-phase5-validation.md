---
date: 2026-07-14
datetime: 2026-07-14 18:37 JST
model: GPT-5 Codex
summary: |
  Phase 5 DQMC validation results after green_rebuild=two_sided integration.
  DQMC-level two_sided sweep regression coverage was added, test_mpi and
  test_slow passed, and beta=30-32 wall-inside combine/two_sided comparison
  passed with identical printed observables.
---

# UDV Phase 5 Validation

## Scope

This validates the opt-in `green_rebuild=two_sided` path after Phase 4 Green
integration.

- Target model: L=4x4 file lattice, U=16, dtau=0.025, stab=4.
- Lattice file:
  `data/production_runs/kugui_F1cpu_2d4x4_U16_PP_dtau0p025_T0p04_to_0p01_nrep120_n10k_gridT_alt_fc0416a_prebuilt_U12U16_20260707/hopping_PP.txt`
- Sweep order: `alternating`.
- Seed: `14012418791647386686`.
- Compared modes: `green_rebuild=combine` and `green_rebuild=two_sided`.
- beta list: 30, 31, 32.
- Local validation length: `nwarm=1000`, `nmeas=5000`, `nbin=50`, `nrep=1`.

This is a local mode-equivalence gate, not a replacement for production-scale
multi-replica physics statistics.

## Regression Coverage

Added DQMC-level two-sided coverage:

- `tests/test_dqmc_stack.c`: one forward stack/ref comparison case now runs
  with `GREEN_REBUILD_TWO_SIDED` and checks mode propagation to Gu/Gd.
- `tests/test_dqmc_alternating.c`: one alternating/backward case now runs with
  `GREEN_REBUILD_TWO_SIDED` and checks propagation into stab_drift reference
  Green.

Verification:

- `make test` -> `ALL TESTS PASSED`.
- `make test_mpi` -> `ALL MPI TESTS PASSED`.
- `make test_slow` -> `ALL SLOW TESTS PASSED`.

Expected stderr remains from existing negative parser/alternating tests and
from the intentional `test_udv_two_sided` combine-fail case.

## Wall-Inside Statistical Comparison

Command shape:

```ini
lattice=file
latfile=data/production_runs/kugui_F1cpu_2d4x4_U16_PP_dtau0p025_T0p04_to_0p01_nrep120_n10k_gridT_alt_fc0416a_prebuilt_U12U16_20260707/hopping_PP.txt
Lx=4
Ly=4
pbc=1
t=-1.0
U=16
dtau=0.025
beta_list=30,31,32
nwarm=1000
nmeas=5000
nbin=50
stab=4
parallel=serial
nrep=1
profile=0
seed=14012418791647386686
sweep_order=alternating
green_rebuild=combine       # repeated with two_sided
replica_log=none
```

Temporary output directory:

- `/tmp/afqmc_phase5_validation.Qbk5VS`

Runtime:

- combine: `real 187.82`
- two_sided: `real 159.93`

Summary rows printed by `dqmc` were identical at output precision:

| beta | T | observable | combine | two_sided | diff | 2-sigma status |
|---:|---:|---|---:|---:|---:|---|
| 30 | 0.0333333 | E_hub | -4.5839561 | -4.5839561 | 0 | OK |
| 30 | 0.0333333 | E_gc | -132.58396 | -132.58396 | 0 | OK |
| 30 | 0.0333333 | E_ph | -68.583956 | -68.583956 | 0 | OK |
| 30 | 0.0333333 | N | 16 | 16 | 0 | OK |
| 30 | 0.0333333 | doublon | 0.015910774 | 0.015910774 | 0 | OK |
| 30 | 0.0333333 | acceptance | 0.44972805 | 0.44972805 | 0 | OK |
| 31 | 0.0322581 | E_hub | -4.6630959 | -4.6630959 | 0 | OK |
| 31 | 0.0322581 | E_gc | -132.6631 | -132.6631 | 0 | OK |
| 31 | 0.0322581 | E_ph | -68.663096 | -68.663096 | 0 | OK |
| 31 | 0.0322581 | N | 16 | 16 | 0 | OK |
| 31 | 0.0322581 | doublon | 0.015978759 | 0.015978759 | 0 | OK |
| 31 | 0.0322581 | acceptance | 0.4493698 | 0.4493698 | 0 | OK |
| 32 | 0.03125 | E_hub | -4.5880617 | -4.5880617 | 0 | OK |
| 32 | 0.03125 | E_gc | -132.58806 | -132.58806 | 0 | OK |
| 32 | 0.03125 | E_ph | -68.588062 | -68.588062 | 0 | OK |
| 32 | 0.03125 | N | 16 | 16 | 0 | OK |
| 32 | 0.03125 | doublon | 0.016104701 | 0.016104701 | 0 | OK |
| 32 | 0.03125 | acceptance | 0.44966587 | 0.44966587 | 0 | OK |

The average sign was 1 in both modes for all three beta values.

## Verdict

Phase 5 local gates are complete:

- Gate (a) was already confirmed by Phase 4 review: beta=33.325 no longer fails
  at `udv_combine stage=C`; it moves to the predicted single-factor site
  `udv_lmul_work stage=B_U_D`.
- DQMC-level two-sided sweep regression coverage is now present.
- `make test_mpi` and `make test_slow` pass.
- beta=30-32 wall-inside combine/two_sided comparison passes.

The next technical phase is Phase 6: design the single-factor scale
countermeasure, with bounded multi-factor prefix/suffix storage as the current
first candidate.
