---
date: 2026-06-26
datetime: 2026-06-26 10:02 JST
model: Codex (GPT-5)
summary: |
  DQMC benchmark against FullDiag references for 1D Hubbard chains.
  Conditions: U/t=4, mu=U/2, periodic chain, L=4,6,8, dtau=0.05.
  Energy and doublon temperature dependence were compared with exact diagonalization.
---

# L=4,6,8 U=4 FullDiag Benchmark

Reference:

- `reference/L{L}_U4_FullDiag.dat`
- FullDiag columns are `T, E_gc, C, N, Sz, S2, D_total, Z`.
- The comparison converts `E_hub = E_gc + mu * N` with `mu=U/2=2`.
- The doublon comparison uses `D_total / L`.

DQMC conditions:

| L | dtau | nrep | nwarm | nmeas | nbin | MPI ranks |
|---:|---:|---:|---:|---:|---:|---:|
| 4 | 0.05 | 8 | 1000 | 10000 | 100 | 8 |
| 6 | 0.05 | 8 | 1000 | 10000 | 100 | 8 |
| 8 | 0.05 | 8 | 500 | 5000 | 100 | 8 |

Temperature grid:

- `beta = 0.5, 0.6, 0.8, 1.0, 1.2, 1.6, 2.0, 2.4, 3.2, 4.0, 4.8, 6.4, 8.0`
- `T = 2.0 ... 0.125`

Runtime:

| L | real_sec |
|---:|---:|
| 4 | 315.40 |
| 6 | 526.07 |
| 8 | 359.63 |

Residual summary:

| L | max abs diff E_hub/L | max abs diff doublon |
|---:|---:|---:|
| 4 | 0.006336 | 0.001269 |
| 6 | 0.005809 | 0.001467 |
| 8 | 0.006306 | 0.000857 |

Files:

- `input_L*.in`: DQMC inputs.
- `out_L*.dat`: raw DQMC output.
- `profile_L*.csv`: profiler output.
- `replicas_L*.csv`: MPI replica assignment logs.
- `time_L*.txt`: `/usr/bin/time -p` runtime.
- `comparison_qmc_vs_fulldiag.csv`: merged comparison table.
- `comparison_qmc_vs_fulldiag.tsv`: tab-separated comparison table.
- `qmc_vs_fulldiag_energy_doublon.png`: ED curves and DQMC error bars.
- `qmc_vs_fulldiag_residuals.png`: DQMC-ED residuals.
- `analyze_full_diag_comparison.py`: analysis script.

Interpretation:

- DQMC follows the ED temperature dependence for both energy and doublon.
- At fixed `dtau=0.05`, residual `E_hub/L` remains at the `O(10^-3)` to `O(10^-2)` level.
- Doublon agrees at the `O(10^-3)` level.
- A production-quality comparison should add `dtau^2 -> 0` extrapolation for L=4,6,8.
