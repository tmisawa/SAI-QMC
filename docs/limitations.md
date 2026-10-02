---
date: 2026-09-23
datetime: 2026-09-23 15:12 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Supported scope and known numerical/statistical limitations of the initial source snapshot.
  Separates passing implementation tests from unresolved production convergence checks.
  Clarifies input syntax and supported geometry for spin measurements.
---

# Known limitations

## Physical scope

The command-line program targets the repulsive, half-filled Hubbard model on
bipartite lattices. Doping, non-bipartite frustrated lattices, and general
interactions are outside this distribution's validated scope. Historical
planning documents may discuss features that are not implemented.

Compare finite-temperature results with grand-canonical ED at the same chemical
potential. The mean particle number equals the site count at half filling,
but this does not make a finite system equivalent to canonical ED at finite
temperature. Match hopping/boundary conventions, especially small periodic
lattices. Finite `dtau` bias requires an extrapolation or separate estimate.

## Low-temperature stability

`green_rebuild=centered` is opt-in. For the specifically tested 4x4 periodic
lattice, U=16, dtau=0.025, stab=4, four seeds completed 11000 sweeps at beta=33.325.
For one tested seed and that run length, the observed failure boundary was
`33.75 < beta_fail <= 34.0`. Beta=50 and 100 were unsupported in those tests.
These values are not universal limits or guarantees for other seeds and models.
See [the full applicability study](2026-07-15-udv-phase6a-a5-validation.md).

The default remains `combine`. A structured UDV-chain extension was investigated
but is not part of the implemented low-temperature solution.

## Spin statistics

Tests validate estimators and data aggregation, while a production simulation
must also equilibrate and sample sufficiently. A 24-seed test of a 4-site
chain at U=8, beta=2 found pooled-bin `dSperp` smaller than independent-seed
uncertainty (ratios 0.75–0.88). This does not establish a universal correction.
Compare independent runs; a bin-width plateau alone is insufficient.

In the 4x4 U=4, dtau=0.025 study, a short beta=24 canary failed SU(2) consistency;
longer sampling passed, including a second independent seed series. Beta=4 also
passed. The remaining beta=8,12,16,32 run had been submitted when the source
record stopped on August 22; its results are not included here.

In the 4x4 U=12 study, beta=32 independent runs disagreed by 3.41 reported
standard errors at the antiferromagnetic momentum, and by up to 6.14 over all
momenta. These data do not establish a low-temperature plateau. A subsequent
time-step scan remained on hold. Energy/doublon also require separating
time-step systematics from possible reference-convention differences.

The corresponding analysis summaries, including failed checks, are under
`jobs/`. They are historical result packages, not scheduler submission recipes.
An old diagnostic suggestion to run with `nbin=1` is incompatible with the
current input requirement `nbin>=2` and should not be used as a runnable example.

## Parallel tempering

The optional `tempering=dtau_ladder` mode is validated only within the scope
above: every slot must already be a sign-free, particle-hole-symmetric model
at half filling on a bipartite lattice, and the same finite-`dtau` Trotter
bias applies independently to each slot's own `dtau_k`. Exchanging
configurations between slots does not reduce or otherwise change that bias.

A ladder's slots and exchanges all run on one MPI rank and one OpenMP thread
for the ladder's entire lifetime; the current implementation does not
distribute a single ladder's slots across ranks or threads, so a ladder's
per-rank/per-thread cost scales with its own number of slots. The
diagnostic and profiling files rejected together with PT (`stab_drift_file`,
`udv_scale_file`, `udv_centered_file`, `global_site_diag_file`, `profile=1`)
have not been given a per-slot, per-ladder meaning; using them requires
`tempering=none`. The `beta_list` temperature ladder is fixed by the input
file; there is no feedback-optimized, or otherwise automatic, placement of
temperatures based on observed exchange acceptance rates.

One failed ladder fails the whole run: the other ladders still run to
completion, but the run exits nonzero and writes no observables for any
ladder; only `tempering_file` is written in full, with `failed=1` in each
failed ladder's row. A failure late in a long run therefore loses the
observables of every ladder in that run. With exactly two slots, only every
second exchange round has a pair to try, so the effective exchange interval
is `2*tempering_interval` sweeps.

`field_init=uniform` (available with or without PT) starts from the all-`+1`
field, the configuration of largest numerical scale. Its up-spin product
`B_{L-1}...B_0` has largest scale about `exp(Ltr*lambda + beta*w)`, with
`lambda = acosh(exp(dtau*U/2))` and `w` the largest eigenvalue of the
hopping matrix (`4|t|` on the periodic square lattice, `2|t|` on the
periodic chain). When
this exponent passes the double-precision limit `ln(DBL_MAX) ≈ 709.78` by a
margin of order one (1.0 to 1.4 in the measured 4x4 case), the run fails at
initialization, before any sweep
(`udv_lmul_work non-finite matrix at stage=qr_raw`, then `dqmc_init failed`,
nonzero exit). On the periodic 4x4 square lattice at `U=8` and
`dtau=0.0125`, `beta=24` (`Ltr=1920`, exponent 708.2) keeps about 1.6
e-folds of margin, while `beta=24.5`, `25`, and `26` fail at initialization.
Under PT the coldest slot (largest `beta_k` and `dtau_k`) sets the bound.
Inputs are not checked against this bound in advance; check it before using
`field_init=uniform` at low temperature or large `Ltr`.

## Distribution details

Spin structure factors currently require built-in chain/square geometry.
For open boundaries the wave vectors label discrete Fourier samples of the site
coordinates, rather than translation-symmetry quantum numbers.
All-momentum spin measurements use memory proportional to site/momentum counts
and gathered replica bins; use selected momenta for large systems.
The scalar `output_file` is checked against inputs and other active outputs,
including existing file identities and aliases through existing parent directories.
Collision checks among the older output settings still compare path strings.
Use separate working directories and distinct paths for saved runs.
No compiled executables or external libraries are shipped in the source snapshot.

Use the documented `key=value` syntax without indentation or spaces around `=`.
Some malformed lines are silently skipped by the version 0.1 parser, so a
misspelled setting format can leave the default value in effect. Correctly
parsed unknown keys and invalid parameter values are rejected. See
[input/output conventions](usage.md).
