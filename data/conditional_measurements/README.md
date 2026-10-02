# Independent conditional-measurement references

`exact_fock.py` enumerates all auxiliary-field configurations of two- and
four-site systems, reconstructs both spin Green functions from explicit matrix
products, and compares the locally conditioned energy with the two explicitly
flipped configurations. It also constructs the full Fock-space finite-time-step
transfer matrix independently of the DQMC solver.

Run from the repository root with Python 3.10 or newer, NumPy, and SciPy:

```sh
OPENBLAS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 python3 data/conditional_measurements/exact_fock.py
```

The script writes `fock-reference.json` beside itself. The retained reference
contains three cases, each with 256 HS configurations. Hopping is `-1`, `U=8`,
and the chemical potential is `U/2`. The two-site case has one bond; the
four-site case is a periodic ring. Energy is the Hubbard energy excluding the
chemical-potential term, divided by the site count; D is double occupancy per
site. The Fock calculation uses the same finite time step, without extrapolation.

This checks the local formulas and their equilibrium expectations, including
the reference constants used by `tests/test_conditional_measure.c`. Its variance
comparison concerns a single conditioned observable under the exact ensemble;
it does not establish integrated sampling error or efficiency per unit time.
See [the measurement guide](../../docs/conditional-measurements.md) for the
production option, bin schema, and statistical limitations.


## First independent low-temperature pilot

The first comparison completed on 2026-09-30: a 4x4 periodic square lattice,
U=8, beta=16, dtau=0.025, alternating sweeps, centered Green rebuild, stab=4,
and a global site pass every 10 sweeps. Eight replicas use base seed 2026093001,
10,000 warmup and 10,000 measurement sweeps each, with 20 bins per replica.
The initial field uses the default random initialization. All 160 bins are retained.

[Input](pilot/fixed-b16-dt025/input.in), [bins](pilot/fixed-b16-dt025/bins.tsv),
and [independent-replica analysis](pilot/fixed-b16-dt025/analysis.json) are provided.
Run the program from a new output directory; the binary must include the
conditional-measurement implementation. Reproduce the analysis with:

```sh
python3 scripts/analyze_conditional_bins.py data/conditional_measurements/pilot/fixed-b16-dt025/bins.tsv --expect-replicas 8 --expect-slots 1
```

| Observable | Ordinary mean +/- SE | Conditional mean +/- SE | Ordinary SE / conditional SE |
| --- | ---: | ---: | ---: |
| D | 0.05325563 +/- 0.00010675 | 0.05315417 +/- 0.00003043 | 3.508 |
| E/N | -0.53144395 +/- 0.00114178 | -0.53182272 +/- 0.00034353 | 3.324 |

The paired conditional-minus-ordinary differences are -0.91 SE for D and
-0.36 SE for E/N. These sample errors use eight independent replicas; they do
not establish equilibration or confidence-interval coverage. Finer-time-step,
PT, and measurement-cost comparisons remain in progress at this checkpoint.
No cost-normalized improvement is inferred from this first result.


## Second independent fixed-beta pilot

The beta=16, dtau=0.0125 comparison also completed, with base seed 2026093002
and otherwise the same physical parameters and run lengths as the first case.
All 160 bins from eight replicas are retained. The D error decreased from
0.00011899 to 0.00002368 (ratio 5.024); the E/N error decreased from
0.00124814 to 0.00046805 (ratio 2.667). Paired differences are -1.50 SE and
+0.70 SE, respectively. See its [input](pilot/fixed-b16-dt0125/input.in),
[bins](pilot/fixed-b16-dt0125/bins.tsv), and
[analysis](pilot/fixed-b16-dt0125/analysis.json) for all means and replica values.
PT and the paired timing comparisons remain in progress at this checkpoint.
The means at different time steps must not be pooled as the same finite-time-step ensemble.


## Completed three-condition comparison

The subsequent comparison includes the two fixed-beta runs and all six PT
slots, each with eight independent replicas/ladders. The PT slots share ladders
and must not be pooled as independent observations. All bins are retained.
[Summary](summary.json), [tables](tables.md), [paired timings](timings.json),
and [numerical provenance](provenance.json) contain the complete results.
Each case directory under `pilot/` contains its input, bins, and analysis.

The retained fixed-beta analysis JSON files were generated with Python 3.12.5,
and the retained PT analysis JSON was generated with Python 3.10.14. Re-running
the standard-library analysis on another supported Python version can change
the last few printed bits of some sample standard errors (observed maximum:
2 ULP between Python 3.10.14 and 3.12.5); means, per-replica values, and the
scientific interpretation are unchanged. The checksums identify the retained
artifacts exactly.

The measurement-cost ratios use three short serial ON/OFF pairs for each
condition, with matching seeds and alternating execution order. The fixed-beta
pilots, PT pilot, and timing pairs ran on three different platforms; numerical
source hashes match, while compiler/library versions and binaries differ. The unchanged
ordinary bin columns and PT records were checked within every pair. Warmup is
reported separately. Fixed-beta timings use `profile=1` and nonoverlapping
profiler regions; PT uses its built-in cost counters. Fixed-beta ratios can
change when profiling is disabled, and the measurements do not promise the
same ratios on other hardware. Combining these ratios with pilot SEs gives a descriptive
variance-times-cost proxy across platforms, not a measured same-machine
efficiency gain or a proof of asymptotic efficiency. The
reported leave-one-replica-out ranges are sensitivity checks, not confidence
intervals. Initialization dependence, equilibration, and interval coverage
remain separate scientific questions.
