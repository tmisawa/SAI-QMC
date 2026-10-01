---
date: 2026-09-30
datetime: 2026-09-30 13:30 JST
model: OpenAI GPT-6 (Codex)
summary: |
  User guide for SAI-QMC 0.1, a finite-temperature determinant QMC code.
  Describes input syntax, output columns, build commands, validation, and development records.
---

# SAI-QMC

[English](README.md) | [日本語](README_ja.md)

SAI-QMC is a C implementation of finite-temperature determinant quantum Monte
Carlo (DQMC/BSS) for the repulsive, half-filled Hubbard model on bipartite lattices.
It measures energies, density, double occupancy, and equal-time longitudinal
and transverse spin structure factors. The command-line executable is `dqmc`.

Version **0.1** is based on the numerical implementation through August 22, 2026,
with subsequent input/output and documentation improvements recorded in [LOG.md](LOG.md).
The source includes the developments made after AIMHack2026;
[the hackathon scope](docs/hackathon-2026.md) identifies what was achieved during
June 24–26, 2026. AI-assisted development, reviews, failures, and corrections are
documented in the [development record](development-record/README.md).

## Build and run

Requirements: a C11 compiler, `make`, BLAS and LAPACK. macOS uses Accelerate;
on Linux the Makefile links `-llapack -lblas -lm` (for example, using LAPACK
and OpenBLAS packages). OpenMP and MPI are optional.

From the repository root:

```sh
make dqmc
./dqmc input/1d_L4_U0_spin_all.txt
```

This small noninteracting example automatically writes scalar observables to
`observables.dat` and
spin observables to `szz.dat`, `sperp.dat`, and `spin_consistency.dat`. For an
interacting temperature scan:

```sh
./dqmc input/1d_L4_U4.txt
```

Output files use the current working directory. A subsequent run can overwrite
them; use a separate working directory for each saved run. The examples in
`input/bench_2d_L*.txt` are larger profiling inputs, not quick smoke tests.

## Input format

Pass one text input file as the executable's argument. Each setting occupies
one line in `key=value` form; `#` starts a comment. Write keys at the start of
the line, without spaces around `=`, and keep comma-separated lists free of
spaces. The version 0.1 parser can silently skip some malformed lines.

For example, `input/1d_L4_U4.txt` contains:

```text
lattice=chain
Lx=4
pbc=1
t=-1.0
U=4.0
dtau=0.1
beta_list=0.5,1.0,2.0,4.0,6.0,8.0
nwarm=300
nmeas=3000
nbin=30
seed=1
```

`beta_list` specifies inverse temperatures (`T=1/beta`, with `k_B=1`).
`nwarm` and `nmeas` are warmup and measurement sweeps per replica, at each
temperature. `nbin` partitions the measurements for error estimation;
it must be at least 2, and `nmeas` must be divisible by `nbin`.
Each `beta/dtau` must be an integer to within `1e-9`.
The chemical potential is fixed to `mu=U/2`; there is no `mu` input key.

Use `lattice=square` with `Lx` and `Ly` for a rectangular square lattice;
`pbc=1` is periodic and `pbc=0` is open. With periodic boundaries, active
directions must have even length to retain bipartiteness. Omitted settings
use defaults documented in [the input/output reference](docs/usage.md).

## Model and observables

The simulated grand-canonical Hamiltonian is

```text
H = K_hop + U sum_i n_i_up n_i_down - mu N,  mu = U/2.
```

`U >= 0`; the lattice must be bipartite. Built-in chains and square lattices use
`t=-1` by default for nearest-neighbor hopping. A real symmetric hopping matrix with zero
diagonal may also be supplied using `lattice=file`; see
[input and observable conventions](docs/usage.md).

Scalar observables are saved to `observables.dat` by default and are also
written to standard output. Set `output_file=results.dat` to change the file,
or `output_file=none` for standard output only. The file starts with `#`
comment lines containing run metadata and column names, followed by one
whitespace-separated row per requested temperature. Scalar output has 14 columns:

```text
T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign  acceptance dAcceptance
```

| Columns | Meaning |
| --- | --- |
| `T` | Temperature, the inverse of the actual simulated `beta` |
| `E_hub`, `dE_hub` | Hubbard energy excluding the chemical-potential term, and its statistical error |
| `E_gc`, `dE_gc` | Grand-canonical energy including `-mu*N`, and its error |
| `E_ph`, `dE_ph` | Energy in the particle-hole-symmetric interaction convention, and its error |
| `ntot`, `dN` | Total particle number and its error |
| `doublon`, `dD` | Double occupancy per site and its error |
| `sign` | Mean Monte Carlo sign |
| `acceptance`, `dAcceptance` | Local-update acceptance fraction and its error |

Energies and `ntot` are totals. `doublon` is per site.
`E_hub=<K_hop+U sum n_up n_down>`, `E_gc=E_hub-mu*ntot`, and
`E_ph=E_hub-(U/2)*ntot+(U/4)*n_site`.
`acceptance` is the fraction of accepted local updates during measurement
sweeps; `dAcceptance` is its bin-jackknife error. These two diagnostic columns
follow the original 12 scalar columns; older reference data may omit them.

Optional spin measurements use `szz_q` and `sperp_q`, with `none`, `af`, `all`,
or a comma-separated momentum list. `Sperp=Sxx+Syy`. With matching ordered momentum
selections, `spin_consistency_file` reports the paired estimate
`DeltaSU2=Sperp/2-Szz` and its jackknife error.

Enable all three spin outputs by adding:

```text
szz_q=all
szz_file=szz.dat
sperp_q=all
sperp_file=sperp.dat
spin_consistency_file=spin_consistency.dat
```

Each spin data file contains `#` metadata comments and a `#` column header,
followed by space-separated rows, one per inverse temperature and selected
momentum. The last two columns contain the observable and its error.
Full schemas and momentum
conventions are in [the input/output reference](docs/usage.md).

| Output | Format and activation |
| --- | --- |
| `observables.dat` and standard output | The 14 scalar columns above; change the file with `output_file` |
| `hopping_used.txt` | Site count followed by the hopping matrix; written in the working directory |
| `szz.dat`, `sperp.dat`, `spin_consistency.dat` | Optional spin data files enabled by the settings above |
| `replicas.dat` | Replica seeds, run parameters, and status; enabled by default for `nrep>1`. Set `replica_log=none` to disable, or `replica_log=path.dat` to choose a file |
| `profile.dat` | Timing data when `profile=1`; change the path with `profile_file` |

All generated tables use space-separated fields and `#` comment headers.
Inputs and the hopping-matrix file retain their `.txt` format. Historical
reference data keep their original names and formats.

Error messages go to standard error. Run `./dqmc input.txt 2> run.err` to save
them separately; scalar data are saved automatically. Existing redirection
workflows remain available with `output_file=none`.

## Conditional local measurements

`conditional_measure=1` adds local HS-pair averages of double occupancy and
joint hopping/interaction energy to `replica_bin_file`. It preserves the
ordinary measurements for comparison and defaults to `0`. Measurements stay
with the temperature slot under PT. See [the estimator, output schema, and
independent-replica analysis](docs/conditional-measurements.md) for supported
models and statistical limits. Improved measurements do not establish mixing
or equilibration.

## Parallel execution

Replicas are independent Monte Carlo chains with deterministic seed assignment.
Select the execution mode in the input using `parallel` and `nrep`.

```sh
make dqmc_omp
OMP_NUM_THREADS=2 ./dqmc_omp input/1d_L4_U0_szz_all_omp.txt

make dqmc_mpi
mpirun -np 2 ./dqmc_mpi input/1d_L4_U0_szz_all_mpi.txt

make dqmc_hybrid
OMP_NUM_THREADS=2 mpirun -np 2 ./dqmc_hybrid input/1d_L4_U0_szz_all_hybrid.txt
```

On Apple Silicon with Homebrew, OpenMP defaults to
`LIBOMP_PREFIX=/opt/homebrew/opt/libomp`; override this Make variable for other
installations. Use an MPI C compiler wrapper compatible with the selected C
compiler. For the Open MPI/Apple Clang setup used in local validation,
`OMPI_CC=clang make dqmc_mpi dqmc_hybrid` selects Clang explicitly.

## Global HS-field update

Low-temperature, large-`U` runs updated only by local flips can leave a replica in
a long-lived state that resembles a nonzero total `S^z` sector. The optional site
world-line update proposes flipping the Hubbard-Stratonovich field of one site on
every time slice and accepts it by Metropolis with a stabilized determinant ratio.
With the default `global_site_select=fixed`, each pass tries every site in a fixed
order and rebuilds the Green functions.

| key | values | default | meaning |
| --- | --- | --- | --- |
| `global_update` | `none` / `site` | `none` | `site` enables the site world-line global update |
| `global_interval` | positive integer | 100 | sweeps between global passes, counted from the start of warmup |
| `replica_bin_file` | path | empty | write sign-weighted sums per replica and bin (TSV); independent of the global update |
| `global_site_diag_file` | path | empty | write a histogram of site-flip attempts and acceptances by polarization; requires `global_update=site` |
| `global_site_select` | `fixed` / `polarized` | `fixed` | how the site of each flip attempt is chosen; `polarized` draws it with weight `(p_i/p_0)^alpha + 1/n` |
| `global_site_power` | real number `>= 0` | 2 | the exponent `alpha` of the polarized weight; validated even when the selection is `fixed` |

```text
global_update=site
global_interval=10
replica_bin_file=bins.tsv
```

With `nwarm=7` and `global_interval=3`, cumulative sweeps 3 and 6 are warmup passes
and 9, 12, ... are measurement passes; the counter restarts for each `beta` but not
at bin boundaries. Disabled runs keep the default random stream, measurements, and
outputs. `global_interval` is validated even when `global_update=none`.

When enabled, the scalar output gains two columns, `global_acceptance` and
`global_attempts`: the acceptance ratio over all replicas during measurement
(`nan 0` when nothing was attempted). The cost per pass grows with the number of
sites, time slices, and stabilization blocks; `profile=1` reports it as the
`dqmc_global` region.

`replica_bin_file` writes all `beta` values to one file, ordered by beta index,
replica id, and bin id. `sweep_begin/end` number the measurement sweeps after
warmup (1-based, inclusive). The comment header records the lattice, conditions,
normalization, and column names. Observables follow from `sum_sign_O / sum_sign`;
`sum_sign_Ehub` is the total energy and `sum_sign_D` is per site. `Szz` uses
`S^z=(n_up-n_down)/2`, `Sperp` is `SxSx+SySy`, both normalized by `1/N`; `Q` is
`(pi,pi)` on the square lattice or `pi` on the chain, `0` is `q=0`, and unselected
momenta are `nan`. The per-replica SU(2) difference `3*Szz(Q)-1.5*Sperp(Q)` equals
`-3*DeltaSU2` of `spin_consistency_file`. A bin file that collides with another
enabled output is rejected before any file is written; a numerical failure drops
the rows of that `beta` while earlier `beta` rows are kept, and open, write, or
close failures end the run with a nonzero status.

`global_site_diag_file` records, per `beta` and replica, how many world-line
flips were attempted and accepted as a function of two per-site indicators.
The first is `p = |m_i|/L`, the polarization of the site's HS world line
(`m_i` is the sum of its field over time slices; for a site whose physical
spin is held fixed in the atomic limit, the mean of the signed `m_i/L` is
`±tanh(lambda)`, written in the header as a scale, not as the expected value
of `p` at finite `L`). The second is `d = -eps_i m_i sign(M)/L`, the mismatch
with the majority staggered pattern (`eps_i` is the sublattice sign,
`M = sum_j eps_j m_j`, and `sign(M) = +1` when `M = 0`). Each indicator has
50 bins; `p` uses width 0.02 on `[0,1]` and `d` uses width 0.04 on `[-1,1]`.
Only measurement sweeps are counted. The output contains 100 TSV rows per
`beta` and replica, and the diagnostic never changes the random stream or
other outputs. If the key is omitted, including when `global_update=site` is
used, no diagnostic is collected. It supports the design of weighted site
selection described below.

`global_site_select=polarized` replaces the fixed order of the site flips by a
weighted draw. At the start of each pass the polarization `p_i = |m_i|/L` of
every site is computed once and the site of each of the `n` attempts is drawn
with probability proportional to `w_i = (p_i/p_0)^alpha + 1/n`, where
`p_0 = tanh(lambda)` is the atomic-limit scale (`p_0 = 1` when `U = 0`) and
`alpha = global_site_power`. Flipping a site does not change any `|m_j|`, so
the weights are the same before and after every proposal and the acceptance is
the plain Metropolis ratio, with no Hastings correction. Each attempt uses two
random numbers (site, then acceptance) instead of one, so the random stream and
the results of a `polarized` run differ from a `fixed` run with the same seed;
`fixed` runs, with or without these keys, are unchanged. The same site can be
drawn more than once in a pass. `1/n` keeps every site proposable; the
exponent `alpha = 0` gives a uniform random site. When the selection is not
`fixed`, the stdout header gains ` global_site_select=<value>
global_site_power=<alpha>` and the diagnostic header names the selection.
Weights that are not usable end the replica as a numerical failure: a
non-finite weight, a cumulative sum whose increment is lost to rounding, a
relative weight below the conservative limit `2^-52`, or a site interval that
no 53-bit RNG value can reach after rounding the target multiplication. The
last check is explicit; the relative-weight limit alone does not guarantee
that every site remains selectable. These checks consume no random numbers.
Selection by the staggered mismatch `d` is not implemented: the Stage A
diagnostic rejected that indicator, and `global_site_select=staggered` is an
input error. In the L4, U8 comparison at beta 16/24, alpha 2 increased acceptance
but did not meet the predeclared mixing-improvement criteria; see
[VALIDATION.md §9](VALIDATION.md#9-stage-b-polarized-selection-versus-fixed-order2026-09-26).

Validation on the 4x2 cluster at `U/t=8` is recorded in [VALIDATION.md](VALIDATION.md)
and [docs/validation/global-hs-4x2-2026-09-21.json](docs/validation/global-hs-4x2-2026-09-21.json).
The acceptance rate of the site flip decreases rapidly at low temperature, so the
update does not guarantee mixing for arbitrary sizes and temperatures.

## Δτ-ladder parallel tempering

Runs that decorrelate slowly under local flips (and even the global site
update above) can use parallel tempering (PT) instead: a ladder of `nbeta`
slots, one per `beta_list` value, sharing one fixed number of time slices so
that each slot keeps its own time step `dtau_k = beta_k / tempering_ltr`,
with periodic proposals to exchange the Hubbard-Stratonovich configuration
of two neighboring slots.

| key | values | default | meaning |
| --- | --- | --- | --- |
| `tempering` | `none` / `dtau_ladder` | `none` | `dtau_ladder` enables the Δτ-ladder parallel-tempering mode |
| `tempering_ltr` | non-negative integer | `0` | time slices shared by every slot; required `> 0` with `tempering=dtau_ladder` (`dtau_k = beta_k / tempering_ltr`); must stay `0` with `tempering=none` |
| `tempering_interval` | positive integer | `1` | sweeps between exchange rounds, including during warmup; with exactly two slots only every second round has a pair to try, so the pair is attempted once per `2*tempering_interval` sweeps; validated even when `tempering=none`, where it has no effect |
| `tempering_file` | path | empty | optional TSV of per-ladder exchange statistics; only usable with `tempering=dtau_ladder`; PT runs without it when left empty |
| `field_init` | `random` / `uniform` | `random` | initial Hubbard-Stratonovich field family; usable with or without PT |

```text
tempering=dtau_ladder
tempering_ltr=200
tempering_interval=1
beta_list=4,5,6.666666666666667,10
tempering_file=pt.tsv
```

With `tempering=dtau_ladder`, `dtau` must not be given (each slot's own
`dtau_k = beta_k / tempering_ltr` is used instead), `beta_list` needs at
least two strictly increasing values, and `stab_drift_file`,
`udv_scale_file`, `udv_centered_file`, `global_site_diag_file`, and
`profile=1` are rejected. Every slot must already be a sign-free,
particle-hole-symmetric model (half filling, bipartite lattice);
`global_update=site` may be combined with PT.

An exchange attempt between neighboring slots `a` and `b` compares each
slot's own configuration against the other's: with
`log W_k(C) = 2 log|det(1+B^k_up(C))| - lambda_k * sum(s)` and
`lambda_k = acosh(exp(dtau_k * U / 2))`, the move accepts with probability
`min(1, exp(log R))` for
`log R = log W_a(C_b) + log W_b(C_a) - log W_a(C_a) - log W_b(C_b)`, drawing
exactly one number from a dedicated exchange random stream per attempt
(even when `log R >= 0`). Attempted neighbor pairs alternate between
`(0,1),(2,3),...` and `(1,2),(3,4),...` from one exchange round to the next,
including during warmup. With exactly two slots the `(1,2),...` rounds
contain no pair, so the single pair `(0,1)` is attempted only in every second
round, that is, once per `2*tempering_interval` sweeps.

A non-finite `log R`, a failed weight evaluation, or a failed configuration
rebuild after an accepted exchange ends the ladder as a numerical failure
(`tempering exchange failed`) rather than a silent rejection. A numerical
breakdown in a slot's own sweep or global update also ends the ladder, but
it is reported as that slot's `dqmc warmup numerical breakdown` or
`dqmc measurement numerical breakdown` line (with `slot=` and `ladder=`),
not as an exchange failure; no exchange round is attempted once a slot has
failed.

One failed ladder fails the whole run. The other ladders still run to
completion; then the process exits nonzero (on every MPI rank) and stderr
names each failed ladder (`tempering ladder r failed`). No observables are
written for any ladder: the scalar output (stdout or `output_file`) keeps
only its header lines, without temperature rows or the final
`solver_elapsed_seconds` line; `replica_bin_file` is left empty; and
`szz_file`, `sperp_file`, `spin_consistency_file`, and `replica_log` keep
only their headers. Only `tempering_file`, when set, is written in full,
with `failed=1` in each failed ladder's `ladder` row.

Slot `k` of ladder `r` seeds its own Monte Carlo chain with
`replica_seed(seed, k, r)`, the same rule an ordinary replica uses; the
dedicated exchange random stream instead uses `replica_seed(seed, nbeta, r)`
(index `nbeta` is never used as a slot index), and `tempering_file`'s
`ladder` row records that value as `swap_seed`.

Each ladder's slots and exchanges run, for the ladder's whole lifetime, on
one MPI rank and one OpenMP thread; ladders are distributed across
ranks/threads the same way ordinary replicas are. Given the same seed,
`serial`/`omp`/`mpi`/`hybrid` builds give identical results. Slot `k`'s
observables are written wherever a replica's would be, with `beta_index=k`
and `replica_id` set to the ladder id: the ladder, not the slot, is the
statistical unit. PT does not change the Trotter error — every slot keeps
its own `dtau_k`, and exchanging configurations does not mix statistical
error with time-step error.

Under PT, the first stdout line, and the `szz_file`/`sperp_file`/
`spin_consistency_file` headers, report `dtau=ladder` in place of a numeric
value, and the stdout line also gains
`tempering=dtau_ladder tempering_ltr=... tempering_interval=...` (plus
`tempering_file=...` when set). A `replica_bin_file` gains one extra header
line, `# tempering=dtau_ladder tempering_ltr=...`, because its usual single
`dtau=` field is unused; each row's own `Ltr` and `beta_effective` columns
give that row's `dtau_k = beta_effective / Ltr`. The scalar output's last
line is `# tempering solver_elapsed_seconds=... nranks=...`: the solver's
own wall time (from process start to just before closing outputs), not a
scheduler job time or a sum over ladders.

`tempering_file` is a self-documenting TSV (a `# tempering=...` header, one
`# slot=k beta=... dtau=... lambda=...` line per slot, and a `# columns:`
legend), with rows of five kinds:

| kind | meaning |
| --- | --- |
| `pair` | attempts/accepted exchanges for one neighboring pair, per bin (`bin=-1` is the warmup total) |
| `slot` | per bin, per slot: which walker occupies that slot at the bin's end, and, summed over the bin's samples, how many times the slot's occupant had last visited the hot (slot 0) versus the cold (last slot) end |
| `walker` | per walker: completed hot->cold->hot round trips within the measurement window, and the walker's final slot |
| `ladder` | one row per ladder: the exchange random stream's seed, and whether the ladder failed |
| `cost` | four worker-second timings per ladder (warmup, measurement sweeps, measurement exchanges, measurement observable calls) |

`cost` rows report one ladder's own worker time, not job wall time or
node-hours: with several ladders running at once, their `cost` rows must
not simply be added together to estimate the wall time actually spent. A
round trip counts a walker only once it completes hot slot -> coldest slot
-> hot slot again, entirely inside the current measurement window.

`field_init=uniform` sets every Hubbard-Stratonovich field to `+1` after the
usual per-site random draws (which are made and then discarded, so the
random stream is identical to `field_init=random`); it works with or
without PT and only changes the starting configuration.

The all-`+1` field is also the configuration of largest numerical scale.
For it, the up-spin product `B_{L-1}...B_0` has largest scale about
`exp(Ltr*lambda + beta*w)`, with `lambda = acosh(exp(dtau*U/2))` and `w` the
largest eigenvalue of the hopping matrix (`4|t|` on the periodic square
lattice, `2|t|` on the periodic chain). Once this exponent passes the
double-precision limit `ln(DBL_MAX) ≈ 709.78` by a margin of order one (in
the 4x4 example below the run still started at 710.8 and failed from 711.2
on), the run fails at initialization, before any sweep:
`udv_lmul_work non-finite matrix at stage=qr_raw`, then `dqmc_init failed`,
and a nonzero exit. For example, on the periodic 4x4 square lattice with
`U=8` and `dtau=0.0125`, `beta=24` (`Ltr=1920`) gives an exponent of 708.2,
about 1.6 e-folds below the limit, and starts normally, while `beta=24.5`,
`25`, and `26` at the same `dtau` fail at initialization. Under PT the
coldest slot (largest `beta_k` and `dtau_k`) sets the bound. Inputs are not
checked against this bound in advance; the failure is immediate and
explicit. `field_init=random` starts far below this scale.

Validation of the PT implementation — cross-weight checks, an
exact-enumeration sampling test, an exact-enumeration regression of the
integrated ladder driver with the production option sets, byte-identity
against the pre-PT baseline, `serial`/`omp`/`mpi`/`hybrid` agreement,
failure-path and failure-message tests, and a finite-size correctness check
against independent chains and exact diagonalization — is recorded in
[VALIDATION.md](VALIDATION.md). PT does
not distribute one ladder's slots across MPI ranks, has no
feedback-optimized placement of the `beta_list` temperatures, and does not
support the diagnostic files or profiler listed above; read
[known limitations](docs/limitations.md) before production use.

## Validation and limits

The CLI tests and conditional-bin analysis require Python 3.10 or newer.
The optional independent Fock-space reference generator also uses NumPy and SciPy.
If `python3` names an older interpreter, select one explicitly with, for
example, `make PYTHON=python3.12 test`; the conditional CLI test uses that same
interpreter for its analysis subprocesses.

```sh
make test
make test_omp
OMPI_CC=clang make test_mpi
OMPI_CC=clang make test_hybrid
make test_slow
python3 scripts/verify_reference_data.py
```

The `OMPI_CC` override is specific to Open MPI. Omit it or configure `MPICC`
as appropriate for a different MPI implementation.

Tests cover Green-function updates and stabilization, independent noninteracting
references, spin sum rules, particle-hole symmetry, output compatibility,
replica aggregation, and parallel consistency. Slow tests are separate.

Read [known limitations](docs/limitations.md) before production use. In particular:

- Finite time steps introduce Trotter bias; statistical error bars alone do not
  include it. Compare grand-canonical ensembles and extrapolate in `dtau^2`.
- Low-temperature stability is limited. `green_rebuild=centered` is an explicit
  option with a measured, condition-dependent range; the default is `combine`.
- Spin error bars can underestimate independent-run variability. Long runs,
  independent seeds, and convergence checks are necessary. Some recorded
  low-temperature spin data failed convergence checks and are identified as such.
- The 120-replica polarized-selection comparison failed its mixing-improvement
  criterion at L4, U8, beta 24; increased acceptance does not establish equilibration
  ([validation results](VALIDATION.md#9-stage-b-polarized-selection-versus-fixed-order2026-09-26)).

[Reference data](data/README.md) include finite-temperature ED comparisons and
Trotter extrapolation, with checksums and provenance in
[PROVENANCE.md](PROVENANCE.md). Historical records do not imply validation of
every parameter combination supported by the input parser.

## Citation and license

Please cite SAI-QMC if you use this software. Citation metadata for version 0.1
are in [CITATION.cff](CITATION.cff). The algorithmic references are in
[REFERENCES.md](REFERENCES.md), including Yuichi Otsuka's doctoral thesis,
Appendix A. The thesis and third-party article PDFs/OCR are not distributed here.

SAI-QMC is licensed under [MIT](LICENSE). Third-party attributions and
external-library conditions are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Planned improvements are listed in [TODO.md](TODO.md).
