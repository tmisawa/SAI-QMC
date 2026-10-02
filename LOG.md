# LOG

---
date: 2026-10-03
datetime: 2026-10-03 08:43 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Updated the metadata of seven user and validation documents to include the
  directional-boundary additions and their contributors. Document bodies are unchanged.
---

## 2026-10-03: Directional-boundary documentation metadata

Updated document dates, model attribution and summaries for the directional-boundary
usage guides, validation scope and reference-data index. The dates record this metadata
update; the contributor entries identify the additions made on 2026-10-02.

---
date: 2026-10-02
datetime: 2026-10-02 22:33 JST
model: OpenAI GPT-5.6 Sol (Codex)
summary: |
  Added bc_x/bc_y (periodic, antiperiodic, open) for built-in lattices. Antiperiodic
  directions keep coordinates, so spin momentum selectors work. Legacy inputs are byte-identical.
---

## 2026-10-02: Directional boundaries and antiperiodic spin structure factors

Built-in chains and square lattices accept `bc_x` and `bc_y`. An antiperiodic direction
adds its closing bond with amplitude `-t`; its length must be even and at least 4. The
keys cannot be combined with `pbc`/`bc`. Output metadata keeps `pbc=0|1` for the legacy
combinations and otherwise records the resolved values, so antiperiodic runs are not
labelled as periodic.

Validation covers U=0 exact momentum formulas, identity with the same matrix given as a
hopping file, byte identity with a614a8f for legacy inputs, identical serial/OpenMP/MPI/hybrid
output, and a pre-registered 4x2 finite-temperature ED comparison (verdict PASS,
VALIDATION.md section 12). Larger antiperiodic lattices are not validated here.

---
date: 2026-10-01
datetime: 2026-10-01 16:48 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Final integration review kept conditional_measure disabled by default.
  Made the Python 3.10+ test interpreter explicit and inherited by child analysis.
---

## 2026-10-01: Conditional-measurement final integration review fix

The numerical implementation and default-off behavior were unchanged. The
conditional CLI target now accepts the conventional `PYTHON` Make variable,
and its analysis subprocess uses `sys.executable`. This prevents a supported
outer interpreter from accidentally launching an older `python3` from `PATH`.
The English and Japanese validation instructions document the override.

The final review rechecked default-off byte compatibility, all build modes and
the slow suite, exact references, retained-data checksums, sanitizers, public
provenance, and the absence of private paths or account identifiers. The
option remains `conditional_measure=0` by default; no production values were
adopted by this software-integration decision.

---
date: 2026-09-30
datetime: 2026-09-30 13:47 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Added optional conditional local D and synchronized K/E measurements.
  Preserves ordinary outputs and update trajectories; validates formulas,
  MPI transfer, bin analysis, and serial/OpenMP/MPI/hybrid compatibility.
---

## 2026-09-30: Conditional local measurements for D and energy

`conditional_measure=1` adds four columns to the replica-bin output, retaining
ordinary scalar, spin, and bin measurements. The default remains off. At each
local proposal the helper reads the effective delayed Green and averages the
matching site's observable over its two HS values without drawing randomness
or modifying the update state. K is measured at the same stages as D, so energy
retains their covariance. Local measurements stay at the temperature slot
through global passes and PT exchanges. Invalid values, signs, bounds, or
measurement counts fail the run instead of discarding samples.

The implementation includes MPI packing and an optional standard-library
analysis script that uses independent replicas/ladders, retains all bins, and
rejects incomplete data. The [measurement guide](docs/conditional-measurements.md)
defines the formulas, units, supported scope, and statistical limitations.
Tests compare all 256 small-system HS fields with direct two-spin matrices,
check Fock-space reference values, preserve field/RNG/Green trajectories, and
verify slot ownership and CLI compatibility across all four parallel builds.

Initial cross-compiler comparisons exposed last-bit spin differences between
Clang and GCC. The existing byte-equality regressions pass with the documented
matching MPI compiler setting; their criteria were retained. New cross-mode
checks use numerical tolerance for floats and exact comparison for integer
identities. An initial CLI-test loop variable collision was also corrected.

All serial, OpenMP, MPI, hybrid, and slow suites passed. The slow suite also
retains the existing 800-ladder exact-driver regression.

Scientific validation and follow-up are recorded in [VALIDATION.md](VALIDATION.md)
and [TODO.md](TODO.md). The option does not establish low-temperature
equilibration or replace the ordinary spin measurements.

---
date: 2026-09-30
datetime: 2026-09-30 09:50 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Combined the Stage B scientific validation record with the PT documentation.
  Stage B retains section 9; PT validation is section 10. Numerical source is unchanged.
---

## 2026-09-30: Combine Stage B and PT validation records

The 120-replica Stage B results and their JSON record are now included alongside
PT validation. Stage B did not meet its mixing-improvement criterion. This result
does not change the implementation tests or establish equilibration of PT runs.
The original Stage B record is retained below with its original date.

---
date: 2026-09-28
datetime: 2026-09-28 20:05 JST
model: Claude Sonnet 5 (Claude Code)
summary: |
  Fixed the CHECK_CLOSE test helper: it used to silently pass whenever an
  operand was NaN or Inf-minus-Inf, because a comparison against NaN is
  always false. The close-comparison predicate now lives in a small
  test_close() helper that requires both operands and the tolerance to be
  finite (and the tolerance non-negative); CHECK_CLOSE uses it and prints
  an explicit non-finite message when it rejects. Added a self-test with
  the non-finite/negative-tolerance negative controls and re-ran every
  test suite.
---

## 2026-09-28: CHECK_CLOSE now rejects non-finite operands and tolerances

- `tests/test_util.h`: `CHECK_CLOSE(a, b, tol)` failed only when
  `fabs(a - b) > tol`. Any comparison against NaN is false, and
  `INFINITY - INFINITY` is NaN, so `CHECK_CLOSE(NAN, 1.0, 1e-12)` and
  `CHECK_CLOSE(INFINITY, INFINITY, 1e-12)` both passed silently. This file
  was added in the repository's first commit ("Initial import of SAI-QMC
  0.1") and had not been touched since (`git log --follow -- tests/test_util.h`
  shows one commit; `git diff` against that commit is empty up to the
  parent of this change). Added
  `static inline int test_close(double a, double b, double tol)`, returning
  1 only when `a`, `b`, and `tol` are all finite, `tol >= 0`, and
  `fabs(a - b) <= tol`; `CHECK_CLOSE` now calls it and prints an explicit
  "non-finite" message for the rejected non-finite/negative-tolerance
  cases, keeping the previous `|a - b| = ... > tol` message for ordinary
  finite failures.
- New `tests/test_check_close.c` (picked up by the Makefile's `tests/test_*.c`
  wildcard) calls `test_close()` directly, not through `CHECK_CLOSE`'s own
  failing path, and checks that `(NAN, 1, 1e-12)`, `(1, NAN, 1e-12)`,
  `(INFINITY, INFINITY, 1e-12)`, `(-INFINITY, -INFINITY, 1e-12)`,
  `(1, 1, NAN)`, and `(1, 1, -1)` all return 0, and that `(1, 1, 0)` and
  `(1, 1 + 1e-13, 1e-12)` return 1. Before the header change, this file did
  not build (`test_close` undeclared); after it, it builds and passes.
- `tests/test_udv_logdet.c:136`: reworded the comment on the explicit
  `isfinite(la) && isfinite(lr)` check, which is now redundant with
  `CHECK_CLOSE` below it (kept for a clearer, standalone signal); the old
  comment ("CHECK_CLOSE does not catch NaN") is no longer accurate.
- Test-only change; nothing under `src/` touched. Checked (`grep`) the
  roughly 230 other `CHECK_CLOSE` call sites across the test suite for one
  that compares an intentionally infinite value on purpose; found none.
- Verification, all suites re-run at the new commit: `make test` rc=0,
  `ALL TESTS PASSED`; `OMPI_CC=cc OMP_NUM_THREADS=2 make test_omp test_mpi
  test_hybrid` rc=0, `ALL OMP TESTS PASSED` / `ALL MPI TESTS PASSED` /
  `ALL HYBRID TESTS PASSED`; `make test_slow` rc=0, `ALL SLOW TESTS PASSED`;
  `make test_tempering_default` rc=0, OK (byte-identical against `3215eee`,
  confirming no production behavior changed). No existing test failed from
  a newly-caught non-finite operand, so no latent defect surfaced.
  `make test_global_default` still cannot run here (baseline `463dc75`
  absent), unrelated to this change, as recorded on 2026-09-27.

---
date: 2026-09-28
datetime: 2026-09-28 01:48 JST
model: Claude Opus 5.5 (Claude Code)
summary: |
  Follow-up fixes for the dtau-ladder parallel tempering: a slot whose own
  sweep or global pass failed is now reported as that slot's numerical
  breakdown instead of as an exchange failure; a hook-based
  failure-message test and a slow exact-enumeration regression of the
  integrated ladder driver under the production option sets were added;
  the field_init=uniform double-precision bound, the whole-run failure
  semantics, and the two-slot exchange interval are documented; identifiers
  not defined in this repository were replaced by self-contained wording.
---

## 2026-09-28: Tempering follow-up fixes (failure reporting, driver regression, documented limits)

- `src/tempering_run.c`: before every exchange round the ladder driver
  checks all slots. A slot whose own sweep or global pass has failed is
  reported with the per-slot `dqmc warmup numerical breakdown` or
  `dqmc measurement numerical breakdown` line (slot, ladder, beta, dtau,
  sweep_count, status, failure_reason), and the round is not attempted.
  Before, the round's pre-flight status check reported it as
  `tempering exchange failed (... slot_status=1,0)`, one sweep late when
  the failed slot was not in the round. Failures of the round itself with
  every slot intact are still exchange failures. Successful runs are
  unchanged (`make test_tempering_default` OK).
- New `tests/test_tempering_failure_messages.sh` (`make test`) injects a
  measurement-phase global-pass failure, a warmup slot failure, and a
  genuine exchange failure through hook-build-only environment variables,
  and checks each message, the nonzero exit, header-only scalar output, an
  empty bin file, and `failed=1` in `tempering_file`; production binaries
  ignore the hooks. Before the fix the first two cases failed.
- New `tests/test_tempering_driver_exact_slow.c` (`make test_slow`) runs
  `dqmc_run_ladder` for three option sets (forward without global update;
  alternating with site global update every 3 sweeps and `stab=2 < Ltr`;
  alternating with polarized global update every 2 sweeps and
  `tempering_interval=3`), 800 ladders each, against exact finite-`dtau`
  enumeration: all 18 E/D comparisons within `|z| < 4` (max 1.89), about
  1 min per case.
- Documented in README.md, README_ja.md, docs/limitations.md, and
  VALIDATION.md: the `field_init=uniform` scale `exp(Ltr*lambda + beta*w)`
  and its double-precision failure at initialization (U=8, dtau=0.0125,
  4x4: beta=24 starts with 1.6 e-folds of margin; beta=24.5, 25, 26 fail
  in `dqmc_init`; measured boundary between Ltr=1927 and 1928); that one
  failed ladder fails the whole run and leaves only `tempering_file`; and
  that two slots attempt their pair only every second round. VALIDATION.md
  also corrects the sampling-test size (4000 ladders x 150 cycles, not
  "200 trial").
- Comments, the 2026-09-27 LOG entry, and the L6 dataset README now state
  the rules and work items themselves instead of identifiers that are not
  defined in this repository; object files are unchanged.
- Verification: `make test` rc=0, `make test_tempering_default` OK,
  `make test_slow` rc=0,
  `OMPI_CC=cc OMP_NUM_THREADS=2 make test_mpi test_hybrid` rc=0, and
  `python3 scripts/verify_reference_data.py` PASS.
  `make test_global_default` still cannot run here (baseline `463dc75`
  absent), as recorded on 2026-09-27.

---
date: 2026-09-27
datetime: 2026-09-27 22:47 JST
model: |
  Claude Opus 5.5 (Claude Code; design, coordination, and implementation of
  the ReplicaChain extraction, the ladder driver, and the main.c
  integration with tempering_file); Claude Sonnet 5 (implementation of the
  cross-weight and field replacement, the ladder exchange round with its
  exact-enumeration sampling test, the input keys, field_init, the L6
  ED/independent-chain validation, and the documentation); Claude Haiku 4.5
  (implementation of the exchange ratio, acceptance rule, pair schedule, and
  walker statistics); Claude Sonnet 5 and Claude Opus 5.5 (reviews)
summary: |
  Documented the new tempering=dtau_ladder parallel-tempering (PT) feature:
  input keys and usage in README.md/README_ja.md, PT-specific limitations
  in docs/limitations.md, and the implementation-correctness validation
  (cross-weight check, exact-enumeration sampling test with a negative
  control, byte identity against the pre-PT baseline, serial/omp/mpi/hybrid
  agreement, MPI failure-path tests, and an L6 chain U=4
  PT-vs-independent-chain screen) in a new VALIDATION.md section. Also adds
  the corresponding dataset to data/README.md. No source code changed.
---

## 2026-09-27: Document the dtau-ladder parallel tempering (PT)

- Added the `tempering`, `tempering_ltr`, `tempering_interval`,
  `tempering_file`, and `field_init` input keys to the English and Japanese
  reference tables, plus a new PT usage section describing the exchange
  rule, the output conventions (`beta_index`/`replica_id` mapping, the
  `dtau=ladder` headers, the `replica_bin_file` PT header line, the
  `tempering_file` row kinds, and the `solver_elapsed_seconds` line), and
  its caveats.
- Added a "Parallel tempering" section to `docs/limitations.md`: PT is
  validated only for sign-free, particle-hole-symmetric half-filled
  bipartite models; one ladder always runs on a single MPI rank/OpenMP
  thread for its whole lifetime (no rank-distributed slots); the
  diagnostic files and the profiler rejected together with PT have no
  PT-specific meaning; there is no feedback-optimized placement of the
  `beta_list` temperatures.
- Added VALIDATION.md Sec. 9, recording: the cross-weight check against an
  independent log-determinant implementation; the exact-enumeration
  sampling test (p=0.3503/0.9012/0.1786) and its negative control
  (p~1.9e-6/7.1e-62/3.4e-123, rc=1); byte identity of every
  `tempering=none`/unspecified run against the pre-PT baseline commit
  `3215eee` across 12 variants and 2 fixtures; `serial`/`omp`/`mpi`/`hybrid`
  agreement and the 4 MPI failure-path cases; and an `L=6` chain, `U=4`
  PT-vs-independent-chain screen (16/16 `|z|<3`, max `|z|=1.808`), with the
  reference `dtau^2` check (8/8 rows within 3 SE) reported as a consistency
  reference, not as proof of a Trotter-error origin.
- Recorded, as a dated fact, that the older `make test_global_default`
  regression cannot run in this repository because its baseline commit
  `463dc75` is absent here; this is pre-existing and unrelated to this
  change. The new `3215eee`-based `make test_tempering_default` is the
  test that actually covers the byte-identity guarantee for this change.
- Added the `tempering_L6_U4_ed_20260927/` dataset to `data/README.md` and
  corrected its "not new calculations" sentence: unlike the two pre-existing
  historical datasets, this one was computed with this branch's own source.
- Model-attribution correction: the Git commit trailers of the first three
  tempering commits (the cross-weight and field replacement; the exchange
  ratio, acceptance rule, pair schedule, and walker statistics; and the
  ladder exchange round with its exact-enumeration sampling test) name
  Claude Opus 5.5, but their actual implementers were Claude Sonnet 5,
  Claude Haiku 4.5, and Claude Sonnet 5, respectively, as listed in this
  entry's `model` field above. This LOG entry is the accurate record of who
  did the work; the commit history itself is not being rewritten.

---
date: 2026-09-26
datetime: 2026-09-26 19:50 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Recorded the 120-replica comparison of polarized alpha=2 and fixed-order selection.
  Acceptance increased, but the predeclared mixing-improvement verdict is fail.
  Added conditions, numerical results, provenance and limits to VALIDATION section 9 and its JSON record.
---

## 2026-09-26: Record the Stage B polarized-selection comparison

- All three L4, U8 runs completed with 120 replicas each. Result checksums,
  replica completeness, seeds and diagnostic counts passed validation.
- At beta 24 and interval 10, accepted flips increased from 63 to 367, but
  adjacent-bin sector switches were 61 versus 60 and replicas visiting both
  sides were 30 versus 34, below the required 122 switches and 60 replicas.
- Criterion 1 failed; criteria 2 and 3 were undetermined because the interval-100
  run missed the precision gate; criteria 4, 5 and 6 passed. Acceptance gains
  alone do not establish better mixing or scientific adoption.
- Independent sector recounts and a paired energy calculation matched the
  frozen analysis. Historical CSV profile data required a delimiter-only
  conversion with exact field preservation and recorded checksums.
- Added [VALIDATION section 9](VALIDATION.md), the
  [machine-readable record](docs/validation/global-site-select-2026-09-26.json),
  and README guidance. Numerical source is unchanged from `3215eee`.


---
date: 2026-09-26
datetime: 2026-09-26 16:16 JST
model: OpenAI gpt-5.6-sol (Codex)
summary: |
  Clarified that fixed site order describes the default global-site selection mode.
  The polarized mode continues to make n weighted attempts and may repeat a site.
  Numerical source and C tests are unchanged.
---

## 2026-09-26: Clarify the default fixed-order site pass

- Qualified the introductory English and Japanese descriptions of the global
  update so fixed ordering applies specifically to `global_site_select=fixed`.
- The later polarized-mode description remains authoritative for weighted draws,
  repeated sites, and the exact `n` attempts per pass.
- Expected numerical-error messages in successful C test logs come from covered
  negative tests. Their stderr behavior is retained; no C source or test changed.

---
date: 2026-09-26
datetime: 2026-09-26 16:06 JST
model: |
  OpenAI GPT-5.6 Luna (implementation and tests); OpenAI GPT-5.6 Sol
  and OpenAI GPT-5.6 Terra (reviews); OpenAI GPT-6 (coordination)
summary: |
  Added the opt-in polarized site selection (Stage B): weights (p_i/p_0)^alpha + 1/n,
  two draws per attempt, no Hastings factor. Fixed-order runs stay byte-identical.
  The 120-replica comparison against the fixed baseline is pending.
---

## 2026-09-26: Polarized site selection (Stage B)

- New keys `global_site_select` (`fixed`/`polarized`) and `global_site_power`; `staggered` is rejected as not implemented.
- `dqmc_global_site_pass` computes the cumulative weights once per pass and draws site then acceptance; the fixed path is unchanged.
- Tests: weight/selection unit tests including the dynamic-range rule, a dense 256-configuration oracle for the polarized kernel (detailed balance and stationarity), a replay test of the pass, explicit-fixed and diagnostic-only byte identity, header rule, unusable-weight failure, cross-mode agreement (serial/OpenMP/MPI/hybrid).

---
date: 2026-09-26
datetime: 2026-09-26 12:58 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Record the completed 120-replica Stage A diagnostic: p is supported and d rejected.
  Count gates and all four test suites passed; weighted-selection effects remain untested.
---

## 2026-09-26: Record the 120-replica diagnostic result

- Added conditions, checksums, fixed-rule bootstrap results, and a machine-readable
  scientific record to [VALIDATION.md §8](VALIDATION.md).
- Observed 63 accepted flips in 31 replicas. The corrected ratios are 145.8561
  for p (95% interval 99.2728–196.8793) and 0.43405 for d (0.26251–0.67121).
- All 12,000 measurement bins match the same-seed diagnostic-free baseline byte
  for byte. This verifies saved aggregates, not complete proposal histories.
- Numerical code is unchanged. Stage B implementation and mixing-effect tests
  remain future work.

---
date: 2026-09-26
datetime: 2026-09-26 11:55 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Record the Stage A 4x4 diagnostic pilot in VALIDATION.md. Execution and
  integrity checks passed; both indicators remain undetermined because the
  predeclared acceptance-count gates were not met.
---

## 2026-09-26: Record the Stage A diagnostic pilot result

- Added the 12-replica conditions, source and artifact checksums, count checks,
  fixed quartile rules, and numerical results to [VALIDATION.md](VALIDATION.md).
- The pilot produced 8 accepted flips in 4 replicas, below the required 30
  accepts and 8 accepting replicas. No bootstrap draws were executed, and
  Stage B remains pending further diagnostic evidence.
- Clarified that quartile ranges include empty histogram bins. Numerical code
  and pilot data are unchanged; this update documents the completed run.

---
date: 2026-09-25
datetime: 2026-09-25 22:38 JST
model: |
  OpenAI GPT-5.6 Luna (implementation and documentation); OpenAI GPT-5.6 Sol
  (implementation); OpenAI GPT-6 (coordination)
summary: |
  Documented the Stage A global site-flip diagnostic and its measurement-only
  histogram semantics. Runs without the diagnostic key keep their existing
  behavior; the Stage B weighted-site-selection hypothesis test remains pending.
---

## 2026-09-25: Document the global site-flip diagnostic

- Added the `global_site_diag_file` input key to the English and Japanese
  reference tables and described the per-beta, per-replica `p` and `d`
  histograms, including their binning and the `sign(M) = +1` convention at
  `M = 0`.
- Clarified that the atomic-limit signed mean `±tanh(lambda)` is a header scale,
  not the finite-`L` expectation of `p`, and that measurement-only diagnostics
  leave the random stream and existing outputs unchanged when the key is absent.
- Existing serial, OpenMP, MPI, and hybrid suites pass. The retained serial and
  empty-rank MPI smokes produce the expected 600-row and 400-row diagnostic
  files with matching attempt totals. Stage B remains pending the hypothesis
  test.

---
date: 2026-09-25
datetime: 2026-09-25 20:23 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Resolve dangling output symlinks before checking for file collisions.
  Prevent replica-bin and scalar output streams from overwriting another
  enabled output when the shared target file has not been created yet.
---

## 2026-09-25: Detect output collisions through dangling symlinks

- Follow relative and absolute symlink targets before resolving the parent of
  a new output file. Relative targets use the link's own directory; link chains
  are bounded to prevent infinite recursion, and long targets are read without
  truncation. A link to a distinct new destination remains usable.
- Add replica-bin regressions for relative, absolute, chained, and reverse
  aliases. Collisions must fail before changing inputs or creating outputs.
  Add a scalar-output collision regression and unit checks for long targets,
  distinct destinations, and cycles.
- `make test` passes, as do `test_global_output`, `test_global_output_omp`,
  `test_global_parallel`, `test_scalar`, and `test_scalar_parallel` with Open MPI
  and two OpenMP threads. The path unit test also passes ASan/UBSan.
- Numerical kernels are unchanged. Slow scientific regressions were not rerun.

---
date: 2026-09-24
datetime: 2026-09-24 11:08 JST
model: Claude Fable 5.1 (Claude Code)
summary: |
  Fixed two review findings on the replica-bin output before merging PR #1:
  the bin file could overwrite the input or an alias of another output, and
  lattices with a direction of length 1 lost the measured staggered momentum.
---

## 2026-09-24: Review fixes for the replica-bin output (PR #1)

- The bin-file collision check now protects the run-time input file and
  `latfile`, and compares paths by file identity (`output_paths_equal`: same
  string, same inode, or same resolved path) instead of by string only.
  `replica_bin_file=input.in` or `./szz.dat` against `szz_file=szz.dat` are
  rejected before any file is written.
- The staggered momentum recorded in the bin file follows the `af` selector:
  a direction of length 1 carries momentum 0, so `square Lx=4 Ly=1` keeps
  `szz_Q_index`/`sperp_Q_index` and finite `Szz(Q)`/`Sperp(Q)` sums.
- Regression checks added to `tests/test_global_output.sh`: input-file and
  `./`-alias and hard-link collisions, and the 4x1 `af` case.
- `make test` and `OMPI_CC=cc OMP_NUM_THREADS=2 make test_omp test_mpi test_hybrid` pass.

---
date: 2026-09-24
datetime: 2026-09-24 10:41 JST
model: Claude Fable 5.1 (Claude Code)
summary: |
  Released version 0.1 on 2026-09-24 as tag v0.1 and a GitHub release of the
  public main branch, which has been public since 2026-09-23.
---

## 2026-09-24: Version 0.1 release

- Version 0.1 is the `main` state made public on 2026-09-23 (the numerical
  implementation through August 22, 2026, with the input/output and
  documentation improvements recorded above).
- Release date: 2026-09-24 (tag `v0.1` and the GitHub release).
  `date-released` in [CITATION.cff](CITATION.cff) is 2026-09-24. A DOI is not
  assigned yet.
- The global HS-field update ported on the `develop` branch is not part of 0.1.

---
date: 2026-09-24
datetime: 2026-09-24 09:34 JST
model: Claude Fable 5.1 (Claude Code)
summary: |
  Ported the opt-in site world-line global HS-field update and the per-replica
  bin output from the private development history into the develop branch.
  All serial, OpenMP, MPI, hybrid and slow tests pass; outputs match the source build.
---

## 2026-09-24: Global HS-field update and replica-bin output

- Applied the thirteen source, test, validation and build commits of the global
  update (upstream range 463dc75..890b6e6, merged upstream as c422af0) onto
  `feat/global-hs-update`, branched from the new `develop` branch.
  The internal design and implementation plan documents were not imported.
- New input keys `global_update`, `global_interval` and `replica_bin_file`;
  see [README.md](README.md#global-hs-field-update). Disabled runs keep the
  default random stream and outputs (`tests/test_global_disabled.sh`).
- Adapted the port to the 0.1 output conventions: the scalar output mirror keeps
  the new `global_acceptance global_attempts` columns, and the collision checks
  cover `output_file` and `replica_bin_file` with the `.dat` default names.
  The replica-bin file itself keeps the upstream tab-separated layout.
- `make test_global_default` (byte comparison with a historical build) needs
  Git commit 463dc75, which this repository does not contain; it is not part
  of `make test` and fails with that explanation here.
- Verification: `make test`, `OMPI_CC=cc OMP_NUM_THREADS=2 make test_omp test_mpi
  test_hybrid` and `make test_slow` (including the 4x2 global-update regression)
  pass on macOS/Apple Clang/Accelerate. For the two baseline fixtures with and
  without `global_update=site`, stdout and `bins.tsv` are byte-identical to a
  clean build of upstream 890b6e6; `szz`, `sperp` and replica-log rows agree
  numerically and differ only in the 0.1 header/separator conventions.
- Validation data: [VALIDATION.md](VALIDATION.md) section 6 and
  [docs/validation/global-hs-4x2-2026-09-21.json](docs/validation/global-hs-4x2-2026-09-21.json).
  The scientific limits recorded there are unchanged; no release version is assigned.

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
