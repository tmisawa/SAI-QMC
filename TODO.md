---
date: 2026-09-30
datetime: 2026-09-30 13:47 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Scientific and software follow-up beyond the version 0.1 implementation.
  Passing implementation tests do not resolve the recorded production convergence limits.
---

# Planned improvements

- Evaluate conditional D/K/E measurement efficiency, independent-series error
  stability, initial-state dependence, and finite-time-step effects before
  production adoption. Validate spin alternatives separately; the new option
  leaves ordinary spin measurements unchanged. See
  [conditional measurements](docs/conditional-measurements.md).

- Automate build and test checks in continuous integration for macOS and Linux.
- Improve input-parser diagnostics so malformed setting lines are consistently
  rejected; document any compatibility changes.
- Extend condition-specific low-temperature stability and spin-convergence studies.
- Sign reweighting (2026-10-27): evaluate equilibration and autocorrelation where the mean
  sign is small, without parallel tempering; extend ED and reference comparisons beyond the
  4-site chain and the 4x2 tprime lattice; decide whether hopping files read without `mu` or
  `sign_file` should also use the exact particle-hole test (a compatibility change for files
  with elements below `1e-12`, which can currently be treated as symmetric); and,
  if replica-bin re-analysis of every momentum is needed, add all-q raw sums to the output.

Build and test commands are in [README.md](README.md) and
[README_ja.md](README_ja.md).

Scientific follow-up, including the unrecovered U=4 spin ladder and unresolved
U=12 low-temperature convergence, is described in [the limitations](docs/limitations.md).
