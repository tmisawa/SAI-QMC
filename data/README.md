---
date: 2026-10-03
datetime: 2026-10-03 11:17 JST
model: OpenAI GPT-6 (Codex); OpenAI GPT-5.6 Sol (Codex; 2026-10-02 validation); OpenAI GPT-6 (Codex; 2026-10-03 metadata update)
summary: |
  Places the fixed-temperature spin residuals in the known sampling context motivating PT.
  Includes a matched 36-run P/P control; spin residuals occur in both boundaries.
  Adds the exploratory 4x4 AP/P U=4 ground-state ED comparison; spin agreement remains unresolved.
  Includes the preregistered 4x2 antiperiodic finite-temperature ED comparison dataset.
  Small numerical reference datasets shipped with SAI-QMC.
  Describes historical provenance, reproducibility, and the limits of fixed-step comparisons.
---

# Validation data

Most of these are historical numerical results used during development; they
are not new calculations made with the current source snapshot. The
exceptions are `tempering_L6_U4_ed_20260927/`, `antiperiodic_4x2_U4_ed/`,
`antiperiodic_4x4_U4_ed/`, and `periodic_4x4_U4_control/`,
which were computed with this repository's own source (see each directory's
README for the exact commit and binary checksums).

| Directory | Contents |
| --- | --- |
| `benchmark_L468_U4_full_diag_20260626/` | 39 finite-temperature comparison points: periodic L=4,6,8 Hubbard chains, U=4, mu=2, dtau=0.05; inputs, QMC output, replica/profiler records, figures, analysis, ED curves |
| `L6_U4_mu2_dtau_ed_comparison/` | L=6, U=4 energy/doublon Trotter extrapolation tables and dtau=0.2/0.1/0.05 QMC output |
| `tempering_L6_U4_ed_20260927/` | Periodic L=6 Hubbard chain, U=4, half filling: `tempering=dtau_ladder` PT (4 slots, `tempering_ltr=200`) vs. 4 independent (non-PT) chains at the same `(beta, dtau)`, with finite-T ED reported for context; inputs, QMC/PT output, analysis script and results |
| `antiperiodic_4x2_U4_ed/` | 4x2 square lattice, x antiperiodic / y periodic, U=4, beta=2,4: 96 independent single-series runs at dtau=0.1/0.05/0.025 versus finite-T ED (AP/P and P/P); pre-registered seeds, inputs, outputs, ED, analysis and verdict |
| [`antiperiodic_4x4_U4_ed/`](antiperiodic_4x4_U4_ed/README.md) | Exploratory 4x4 AP/P, U=4, beta=4/8/16, three time steps, four seeds per point, versus archived ground-state ED; energy/doublon agree at beta=16 after extrapolation, spin convergence unresolved |
| [`periodic_4x4_U4_control/`](periodic_4x4_U4_control/README.md) | Matched 36-run P/P control of the 4x4 AP/P study, same seeds and settings except bc_x; paired boundary comparison and uncertainty diagnostics |

The 4x4 AP/P and P/P spin residuals should be read alongside the existing
[low-temperature spin-sampling limitations](../docs/limitations.md#spin-statistics).
Apparent SU(2) violations in finite samples are a known convergence concern,
and improving sampling between slowly mixing sectors motivates the optional
parallel-tempering (PT) implementation (see [VALIDATION.md](../VALIDATION.md),
section 10). The model retains SU(2) symmetry. All 72 runs in this matched
comparison use `tempering=none`; they do not test PT's effectiveness for
these conditions. The numerical residuals and convergence limits remain
reported in full in both datasets.

The ED curves under `benchmark_L468_U4_full_diag_20260626/reference/` originate
from FullDiag numerical reference data supplied by the maintainer. They are data, not a bundled
ED implementation. Columns are `T, E_gc, C, N, Sz, S2, D_total, Z`; the comparison
uses `E_hub=E_gc+mu*N` and `doublon=D_total/L`. The initial transfer records
source and destination SHA-256 hashes in [the provenance manifest](../provenance/files.tsv).

The benchmark analysis now reads the bundled `reference/` directory. To reproduce
its tables and figures, install the optional Python dependencies from
`requirements-analysis.txt` and run:

```sh
python3 data/benchmark_L468_U4_full_diag_20260626/analyze_full_diag_comparison.py
```

This command regenerates the benchmark's CSV/TSV and two PNG figures. Use a copy
of the dataset if you want to retain the byte-exact historical plots. Different
plotting-library versions may change figure bytes.

To verify the shipped numerical data without optional Python packages:

```sh
python3 scripts/verify_reference_data.py
```

Fixed-step deviations from ED include Trotter bias. The energy tables contain
total energies, not energies per site unless the column name says so. Compare
the same ensemble and normalization; see [VALIDATION.md](../VALIDATION.md).

Recent spin-convergence summaries are retained at their original relative paths
under [jobs](../jobs/), so references from the development log remain recognizable.
Both passing and failed checks are included. Full raw HPC runs and submission
configuration are not shipped. The latest beta=8,12,16,32 U=4 result is absent
because it had not been recovered in the available source record.
