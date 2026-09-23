# LOG

---
date: 2026-09-23
datetime: 2026-09-23 16:17 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Simplified the citation request to ask users to cite SAI-QMC.
  Aligned the citation message and English/Japanese README guidance.
---

## 2026-09-23: Simplified citation guidance

- Use "Please cite SAI-QMC if you use this software" in CITATION.cff and
  align the English and Japanese README citation requests.
- Retain software version metadata and the separate algorithmic reference list.

---
date: 2026-09-08
datetime: 2026-09-08 13:47 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Documented the initial SAI-QMC software package and its runnable examples.
  Source, numerical reference data, development records, and citation material are available.
---

## 2026-09-08: Initial software package

- Included the August 22 DQMC implementation with Szz, Sperp, and paired SU(2)
  consistency output; see [source provenance](PROVENANCE.md).
- Added English build/run guidance, input conventions, known limitations,
  algorithmic references, MIT license text, and CITATION metadata.
- Bundled the ED comparison and Trotter extrapolation tables. The benchmark
  analysis reads the bundled references; a standard-library Python script checks
  the numerical tables and checksums.
- Benchmark inputs write profiler output relative to their run directory.
- Historical scientific development is documented in [development-record/](development-record/README.md).
- The first release version remains unassigned.

---
date: 2026-09-23
datetime: 2026-09-23 15:01 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Added Japanese user guidance and documented the version 0.1 input/output interface.
  Clarified defaults, observable normalization, file schemas, and parser limitations.
---

## 2026-09-23: Version 0.1 documentation

- Added [README_ja.md](README_ja.md), a Japanese translation of the English
  build/run guide, model conventions, observables, and applicability limits.
- Expanded the [input/output reference](docs/usage.md) with default values,
  hopping-file syntax, scalar and spin schemas, and optional diagnostic outputs.
- Documented malformed-input handling and open-boundary spin momentum conventions.
- Recorded version `0.1` in the citation metadata and current user guides.
  The implementation, examples, numerical reference data, and archived scientific
  records are unchanged from the initial software package.

---
date: 2026-09-23
datetime: 2026-09-23 15:14 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Added automatic scalar-observable files while preserving the historical stdout format.
  Documented output settings and checked file handling across all execution modes.
---

## 2026-09-23: Automatic scalar data files

- Basic observables now default to `observables.dat`, with the same metadata,
  14 columns, and numerical formatting as standard output. `output_file` selects
  another destination; `output_file=none` restores stdout-only behavior.
- Serial/OpenMP write one aggregate file; MPI/hybrid write it on rank zero.
  Flush data after each temperature and report open/write/close failures.
- Reject scalar destinations that overlap inputs or enabled outputs before
  creating files. Recognize existing file identities and aliases through
  existing parent directories. Direct serial/OpenMP stdout redirection to the
  scalar file uses one stream.
- Update English/Japanese guides, defaults, and the file-format descriptions.
  Correct the guide to state that enabled spin channels reject `*_file=none`;
  their `*_q=none` setting disables measurement.
- `make test`, `test_scalar_parallel`, `test_szz_parallel`, and
  `test_sperp_parallel` passed on macOS, with `OMPI_CC=clang` for MPI builds.
  The scalar tests cover default/custom/disabled output, two temperatures,
  collisions, and open failures. README runs retain byte-identical scalar and
  spin results; a forced regular-file write failure exits with an error.
- Numerical algorithms, input examples, and reference data are unchanged.

---
date: 2026-09-23
datetime: 2026-09-23 15:22 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Standardized generated output tables on .dat files with space-separated fields.
  Updated examples, headers, readers in tests, and user guidance while retaining historical data.
---

## 2026-09-23: Uniform .dat output tables

- Spin outputs now use `szz.dat`, `sperp.dat`, and `spin_consistency.dat` in
  the examples; default replica and timing files are `replicas.dat` and
  `profile.dat`. Stabilization diagnostics use the same table convention.
- All generated tables use spaces between fields and `#` column headers.
  Preserve column order, normalization, printed digits, and the existing scalar
  standard-output columns. Parameter inputs and hopping matrices retain `.txt`.
- This intentionally replaces the earlier TSV/CSV output interface. Readers
  should skip `#` lines and split rows on whitespace. Explicit filenames remain
  literal; choosing an old suffix does not restore the old delimiter.
- Update runnable inputs, format-dependent test readers, and both user guides.
  Historical datasets, their analysis scripts, and dated records retain the
  original filenames and formats.
- On macOS, `make test` and the scalar/spin/dat parallel-output targets passed.
  Before/after comparisons in serial, OpenMP, MPI, and hybrid execution preserve
  all observable, replica, and stabilization fields. Profiler schema, counts,
  and metadata agree; elapsed times vary between runs.

---
date: 2026-09-23
datetime: 2026-09-23 15:27 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Added a Japanese translation of the current input/output reference.
  Connected the language guides and checked keys, examples, formulas, and links.
---

## 2026-09-23: Japanese input/output reference

- Added [docs/usage_ja.md](docs/usage_ja.md), covering input defaults, geometry,
  scalar/spin output, diagnostics, and the transition to space-separated .dat tables.
- Added language links in both references and linked the Japanese README to
  the Japanese reference. Numerical code and output formats are unchanged.
