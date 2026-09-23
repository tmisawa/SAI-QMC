---
date: 2026-09-23
datetime: 2026-09-23 15:34 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Contributor instructions for SAI-QMC, the canonical repository for ongoing development.
  Preserve physical conventions, provenance, development records, and explicit release decisions.
---

# SAI-QMC contributor instructions

SAI-QMC implements finite-temperature DQMC for the repulsive half-filled
Hubbard model on bipartite lattices. Read README.md and docs/limitations.md.

- Use this repository for ongoing software development, fixes, and tests.
  Keep publishable specifications, design explanations, and scientific validation
  alongside the corresponding implementation.
- Keep internal design drafts, implementation plans, and unpublished research
  ideas in the enclosing workspace's internal documentation area. Reflect the
  publishable technical content needed to understand and maintain changes here.
- Matrices are column-major: `A[i+j*n]`. BLAS/LAPACK use Fortran symbols.
- Green functions use `g_ij=<c_i c_j^dagger>`; density matrices use
  `<c_i^dagger c_j>=delta_ij-g_ji`.
- The interaction convention is `U n_up n_down`, with `mu=U/2`.
  Align ensemble, chemical potential, energy shifts, and normalization when
  comparing reference calculations.
- Keep scalar/input compatibility unless an intentional change is documented.
  Do not assign a release version without an explicit release decision.
- Run tests appropriate to the change. See the Makefile's serial, OpenMP,
  MPI, hybrid, and slow targets; hybrid includes cross-mode output tests.
- Preserve original observations and failures in imported development records.
  Add dated corrections rather than silently rewriting scientific history.
- Software development belongs in LOG.md with actual JST date/time, model
  attribution, and a short summary. Record incomplete release work in TODO.md.
- Keep publication-preparation reviews, transfer audits, privacy checks, their
  results, and administrative logs outside this repository. Use the enclosing
  workspace's internal documentation area; do not reproduce them in public logs
  or manifests. Scientific validation reports remain part of this repository.
- Public records cover the full development period; the hackathon analysis
  window is June 24–26, 2026. Retain Japanese records and provide English guidance.
- Keep third-party papers, thesis PDF/OCR, credentials, and personal host/account
  configuration out of the distribution. Document actual source/notice provenance.
- Exclude private correspondence and summaries, third-party contact/consent records,
  manuscript submission arrangements, and unrelated private project identifiers.
  Check follow-up logs and reviews too, so removed details are not reintroduced
  through summaries, links, manifests, or audit reports. Keep detailed edits privately.
  Formal scholarly citations and required upstream author/license notices remain.
- Do not commit, push, publish, or change repository visibility unless requested.
  Do not submit HPC jobs without explicit resource/condition approval.
- This repository is standalone. Do not rely on private sibling projects to
  build, test, or run the shipped examples.
