---
date: 2026-10-27
datetime: 2026-10-27 16:08 JST
model: Claude Opus 5.5 (Claude Code)
summary: |
  Finite-temperature ED comparison of the sign-reweighted (pooled-ratio) path: 4-site chain, U=4, mu=0,
  periodic/antiperiodic, beta<=10, and 4x2 square with tprime=-0.2, U=4, mu=2 and 1, P/P and AP/P, beta<=4,
  at dtau=0.05 and 0.025. 187 of 188 comparisons satisfy the rule fixed before the runs; the remaining
  doublon point is followed up post hoc at dtau=0.0125.
  16:08: outputs regenerated after the review fixes (schema 2 header with nrep/nbeta); every data row is unchanged.
---

# Sign-reweighted DQMC versus finite-temperature ED

These runs check the path used without particle-hole symmetry: general `mu`, the
square-lattice `tprime` and the pooled sign-reweighted estimator. They were made with
this repository's source of the sign-reweighting extension (working tree on top of
`4384c37`, before it was committed). The binary itself is not shipped.

The runs were first made with `dqmc_omp` SHA-256
`fdbd153fc547f6c8c50db2e20523d532f30c17ced7baf566a21dae28b983e8d7`. After an independent
review the source was corrected (an exact particle-hole test for hopping files given `mu` or
`sign_file`, and `nrep`/`nbeta` in the `replica_bin_schema=2` header), and every input was run
again from the same seeds with `dqmc_omp` SHA-256
`09b1cfeccdf30bcfbdf12df721bef4a855a30228c429922b5eba0c53af87988e` (`make dqmc_omp`,
Apple clang 17.0.0, `-std=c11 -O2`, Accelerate, libomp). The files here are from that run.
Every data row of `observables.dat`, `sign.dat`, `szz.dat` and `bins.tsv` equals the first
run; only the schema line of `bins.tsv` differs, and `runs/comparison.json` is byte-identical.

## Conditions (fixed before the runs)

`runs/run_ed_compare.sh` writes every input and runs it:

- `L4_<bc>`: `lattice=chain Lx=4 bc_x=periodic|antiperiodic t=-1 U=4 mu=0`, beta 1, 2, 4, 10.
- `sq4x2_<bc>_mu<mu>`: `lattice=square Lx=4 Ly=2 bc_x=periodic|antiperiodic bc_y=periodic
  t=-1 tprime=-0.2 U=4 mu=2|1`, beta 1, 2, 4. The physical `t'=+0.2` of
  `H=-t sum_NN - t' sum_NNN` is `tprime=-0.2`. These lattices are not bipartite, so `mu=2=U/2`
  is not particle-hole symmetric either.
- `dtau` 0.05 and 0.025, `nwarm=1000 nmeas=20000 nbin=20 nrep=8 stab=10`, `parallel=omp`
  with 8 threads, seeds 2026102711 (chain) and 2026102712 (square), `szz_q=all`,
  `sign_file=sign.dat`, `replica_bin_file=bins.tsv`.

Rule: for each (case, beta, observable) with observables E_hub, ntot, doublon and every
Szz(q), `z0 = (X0 - ED) / err0` with `X0 = (4 X(0.025) - X(0.05)) / 3`; the point is
consistent when `|z0| <= 3`. `X0` is a diagnostic of the step dependence, not an adopted
`dtau -> 0` value. Finite-step `z` are reported separately.

## Reference

- `ed/ed_finite_t_mu.py`: grand-canonical ED over all (N_up, N_down) sectors with weight
  `exp[-beta (H - mu N)]`, `H = K + U sum n_up n_down`. Results: `ed/ed_*.json`.
- `ed/bond_table.py`: builds each hopping matrix from an image sum of the eight
  displacements, independently of the solver, and checks that every `hopping_used.txt`
  written by the program is identical before ED uses it (`ed/L4_*.txt`, `ed/sq4x2_*.txt`).
  As in the other datasets, each run directory keeps `hopping_used.sha256` (SHA-256 of the
  program's `hopping_used.txt`, which is not tracked) instead of the matrix; the values equal
  the matrices in `ed/`, written there with a different number format. Rerunning an input
  regenerates `hopping_used.txt`.

## Results

`runs/compare_ed.py runs/ed-compare ed <repository root> > runs/comparison.json`
recomputes all values from `bins.tsv` (full precision, `scripts/analyze_sign_bins.py`)
and `szz.dat`; `runs/summarize.py` writes `runs/analysis_output.md`.

- 187 of 188 comparisons satisfy `|z0| <= 3`. The exception is the doublon of
  `sq4x2_periodic_mu1` at beta=4 (`z0=3.2`; finite-step `z` = -18.0 and -1.2).
- Every ratio status is `ok`. Mean signs range from 1 to 0.40 (chain, antiperiodic,
  beta=10: 0.3995(46) at dtau 0.05 and 0.4102(43) at 0.025); the sign depends on dtau.
  No bin had a non-positive sign sum.
- The finite-step deviations of E_hub and doublon at dtau=0.05 (up to 18 errors) shrink at
  dtau=0.025 roughly as dtau^2: this is the Trotter error, not covered by the error bars.

The two step sizes use the same base seed, and the error of `X0` assumes no covariance
between them. The two series consume their random numbers differently from the start (the
fields have different numbers of time slices), but the absence of correlation has not been
checked; a further study should give each step its own seed stream or estimate the covariance
from paired replicas. This note does not change the rule or the verdict.

Verdict under the fixed rule: 187/188, not a clean pass. The post-hoc dtau=0.0125 run of the
failing case (`runs/followup/`) agrees with ED within 1.8 errors for every observable at
every beta; see the follow-up section of `runs/analysis_output.md`. It does not change the
recorded verdict.

## Scope

Small lattices and moderate temperatures only. The comparison validates the general-`mu`
propagation, the `tprime` matrix and the pooled estimator where the sign is not small; it
does not establish accuracy at small mean sign, larger lattices or lower temperatures.
