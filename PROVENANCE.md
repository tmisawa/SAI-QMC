---
date: 2026-09-23
datetime: 2026-09-23 15:21 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Source lineage and numerical data provenance for the SAI-QMC distribution.
  Describes the packaged implementation and scientific development records for version 0.1.
---

# Initial transfer provenance

The initial workspace was assembled on September 8, 2026 from AF_QMC, while
preserving the original private project. Its Git history was not imported.
Version 0.1 retains the numerical algorithms and reference data from this
snapshot. Subsequent distribution changes, including automatic scalar-file
output and uniform space-separated `.dat` tables, are recorded in [LOG.md](LOG.md).

## Implementation

- Source checkout: `463dc75be5327f636b2683f58f10343393fb7e17`.
- Source repository's latest main at transfer:
  `ca6fd6cf562b4c6197ad238b341607f506ec6f5c`.
- All 77 files in `src/`, `tests/`, `input/`, and `Makefile` were initially
  copied byte-for-byte from the latest main tree. During publication preparation,
  the two `input/bench_2d_L*.txt` profiler output paths were made relative to the
  run directory. The other 75 files were byte-identical at the initial import.
  This baseline is the August 22 implementation, including Szz, Sperp, and
  paired SU(2) consistency output; later changes are documented separately.
- The older AF_QMC name remains in internal C macros, historical paths, and
  recorded commands. The distributed project name is SAI-QMC and the executable
  remains `dqmc`.

These source revision identifiers document lineage; the source repository
itself is private. The distributed file hashes are independently inspectable
in [provenance/files.tsv](provenance/files.tsv).

## Records and data

The published Japanese main log contains 232 entries through August 22, 2026.
One July entry was recovered separately
from source revision `3177dfe55c02dbc3e1c0821161f2f4ff3735f3ba`, for 233 entries
in total. Of these, 57 fall within the June 24–26, 2026 hackathon analysis window.
See the [English record guide](development-record/README.md).

Dated design, review, implementation, and numerical reports retain their
historical content, with omissions of private correspondence, manuscript
administration, and unnecessary personal environment information.
New distribution guidance is separate from the archived reports.

The two small [validation datasets](data/README.md) retain their historical
numerical values. The L=4,6,8 FullDiag ED reference curves were supplied by the
maintainer; their original file hashes are recorded in the manifest. Private
storage paths remain in the maintainer's source archive. The analysis uses
the bundled `reference/` directory instead of a private absolute path.
Recent spin-analysis tables retain both successful and failed convergence checks.
None of these data are presented as new runs of the current source snapshot.

## Manifest and exclusions

The manifest covers the 253 imported files retained for distribution, recording
source-relative paths or source labels that omit private storage names,
original and exported SHA-256 hashes, sizes, and whether bytes changed during
preparation. It describes this source snapshot and does not track future development
edits. Newly written English/Japanese guides, citation/license material, and checking
scripts are documented in [LOG.md](LOG.md).
[data/SHA256SUMS](data/SHA256SUMS) additionally checks the distributed numerical
data, inputs, and spin-analysis tables from the repository root.

The source `.git` history, thesis and paper PDF/OCR/TeX, private manuscript,
compiled executables, full raw production runs, scheduler/account configuration,
and administrative records are outside this distribution.
