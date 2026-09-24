---
date: 2026-09-23
datetime: 2026-09-23 16:17 JST
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
Each pass tries every site in a fixed order and rebuilds the Green functions.

| key | values | default | meaning |
| --- | --- | --- | --- |
| `global_update` | `none` / `site` | `none` | `site` enables the site world-line global update |
| `global_interval` | positive integer | 100 | sweeps between global passes, counted from the start of warmup |
| `replica_bin_file` | path | empty | write sign-weighted sums per replica and bin (TSV); independent of the global update |

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

Validation on the 4x2 cluster at `U/t=8` is recorded in [VALIDATION.md](VALIDATION.md)
and [docs/validation/global-hs-4x2-2026-09-21.json](docs/validation/global-hs-4x2-2026-09-21.json).
The acceptance rate of the site flip decreases rapidly at low temperature, so the
update does not guarantee mixing for arbitrary sizes and temperatures.

## Validation and limits

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
