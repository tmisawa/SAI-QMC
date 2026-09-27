---
date: 2026-09-27
datetime: 2026-09-27 21:08 JST
model: Claude Sonnet 5
summary: |
  Correctness check of the dtau-ladder parallel-tempering (PT) implementation:
  PT slots compared against independent (non-PT) chains at the same
  (beta, dtau), with finite-temperature grand-canonical ED reported as
  context. Periodic L=6 Hubbard chain, U=4, mu=U/2 (half filling).
---

# Tempering validation: L=6 chain, U=4, PT vs independent chains vs ED

This is a **correctness check of the parallel-tempering (PT) machinery**, not
a scientific adoption of any of the numbers below. The pass/fail criterion is
whether PT slot `k` agrees, within statistical error, with an independent
(non-PT) chain run at the same `(beta_k, dtau_k)`. Agreement with the
finite-temperature ED reference is reported for context only — a difference
from ED is **not** automatically attributed to Trotter (finite-`dtau`) error
here (see "Reference: PT and independent-chain E/N, D vs finite-T ED and
dtau^2 scaling" below for what is checked instead). `T=0` agreement is not the
criterion either — all runs are at finite temperature.

**Passing the `|z|<3` screen below shows that PT and the independent chains
are statistically consistent within their respective SEs. It does not show
that PT and the independent chains reach equal precision (their SEs differ
row by row — see the table), and it is not a general proof that the PT
implementation is correct beyond the cases and the statistics actually
reached here.**

## Conditions

- Lattice: periodic chain, `Lx=6`, `U=4`, `mu=U/2=2` (half filling, grand
  canonical).
- Slots / temperatures: `T = 0.25, 0.2, 0.15, 0.1`, i.e.
  `beta_list=4,5,6.666666666666667,10` — all four values sit exactly on the
  ED reference grid (no interpolation needed).
- PT: `tempering=dtau_ladder tempering_ltr=200 tempering_interval=1`, so
  `dtau_k = beta_k/200` = 0.02, 0.025, 0.0333..., 0.05 for slots 0..3. No
  `dtau` key (forbidden with PT). `seed=270927001`.
- Independent chains: one run per slot, single-value `beta_list`, explicit
  `dtau` equal to the PT slot's `dtau_k` (checked in Python beforehand that
  `beta/dtau` is exactly 200.0 in double precision for all four, including
  `beta=6.666666666666667`, `dtau=0.03333333333333333`).
  `seed=270927101..270927104` (one base seed per slot).
- Common to all five inputs: `nwarm=2000`, `nmeas=50000`, `nbin=100`,
  `nrep=16`, `parallel=omp`, `replica_bin_file=...`, `szz_q=all
  szz_file=...`, `sperp_q=all sperp_file=...` (so that `q=0` and the AF
  wavevector `Q` (`szz_Q_index=3`, `szz_0_index=0`, `Lx=6`) are both
  measured; the default `szz_q=none` would not measure `q=0`, and would make
  `szz_Q_index=szz_0_index=-1` — `analyze_tempering_ed.py` asserts these
  indices against this Lx=6 chain's expected values before using either
  column, so a run like that is rejected rather than silently analyzed as
  NaN; see "Analysis method and validation gate" below).
- `nmeas=50000` is the value used for every file in this directory now (an
  earlier `nmeas=20000` pass is reproducible from these same files, not a
  separately-run dataset — see "On the nmeas=20000 pass" below).

## Build

- Repository `SAI-QMC`, branch `feat/tempering`, commit
  `c4b3dea1b95fbd8db6588b76d5c9b9fbb86e556d`.
- `make dqmc_omp dqmc` (source touched first to force a full rebuild); both
  built cleanly with `-Wall -Wextra`, no warnings.
- Binary SHA-256:
  - `dqmc_omp`: `0ad01e782adb786e09acb664a5fc225f024875c7f613ec5732979ec9909f7a11`
  - `dqmc`: `d79c3b36af4520f0e37e67d07f0416eea72f96d5d71e808f7e933bd877652ce1`
- All five runs used `OMP_NUM_THREADS=4` on an 8-logical-CPU machine already
  under load (load average ~11 at the start of the session), run strictly
  sequentially (never two runs at once), each timed with `/usr/bin/time -p`.

## Run times (nmeas=50000, the pass described throughout this file)

| run | beta | real (s) |
|---|---|---:|
| `input_fixed_b1.in` | 4 | 64.99 |
| `input_fixed_b2.in` | 5 | 62.50 |
| `input_fixed_b3.in` | 6.666666666666667 | 60.86 |
| `input_fixed_b4.in` | 10 | 60.76 |
| `input_pt.in` | (all 4 slots) | 385.52 (stdout also reports `solver_elapsed_seconds=385.518`) |

All five runs exited 0; no NaN/non-finite `log R`, no ladder numerical
failures, in any stdout. (The earlier `nmeas=20000` pass — see "On the
nmeas=20000 pass" below — took 26.25/25.99/25.89/25.65 s for the four
independent chains and 170.00 s for PT; those raw files no longer exist,
only the timings are quoted here.)

## Seed derivation and uniqueness

`replica_seed(base_seed, beta_index, replica_id)` from `src/replica.c`
(the legacy `base_seed + 1000*beta_index` branch for `replica_id==0`, the
splitmix64-mixing branch otherwise) was ported to Python bit-exactly
(64-bit modular arithmetic) at the top of `analyze_tempering_ed.py`. Before
any statistical analysis, the script:

1. Enumerates, from the formula alone, every seed the run should use: the
   `4 slots x 16 replicas = 64` PT per-slot chain seeds
   (`replica_seed(seed, k, r)`), the `16` PT exchange/swap seeds
   (`replica_seed(seed, nbeta=4, ladder)`), and the `4 bases x 16 replicas =
   64` independent-chain seeds (`replica_seed(base_k, 0, r)`) — 144 in total
   — and checks that all 144 are pairwise distinct. They are.
2. Cross-checks the formula against the seeds actually written to disk:
   all 64 `(beta_index, replica_id)` chain seeds in `bins_pt.tsv`, all 16
   `swap_seed` values in the `ladder` rows of `pt.tsv`, and all 16 replica
   seeds in each `bins_fixed_b{1..4}.tsv` match the Python port of
   `replica_seed()` exactly (this also happens to double as an independent
   verification of the C implementation, not just of the Python port). The
   script asserts the *expected count* found in each place (64, 16, 64) —
   an empty or truncated file fails loudly here rather than printing a
   vacuously-true "0 ... match".

## Analysis method and validation gate

For each of the 4 slots, `analyze_tempering_ed.py` sums `sum_sign`,
`sum_sign_Ehub`, `sum_sign_D`, `sum_sign_Szz_Q`, `sum_sign_Szz_0` over all
100 bins **within each of the 16 replicas** first (ratio-of-sums per
replica, not average-of-bin-ratios), giving one value per replica for:

- `E/N = (sum_sign_Ehub/sum_sign) / 6` (`sum_sign_Ehub` is the **total**
  energy, not per site).
- `D = sum_sign_D / sum_sign` (`sum_sign_D` is already per site).
- `M^2 = 6 * sum_sign_Szz_0 / sum_sign` (`Sz_i = (n_up-n_down)/2`,
  `Szz(q)` is `1/N`-normalized, so `M^2 = N*Szz(q=0)`).
- `Szz(Q) = sum_sign_Szz_Q / sum_sign` (`Q` = staggered/AF wavevector,
  `mx=Lx/2=3` for `Lx=6`).

and then takes the mean and SE (`sample std, ddof=1, / sqrt(16)`) **across
the 16 replicas**. The same procedure is applied to the PT run (grouped by
`beta_index`) and to each independent-chain file (single `beta_index=0`),
then `z = (PT_mean - indep_mean) / sqrt(PT_SE^2+indep_SE^2)` over the
`4 slots x 4 observables = 16` comparisons. The ED lookup (used only in
"Reference: ... vs finite-T ED" below, not in the screen above) uses the
exact grid rows of
`../benchmark_L468_U4_full_diag_20260626/reference/L6_U4_FullDiag.dat`
(`T=0.25,0.2,0.15,0.1`, present verbatim, no interpolation),
`E_hub = E_gc + 2*N`, `D = D_total/6`; the `S2` column is **not** used
(its definition relative to `M^2`/`Szz` here is unconfirmed).

Before computing any of this, and while parsing each `bins_*.tsv` file, the
script checks assumptions it would otherwise take on faith, and aborts with
a non-zero exit and a specific message if any of them fails, rather than
continuing to a plausible-looking but wrong screen:

- Each file's own `# columns:` header line is parsed and compared against
  the hardcoded column list this script uses to index rows positionally;
  a mismatch aborts instead of silently misreading columns.
- Each file's `# szz_Q_index=... szz_0_index=... sperp_Q_index=...` header
  line is parsed and checked against the values expected for this `Lx=6`
  chain (`3`, `0`, `3`). If `szz_q` had not been `all` (e.g. the default
  `szz_q=none`), these would be `-1` and the `sum_sign_Szz_Q`/
  `sum_sign_Szz_0` columns would be `NaN` — exactly the case this guards
  against (see "the bug this replaces" below).
- `sum_sign == count` for every single row: the ratio-of-sums-equals-a-
  plain-average shortcut this analysis relies on assumes the run is
  sign-free (`sign` is exactly 1 throughout — bipartite, half-filled,
  particle-hole-symmetric). This is checked, not assumed.
- Each independent-chain file's `(beta_requested, Ltr)` is asserted equal to
  its corresponding PT slot's, and both equal the intended `(beta_k, 200)` —
  i.e. that slot `k` of the PT run and `input_fixed_b{k+1}.in` really
  describe the same physical point, not just that they happen to be listed
  in the same script position.
- Every per-replica ratio, and the resulting per-slot mean, SE, and `z`,
  must be finite (`math.isfinite`). A non-finite value raises `SystemExit`
  immediately, identifying the slot, observable, and (for per-replica
  values) which replicas were affected.
- At the end, if any of the 16 comparisons has `|z| >= 3`, the script exits
  non-zero (in addition to printing `FAIL` on stdout) — a real fail no
  longer looks the same as a pass to an automated caller that only checks
  the exit code.

**The bug this replaces:** an earlier version of this script computed
`z = diff / combined_se` and then `flagged = abs(z) >= 3` and
`max_abs_z = max(max_abs_z, abs(z))` with no finiteness check anywhere, and
never called `sys.exit`/`raise SystemExit` on `n_flagged > 0`. Since
`abs(nan) >= 3` is `False` and `max(x, nan)` (with `x` first) returns `x` in
Python, a `NaN` row was silently excluded from both the flag count and the
max-`|z|` tracking while still being counted in `"Comparisons: 16"`, and the
process exited 0 with `"PASS"` printed regardless. This was reproduced (in a
throwaway copy, not committed here) by setting `sum_sign_Szz_0` to `nan` in
one independent-chain file — the kind of file a run without `szz_q=all`
would actually write — which is now instead rejected with exit code 1 and
`independent slot 0: non-finite per-replica value(s) for M2 at replica(s)
[0, 1, ..., 15]: [nan, nan, ..., nan]` before any comparison table is even
printed. The same fix also made a genuine `|z|>=3` fail (no `NaN` involved)
exit non-zero, which the earlier version did not do either. Re-running the
fixed script against the actual, uncorrupted files in this directory
reproduces the same 16 `z` values as before (see "Result" below) and exits
0.

## Result: PT vs independent-chain screen

`z = (PT_mean - Indep_mean) / sqrt(PT_SE^2 + Indep_SE^2)`.

| slot | beta | T | observable | PT mean | PT SE | Indep mean | Indep SE | diff | z |
|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| 0 | 4 | 0.25 | E/N | -0.578191 | 4.40e-4 | -0.577948 | 3.46e-4 | -2.43e-4 | -0.43 |
| 0 | 4 | 0.25 | D | 0.106584 | 7.23e-5 | 0.106506 | 4.31e-5 | 7.74e-5 | 0.92 |
| 0 | 4 | 0.25 | M^2 | 0.116095 | 8.33e-4 | 0.115113 | 7.62e-4 | 9.82e-4 | 0.87 |
| 0 | 4 | 0.25 | Szz(Q) | 0.501364 | 4.91e-4 | 0.500860 | 5.93e-4 | 5.04e-4 | 0.66 |
| 1 | 5 | 0.20 | E/N | -0.599451 | 3.60e-4 | -0.599215 | 2.49e-4 | -2.36e-4 | -0.54 |
| 1 | 5 | 0.20 | D | 0.108714 | 7.64e-5 | 0.108648 | 4.56e-5 | 6.67e-5 | 0.75 |
| 1 | 5 | 0.20 | M^2 | 0.050577 | 3.71e-4 | 0.051301 | 7.41e-4 | -7.23e-4 | -0.87 |
| 1 | 5 | 0.20 | Szz(Q) | 0.519052 | 6.33e-4 | 0.519161 | 7.53e-4 | -1.09e-4 | -0.11 |
| 2 | 6.666667 | 0.15 | E/N | -0.609613 | 3.14e-4 | -0.610496 | 3.74e-4 | 8.84e-4 | **1.81** |
| 2 | 6.666667 | 0.15 | D | 0.110182 | 4.51e-5 | 0.110210 | 4.75e-5 | -2.77e-5 | -0.42 |
| 2 | 6.666667 | 0.15 | M^2 | 0.013041 | 2.70e-4 | 0.012643 | 5.03e-4 | 3.99e-4 | 0.70 |
| 2 | 6.666667 | 0.15 | Szz(Q) | 0.527616 | 3.20e-4 | 0.527642 | 5.42e-4 | -2.69e-5 | -0.04 |
| 3 | 10 | 0.10 | E/N | -0.615171 | 3.52e-4 | -0.615716 | 3.36e-4 | 5.45e-4 | 1.12 |
| 3 | 10 | 0.10 | D | 0.110412 | 4.38e-5 | 0.110460 | 5.57e-5 | -4.84e-5 | -0.68 |
| 3 | 10 | 0.10 | M^2 | 0.001022 | 8.04e-5 | 0.001058 | 2.41e-4 | -3.65e-5 | -0.14 |
| 3 | 10 | 0.10 | Szz(Q) | 0.529779 | 5.20e-4 | 0.529541 | 5.02e-4 | 2.38e-4 | 0.33 |

**16/16 comparisons have `|z| < 3`. Max `|z| = 1.808`** (not 1.81 — the
printed table above rounds to 3 significant figures; `analysis_output.txt`
and `pt_vs_independent_screen.tsv` carry full precision, where the slot-2
`E/N` row is `z=1.808`).

**Verdict: PASS — no indication of a PT correctness problem within the
statistics reached here.** As stated at the top of this file: this shows
statistical consistency within each row's own SE, not equal PT/independent
precision (compare the `PT SE`/`Indep SE` columns — they differ, sometimes
by almost 2x), and it is not a proof that PT is correct in general.

## Achieved SE vs precision targets

Target: `E/N` SE `<= 5e-4 t`, `D` SE `<= 2e-4`.

| slot | beta | run | E/N SE | meets target | D SE | meets target |
|---:|---:|---|---:|:---:|---:|:---:|
| 0 | 4 | PT | 4.40e-4 | Y | 7.23e-5 | Y |
| 0 | 4 | independent | 3.46e-4 | Y | 4.31e-5 | Y |
| 1 | 5 | PT | 3.60e-4 | Y | 7.64e-5 | Y |
| 1 | 5 | independent | 2.49e-4 | Y | 4.56e-5 | Y |
| 2 | 6.666667 | PT | 3.14e-4 | Y | 4.51e-5 | Y |
| 2 | 6.666667 | independent | 3.74e-4 | Y | 4.75e-5 | Y |
| 3 | 10 | PT | 3.52e-4 | Y | 4.38e-5 | Y |
| 3 | 10 | independent | 3.36e-4 | Y | 5.57e-5 | Y |

All 16 achieved SEs meet their targets.

## On the nmeas=20000 pass

All five inputs were first run once at `nmeas=20000` (the value stated in
the original task brief). That pass's own screen already showed 16/16
`|z| < 3` with max `|z| = 1.326` — never an indication of a PT correctness
problem. But its achieved `E/N` SE exceeded the `5e-4` precision target in 6
of the 16 relevant rows (PT slots 0, 1, 3; independent slots 1, 2, 3), while
`D`'s SE met target everywhere. Per this validation's own precision-target
rule (increase `nmeas` and rerun if the target is not met — this rule comes
from the task brief/plan, not from the analysis script), `nmeas` was raised
to 50000 (2.5x) in all five input files, reusing the same seeds, and all
five runs were redone; that `nmeas=50000` pass is the one described
everywhere else in this file.

**The `nmeas=20000` pass is *not* independent evidence and is not kept as a
separate directory.** Bins are contiguous blocks of `nmeas/nbin` sweeps each,
in sweep order (`src/replica_run.c`, `src/tempering_run.c`: the measurement
loop is `for bin in 0..nbin: for i in 0..nmeas/nbin: sweep(); measure()`).
Both passes used `nbin=100` and the same seeds, so with `nmeas=50000` each
bin covers 500 sweeps and the first 40 bins (`bin_id < 40`) cover sweeps
1-20000 — the exact same 20000 measurement sweeps, from the exact same
seeded RNG stream after the exact same `nwarm=2000` warmup, that the whole
`nmeas=20000` pass consisted of. Summing only bins 0-39 of these
`nmeas=50000` files therefore reproduces the `nmeas=20000` pass's numbers,
not just approximately but to floating-point summation-order noise: run

```sh
python3 analyze_tempering_ed.py --max-bin 40
```

which prints `max |z| = 1.326; flagged (|z|>=3): 0` — the exact number the
`nmeas=20000` pass reported — reconstructed entirely from the `nmeas=50000`
files already in this directory. (Checked directly against the two passes'
raw PT bins before this option existed: every `(beta_index, replica_id)`
`sum_sign`/`sum_sign_Ehub`/`sum_sign_D`/`sum_sign_Szz_Q`/`sum_sign_Szz_0`
total from the first 40 bins of the final `bins_pt.tsv` agreed with the
`nmeas=20000` PT run's own raw bins to better than `1e-10` relative; the
independent-chain aggregated means/SEs agreed with the recorded
`nmeas=20000` screen to about `5e-14` relative — both consistent with pure
floating-point summation-order differences, not a physics difference.)

## Reference: PT and independent-chain E/N, D vs finite-T ED, and vs dtau^2 scaling

Not a pass/fail criterion. For `E/N` and `D` at each slot, this reports (a)
`PT - ED` and `independent - ED`, and (b) the offset a leading-order
Trotter (`dtau^2`) model predicts, `slope(T) * dtau_k^2`, where `slope(T)`
is `E_slope_dtau2`/`D_slope_dtau2` from the existing same-system
(`L=6, U=4, mu=2`) three-point `dtau`-extrapolation table,
`../L6_U4_mu2_dtau_ed_comparison/qmc_L6_U4_energy_doublon_dtau_extrap_vs_ED.csv`,
linearly interpolated in `T` (that table's grid is `beta =
0.6, 0.8, ..., 12.0`, which does not include `beta = 5` or `6.666...`;
`E_slope_dtau2` is converted from total-energy to per-site (`E/N`) units by
dividing by 6; `D_slope_dtau2` is already per site in that table). The
deviation of each observed difference from this prediction, in units of the
observed quantity's own SE (`ED` itself is treated as exact; the slope's own
fit uncertainty is not propagated), is reported as "consistent" or
"inconsistent" with `dtau^2` scaling at the `3`-SE level used elsewhere in
this file — **this is a reference check using a 3-point slope from a
different beta grid interpolated onto these slots' T values, not a proof of
a Trotter-error origin, and not a controlled `dtau -> 0` extrapolation of
this run's own data.**

| slot | beta | T | dtau | obs | PT-ED | PT SE | Indep-ED | Indep SE | predicted dtau^2 offset | PT dev/SE | Indep dev/SE |
|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 0 | 4 | 0.25 | 0.02 | E/N | -8.72e-4 | 4.40e-4 | -6.29e-4 | 3.46e-4 | -6.38e-4 | -0.53 | 0.03 |
| 0 | 4 | 0.25 | 0.02 | D | -4.37e-5 | 7.23e-5 | -1.21e-4 | 4.31e-5 | -9.62e-5 | 0.73 | -0.58 |
| 1 | 5 | 0.20 | 0.025 | E/N | -1.33e-3 | 3.60e-4 | -1.10e-3 | 2.49e-4 | -1.14e-3 | -0.53 | 0.18 |
| 1 | 5 | 0.20 | 0.025 | D | -1.79e-4 | 7.64e-5 | -2.46e-4 | 4.56e-5 | -1.30e-4 | -0.65 | -2.55 |
| 2 | 6.666667 | 0.15 | 0.033333 | E/N | -1.09e-3 | 3.14e-4 | -1.97e-3 | 3.74e-4 | -1.71e-3 | 1.98 | -0.70 |
| 2 | 6.666667 | 0.15 | 0.033333 | D | -2.73e-4 | 4.51e-5 | -2.46e-4 | 4.75e-5 | -2.58e-4 | -0.33 | 0.27 |
| 3 | 10 | 0.10 | 0.05 | E/N | -3.90e-3 | 3.52e-4 | -4.45e-3 | 3.36e-4 | -4.45e-3 | 1.55 | 0.00 |
| 3 | 10 | 0.10 | 0.05 | D | -6.08e-4 | 4.38e-5 | -5.59e-4 | 5.57e-5 | -6.32e-4 | 0.55 | 1.30 |

**8/8 (slot, observable) rows have both the PT and the independent
ED-difference within 3 SE of the predicted `dtau^2` offset** — the largest
single deviation is the independent chain's `D` at slot 1
(`-2.55` SE). In that limited sense, none of these eight rows contradicts
`dtau^2` scaling; this is not the same as demonstrating it (no
`dtau -> 0` extrapolation of this run's own multiple-`dtau` data was done,
and each slot here also has a different `T`, which the `T`-interpolation of
the reference slope only approximately corrects for).

## Files

- `input_pt.in`, `input_fixed_b1.in`..`input_fixed_b4.in`: the five inputs
  (`nmeas=50000`).
- `bins_pt.tsv` (1.1M), `bins_fixed_b{1..4}.tsv` (~270-320K each):
  replica-bin sums, one row per `(beta_index, replica_id, bin_id)`; see the
  in-file header comments for the exact column meaning and normalization.
- `pt.tsv` (245K): PT-specific diagnostics (pair-exchange attempts/accepts,
  per-bin walker/slot occupancy, round trips, swap seeds, worker-second cost
  breakdown) — see its header comment for the column layout.
- `szz_pt.tsv`/`sperp_pt.tsv`, `szz_fixed_b*.tsv`/`sperp_fixed_b*.tsv`: full
  `q`-resolved structure-factor output (`szz_q=all`/`sperp_q=all`); the
  screen above only uses the `Q` and `q=0` columns already summarized in the
  `bins_*.tsv` files.
- `out_pt.dat`, `out_fixed_b*.dat`: the program's own scalar stdout (one row
  per beta; PT's row includes `solver_elapsed_seconds`).
- `time_pt.txt`, `time_fixed_b*.txt`: `/usr/bin/time -p` output.
- `analyze_tempering_ed.py`: the analysis script described above, including
  the `--max-bin N` option used in "On the nmeas=20000 pass".
- `analysis_output.txt`: full stdout of the script's default (no
  `--max-bin`) run against the files in this directory.
- `pt_vs_independent_screen.tsv`: the 16-row PT-vs-independent table
  (machine-readable).
- `pt_independent_vs_ed.tsv`: the 8-row PT/independent-vs-ED-and-`dtau^2`
  table (machine-readable).

There is no `first_pass_nmeas20000/` (or similar) subdirectory — see "On the
nmeas=20000 pass" above for why, and how to reconstruct that pass's numbers
from the files listed here instead.

## Reproducing

From the repository root, with `feat/tempering` checked out at
`c4b3dea1b95fbd8db6588b76d5c9b9fbb86e556d` (or later, if this directory is
regenerated against a newer commit — update the SHA-256 and commit
references above if so):

```sh
make dqmc_omp
OMP_NUM_THREADS=4 ./dqmc_omp data/tempering_L6_U4_ed_20260927/input_pt.in
OMP_NUM_THREADS=4 ./dqmc_omp data/tempering_L6_U4_ed_20260927/input_fixed_b1.in
OMP_NUM_THREADS=4 ./dqmc_omp data/tempering_L6_U4_ed_20260927/input_fixed_b2.in
OMP_NUM_THREADS=4 ./dqmc_omp data/tempering_L6_U4_ed_20260927/input_fixed_b3.in
OMP_NUM_THREADS=4 ./dqmc_omp data/tempering_L6_U4_ed_20260927/input_fixed_b4.in
python3 data/tempering_L6_U4_ed_20260927/analyze_tempering_ed.py \
  | tee data/tempering_L6_U4_ed_20260927/analysis_output.txt
```

Run them sequentially, not concurrently, if the machine is shared/loaded.
The five `dqmc_omp` runs took a combined ~635 s (real time) on an
8-logical-CPU machine with `OMP_NUM_THREADS=4` under moderate background
load; expect this to vary with hardware and load.

**Run this in a copy of the directory, not in place, if you want to keep
the committed files unchanged.** Rerunning overwrites wall-time-dependent
bytes even when the physics is unchanged: `time_*.txt`, the
`solver_elapsed_seconds` line in `out_pt.dat`, and the per-ladder
worker-second `cost` rows in `pt.tsv` will all differ run to run on the same
hardware, let alone across machines. `analyze_tempering_ed.py` regenerates
`analysis_output.txt`, `pt_vs_independent_screen.tsv`, and
`pt_independent_vs_ed.tsv` in place (the command above does this
explicitly for `analysis_output.txt` via `tee`; the two `.tsv` files are
always written by the script itself) — the physics-bearing numbers in all
three should reproduce exactly (same seeds, same `dqmc_omp`), but bytes
that encode wall-clock time will not.
