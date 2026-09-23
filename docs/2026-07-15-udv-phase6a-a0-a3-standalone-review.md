---
date: 2026-07-15
datetime: 2026-07-15 00:23 JST
model: GPT-5 Codex
status: approved-for-a4
topic: Track A A0--A3 standalone implementation review
summary: |
  Centered offset representation and the one-/two-sided effective-log algebra are
  internally consistent, and legacy offset-zero paths remain isolated. The F1--F4
  findings were closed with input-aware margins, fixed high-precision fixtures,
  centered-sign coverage, and typed range diagnostics. A4 may proceed opt-in.
---

# Track A A0--A3 Standalone Review

## Verdict

**Approved for opt-in A4 integration after resolution of F1--F4.** No algebraic
correctness defect was found in the centered one-sided or two-sided formulas.
A0--A3 now satisfy the standalone implementation plan: update amplification is
bounded before mutation, real and independent high-precision fixtures pass,
centered sign crossings are covered, and range failures are typed and explicit.

The ordinary `udv_combine()` centered-input guard is correct and closes the
previous semantic hazard without changing its offset-zero path.

## Findings

### F1 — High: the pre-update hard margin does not account for the multiplier

`udv_lmul_centered_work()` and `udv_rmul_centered()` recenter with a fixed margin
of 8 before forming `B U D` or `D T B`. The subsequent matrix multiplication can
consume part or all of that margin. Therefore `remaining_margin >= 8` alone does
not prove that the QR input is representable for an arbitrary finite `B`.

This is not reproduced by the current DQMC slice matrices, and the post-operation
finite checks fail safely. It is nevertheless a missing precondition in the API
contract. Before A4, do one of:

1. compute a conservative per-update amplification allowance from `B` and require
   `remaining_margin > allowance + safety_margin`; or
2. prove and document a fixed upper bound for all DQMC `B` matrices in Track A and
   choose the hard margin from that bound.

Required regression: a factor near the admitted radius plus a deliberately large
finite `B` must fail before mutating the factor, while the maximum allowed DQMC
slice must pass.

### F2 — High: planned fixed numerical fixtures are not automated

The investigation recorded an actual beta=33.325 factor with spread
`1350.7731737311087`, offset `30.3872021385450`, and mpmath relative Green error
`6.82e-13`. The raw `U/D/T` data and expected Green are not present in the test
suite. A2 currently covers moderate reparameterization and a diagonal effective
log `+650/50/-550` case; A3 covers reparameterization and high-scale diagonal
cases. These do not replace the planned non-orthogonal real-factor regression.

Before A4, preserve the captured n=16 factor (or recapture the same deterministic
trajectory point) and add a fixed expected-Green regression with the plan's
`1e-9` relative-error gate. Add a non-diagonal, high-precision two-sided fixture as
well, because comparing two implementations that share the same derivation is not
an independent oracle.

### F3 — Medium: centered determinant-sign coverage is incomplete

Negative-D sign tests exist only with `log_offset == 0`. Offset reparameterization
tests use positive factors. The effective-log big/small classification is used in
the determinant sign calculation, so the combination of nonzero offset, negative
entries, and entries crossing the effective `ell=0` boundary needs a direct test.

Required regression: shift both sides independently, include negative D on both
big and small sides, and compare Green plus determinant sign with a dense-safe or
high-precision reference.

### F4 — Medium: effective-log underflow is conservative but not diagnosed

The inverse routines reject zero results from `exp(-ell)` or `exp(ell)`. This is a
safe failure for `|ell|` beyond the subnormal range, but the failure is currently
reported only through the generic zero-vector check. Stored-radius margin alone
does not constrain the absolute effective logs because `log_offset` may
accumulate.

A4 diagnostics should record effective min/max and identify this as an explicit
stop condition. For the captured target factor (`min=-645.0`, `max=705.8`) the
split remains representable, so this does not block the scoped beta=33.325 case.

### F5 — Low: failure-state and lifecycle contracts should be documented

Centered lmul/rmul may have partially updated `U/D/T` if a post-QR check fails.
This is acceptable only because `LinalgWork.failed` is latched and DQMC treats it
as fatal; callers must not retry or consume that factor. Also, `udv_free()` clears
pointers and size but leaves `log_offset` stale. Resetting it to zero would make
the lifecycle invariant complete, though no current caller uses a freed factor.

## Passed Review Points

- A0 representation preserves effective logs and D signs; recenter is
  transactional on its own failure path.
- A1 never forms `exp(log_offset)` and keeps the offset-zero arithmetic path
  separate.
- A2 one-sided factorization consistently uses inverse-big/small factors of
  magnitude at most one.
- A3 applies the same convention independently to left and right factors; the
  matrix scaling indices and determinant decomposition are consistent with the
  legacy derivation.
- Non-finite inputs and zero split factors latch failure.
- Ordinary combine rejects centered factors before changing its output.
- The last full verification passed `make test`, `make test_mpi`,
  `make test_slow`, and `git diff --check`.

## Gate to Resume A4

1. Resolve F1 with an input-aware margin rule or a documented DQMC bound.
2. Add the actual-factor one-sided fixture and an independent non-diagonal
   two-sided fixture (F2).
3. Add centered negative-sign/crossing tests (F3).
4. Define the effective-log diagnostic/stop reason required by F4.
5. Re-run all standard, MPI, and slow tests.

After these items pass, A4 may proceed as an opt-in mode. This review does not
support changing the default from `combine`, nor does it extend Track A beyond
the beta=33.325 experimental scope.

## Resolution Log

- 2026-07-15: **F1 resolved.** Centered lmul/rmul now evaluate a conservative
  log-domain upper bound for the actual QR input before forming it. The bound
  includes `B`, and right multiplication also includes `T`. A rejected update
  restores the pre-recenter D and offset; tests verify the complete factor is
  unchanged for a near-wall factor amplified by a finite matrix.
- 2026-07-15: **F3 resolved.** One- and two-sided tests now cover negative D
  with nonzero offsets and stored/effective big-small classifications crossing
  in both directions; Green values and determinant signs are invariant.
- 2026-07-15: **F4 resolved at the linalg layer.** `LinalgWork.failure_reason`
  distinguishes centered-radius, centered-update-margin, and effective-log-range
  stops. Effective split exponents below `log(DBL_TRUE_MIN)` now emit an explicit
  diagnostic with side, index, effective log, and representable limit. A4 must
  propagate this reason into its companion diagnostic file.
- 2026-07-15: **F2 resolved.** The deterministic beta=33.325 n=16 factor was
  recaptured from seed `14012418791647386686`; its min/max logs match the
  investigation exactly. The raw factor and a 500-digit direct-inverse Green
  reference are fixed fixtures. A separate non-diagonal two-sided fixture with
  independent left/right offsets and a 500-digit direct reference was added.
  Both enforce relative error `<=1e-9` and determinant-sign agreement. Reference
  generators are committed and do not use the production Db/Ds formulas.

All standalone review gates are now closed. A4 may proceed as an experimental,
opt-in DQMC mode; default-mode and beta=100 restrictions remain unchanged.
