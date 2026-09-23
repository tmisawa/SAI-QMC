---
date: 2026-09-08
datetime: 2026-09-08 11:37 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Small numerical reference datasets shipped with SAI-QMC.
  Describes historical provenance, reproducibility, and the limits of fixed-step comparisons.
---

# Validation data

These are historical numerical results used during development. They are not
new calculations made with the current source snapshot.

| Directory | Contents |
| --- | --- |
| `benchmark_L468_U4_full_diag_20260626/` | 39 finite-temperature comparison points: periodic L=4,6,8 Hubbard chains, U=4, mu=2, dtau=0.05; inputs, QMC output, replica/profiler records, figures, analysis, ED curves |
| `L6_U4_mu2_dtau_ed_comparison/` | L=6, U=4 energy/doublon Trotter extrapolation tables and dtau=0.2/0.1/0.05 QMC output |

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
