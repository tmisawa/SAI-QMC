---
date: 2026-09-23
datetime: 2026-09-23 15:27 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Input/output reference for SAI-QMC 0.1, including defaults and file schemas.
  Documents scalar and spin conventions, output paths, and optional diagnostics.
---

# Input/output reference

[English](usage.md) | [日本語](usage_ja.md)

Run `./dqmc input.txt 2> run.err`. The input is a text file. Scalar observables
are saved automatically to `observables.dat` and also written to standard
output; diagnostics go to standard error.
Input and output paths are relative to the current working directory, not the
input file's directory. Use separate working directories for saved runs;
output files are overwritten on subsequent runs with the same paths.

## Input syntax and defaults

Write one `key=value` setting per line. Blank lines and `#` comments are allowed.
Start keys at the beginning of the line and do not insert spaces around `=`
or within values, including comma-separated lists and paths. Do not quote values.
In version 0.1, some malformed lines are silently skipped, potentially leaving
the default value in effect. Successfully parsed unknown keys, invalid values,
and invalid combinations are errors. See [the examples](../input/) and
[`src/io.c`](../src/io.c) for the parser and its validation rules.

| Key | Default | Meaning / values |
| --- | --- | --- |
| `lattice` | `chain` | `chain`, `square`, or `file` |
| `Lx`, `Ly` | `4`, `1` | Built-in lattice lengths; positive integers |
| `pbc` | `1` | `1`: periodic; `0`: open |
| `bc` | Same as `pbc` | Alias: `periodic` / `pbc` / `1`, or `open` / `obc` / `0` |
| `latfile` | Unset | Hopping file, required for `lattice=file` |
| `t`, `U` | `-1.0`, `4.0` | Built-in hopping amplitude and repulsive interaction (`U>=0`) |
| `dtau` | `0.1` | Positive imaginary-time step |
| `beta_list` | `2.0` | Up to 64 positive inverse temperatures, comma-separated |
| `nwarm`, `nmeas`, `nbin` | `200`, `2000`, `20` | Warmup sweeps, measurement sweeps, and measurement bins, per replica and temperature |
| `stab` | `8` | Positive stabilization interval; alias `stabilize_interval` |
| `green_rebuild` | `combine` | `combine`, `two_sided`, or `centered`; see [stability limits](limitations.md) |
| `sweep_order` | `forward` | `forward` or `alternating` |
| `parallel` | `serial` | `serial`, `omp`, `mpi`, or `hybrid`; requires a compatible executable |
| `nrep` | `1` | Number of independent replicas; positive integer |
| `seed` | `12345` | Unsigned 64-bit base seed |
| `output_file` | `observables.dat` | Scalar data file; `none` disables this file and keeps standard output only |
| `profile` | `0` | Enable timing output with `1` |
| `profile_file` | `profile.dat` when enabled | Timing data path |
| `replica_log` | `replicas.dat` if `nrep>1`; otherwise off | Explicit path enables the log even for one replica; `none` disables it |
| `szz_q`, `sperp_q` | `none` | Spin momentum selectors: `none`, `af`, `all`, or `mx:my,...` |
| `szz_file`, `sperp_file` | `szz.dat`, `sperp.dat` | Paths for enabled spin measurements |
| `spin_consistency_file` | `none` | Path for paired `Sperp/2-Szz`; requires both selectors with the same ordered momenta |

The chemical potential is fixed to `mu=U/2`; there is no `mu` setting.
`nwarm>=0`, `nbin>=2`, `nmeas>=nbin`, and `nmeas` must be divisible by `nbin`.
The reported means and errors combine the `nrep*nbin` measurement bins.
The number of replicas is a separate setting from MPI ranks and OpenMP threads.

Each `beta/dtau` must lie within `1e-9` of an integer; otherwise the run fails.
The actual inverse temperature is the integer slice count times `dtau`.
Spin data files distinguish `beta_requested` from actual `beta`; use the actual
inverse temperature when comparing results. Temperature is `T=1/beta`, with
`k_B=1`; for the default hopping, the energy unit is `abs(t)=1`.

## Geometry and hopping files

Use `lattice=chain` with `Lx`, or `lattice=square` with `Lx` and `Ly`.
For periodic built-in lattices, each length greater than one must be even
to preserve bipartiteness. Open boundaries also support odd lengths.

For `lattice=file`, the first value in `latfile` is the number of sites,
followed by the real symmetric hopping matrix in row order, separated by
whitespace. Its diagonal must be zero. The matrix entries directly specify
hopping amplitudes and are not multiplied by `t`. For example, a two-site
hopping matrix is:

```text
2
0 -1
-1 0
```

Do not include comment lines in a hopping file. Non-bipartite hopping graphs
are rejected. `hopping_used.txt` records the constructed matrix in this same
format. Spin momentum selectors require built-in chain or square geometry;
they do not support `lattice=file`.

## Scalar output

`observables.dat` and standard output contain the same data: `#` metadata and
column comments followed by one whitespace-separated row per requested
temperature. Set `output_file=results.dat` to change the destination. Data are
flushed after each temperature; a failed run may leave an incomplete file.
Opening or writing the scalar output unsuccessfully causes a nonzero exit status.
MPI/hybrid runs write the aggregate file on rank zero only.

For existing scripts that save standard output, use `output_file=none` and
`./dqmc input.txt > out.dat`. In MPI/hybrid runs, avoid redirecting the launcher
output to the same file as `output_file`; launchers may add or relay output
through separate streams. There are 14 scalar columns:

```text
T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign acceptance dAcceptance
```

The [English README](../README.md#model-and-observables) and
[Japanese README](../README_ja.md#模型と観測量) describe each column.
Energies and `ntot` are totals for the lattice; `doublon` is per site.
Errors are estimated with bin jackknife and do not include finite-time-step
bias or guarantee convergence. The three energy conventions are
`E_hub=<K_hop+U sum n_up n_down>`, `E_gc=E_hub-(U/2)*ntot`, and
`E_ph=E_gc+(U/4)*n_site`.

The scalar file aggregates replicas; it does not contain individual measurements
or per-bin samples. The last two columns, `acceptance` and `dAcceptance`, were
added after the original 12-column format. Older reference files may omit them.

## Spin output

Select momenta separately with `szz_q` and `sperp_q`:

- `none`: disable measurement; assigning a file path alone does not enable it.
- `af`: the antiferromagnetic momentum (`pi` along each active direction);
  active lengths must be even.
- `all`: all discrete momenta of the built-in lattice.
- `mx:my,...`: explicit integer indices, for example `szz_q=0:0,2:0` for an
  `Lx=4` chain. Indices satisfy `0<=mx<Lx`, `0<=my<Ly`, with `my=0` for chains.
  Repeated momenta are rejected.

Momenta are `qx=2*pi*mx/Lx`, `qy=2*pi*my/Ly`. For open boundaries, these are
Fourier sampling points, not translation-symmetry quantum numbers.
To disable a channel, set its selector to `none`; `szz_file=none` or
`sperp_file=none` is rejected when that channel is enabled.
Use distinct paths for all outputs.

Each spin file has `#` metadata comments and a `#` column header, then one
space-separated row per requested inverse temperature and selected momentum.
The Szz column header is:

```text
# beta_requested beta T q_index mx my qx_over_pi qy_over_pi qx_folded_over_pi qy_folded_over_pi Szz dSzz
```

The Sperp schema replaces the last two columns with `Sperp dSperp`; the paired-consistency schema
uses `DeltaSU2 dDeltaSU2`. `q_index` is a zero-based index in the selected
momentum order. Raw momenta lie in `[0,2*pi)` and folded momenta in `(-pi,pi]`;
all four momentum-value columns are divided by `pi`.

`Szz(q)=(1/n_site) sum_ij exp[i*q*(r_i-r_j)] <Sz_i Sz_j>`, where
`Sz_i=(n_i_up-n_i_down)/2`. It includes the full correlation without subtracting
the product of one-point means and without a factor of three.
`Sperp=Sxx+Syy=(S+-+S-+)/2` uses the same site normalization.
The momentum sums of these structure factors differ from their per-site values.

At ensemble level, the present SU(2)-invariant model obeys `Sperp=2*Szz` at any
time step; an individual auxiliary-field configuration need not.
`DeltaSU2=Sperp/2-Szz` uses paired measurement bins and their jackknife error.
It is a sampling/implementation diagnostic, not a direct estimator of Trotter bias.

## Replica, timing, and stabilization diagnostics

`replica_log` is a space-separated data file containing run metadata, not
replica-resolved observables. Its column header is
`# beta T replica_id seed nwarm nmeas nbin status`; MPI/hybrid execution adds
`rank`. There is one row per inverse temperature and replica.

With `profile=1`, the timing data file has the column header
`# beta T dtau Ltr phase region calls total_sec avg_sec frac_beta wall_sec thread_total_sec nrep parallel`.
MPI-enabled builds add `nranks`. Rows describe measured regions of the computation;
`Ltr` is the number of imaginary-time slices.

Three additional path settings enable space-separated stabilization-diagnostic
data files (use names such as `drift.dat`, `scales.dat`, and `centered.dat`):

| Key (default: unset) | Content |
| --- | --- |
| `stab_drift_file` | Drift between updated and rebuilt Green functions |
| `udv_scale_file` | Logarithmic scales of left/right UDV factors |
| `udv_centered_file` | Centered-UDV scales, offsets, and remaining numerical margin |

These settings support `parallel=serial` only; `udv_centered_file` also requires
`green_rebuild=centered`. Their aliases are `stabilization_drift_file`,
`udv_scale_diagnostics_file`, and `udv_centered_diagnostics_file`, respectively.
Each file starts with a `#` column header. Leave the setting absent
to disable it; the string `none` is not a disable value for these three paths.

## File formats and older data

New output tables consistently use `.dat` filenames, space-separated fields,
and `#` comments, including the column header. Column order, normalization,
and the number of printed digits are unchanged. Parameter input files and
the site-count-plus-matrix hopping format keep `.txt` filenames.

This changes the earlier development interface: spin/stabilization output used
TSV, while replica/profiler output used CSV, with uncommented column headers.
Readers for new files should ignore `#` lines and split data rows on whitespace.
Explicit filenames are used literally, so an old `.tsv` or `.csv` suffix does
not restore the earlier delimiter or header format.

Bundled historical data, their analysis scripts, and dated development records
retain their original names and formats. Their CSV/TSV readers still apply to
those archived files. The dated spin specifications below describe the earlier
interface; use this page for the current file format and defaults.

Detailed scientific specifications:

- [Szz input and output](2026-08-21-szz-structure-factor-usage.md)
- [Sperp input, output, and statistical interpretation](2026-08-22-sperp-structure-factor-usage.md)
- [Energy and ED comparison conventions](../VALIDATION.md)
