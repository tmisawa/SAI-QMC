---
date: 2026-09-08
datetime: 2026-09-08 11:46 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Distinguishes the AIMHack2026 analysis window from subsequent SAI-QMC development.
  Links dated records, numerical references, and the archived achievement report.
---

# AIMHack2026 and subsequent development

The hackathon took place on **June 24–26, 2026 (JST)**. The development log
contains 57 entries in this window, dated June 25 (46) and June 26 (11).
The manuscript analyzes this window. The distributed software and development
records also include work after the hackathon.

| Capability | During the hackathon | After the hackathon |
| --- | --- | --- |
| Core DQMC | C implementation, UDV stabilization, rank-1 updates, Green wrapping | UDV stack optimization, delayed updates, particle-hole symmetry, alternating sweeps, centered rebuild |
| Observables | Energy conventions, density, doublon, sign | Szz, Sperp, paired SU(2) checks |
| Execution | Serial replicas, OpenMP, MPI | Hybrid execution and further HPC validation |
| Validation | U=0, ED/FullDiag, Trotter extrapolation, parallel integrity | Low-temperature limits, spin estimators, independent-run error checks |

The dated [achievement report](2026-06-26-dqmc-hackathon-achievements.md) describes
the original endpoint. Source revision `1d99362` is the final local commit dated
June 26 (presentation materials); the source lineage is recorded in
[PROVENANCE.md](../PROVENANCE.md). This distribution's code is from the later
August 22 implementation, not a reconstruction of the June executable.

The original model labels, including their inconsistencies, remain in the
[Japanese logs](../development-record/logs/af-qmc.md). The historical family
aggregation for this window is 44 Codex entries and 13 Claude Opus entries.
The public aggregation script retains raw labels, and reports the hackathon
window separately from later records. These are attributed work summaries,
not a complete transcript of conversations or tool activity.

```sh
python3 development-record/aggregate_log_stats.py --period hackathon
python3 development-record/aggregate_log_stats.py --period all
```
