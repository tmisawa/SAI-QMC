---
date: 2026-07-14
datetime: 2026-07-14 23:45 JST
model: GPT-5 Codex
status: plan
topic: Track A centered scalar offset prototype
summary: |
  beta=33.325 限定の実験的対策として、UDV の全体正スケールを log_offset に
  分離し、D を centered exponent range に保つ実装計画。
  既存 combine/two_sided の数値経路は維持し、centered 専用更新 API と
  green_rebuild=centered を TDD で段階導入する。
---

# Track A: Centered Scalar Offset Implementation Plan

## 1. Scope and Acceptance Boundary

Track A は `L=4, U=16, dtau=0.025, beta=33.325` の single-factor overflow を
小さい変更で回避できるか検証する experimental prototype である。

対象:

- forward / alternating backward の running prefix。
- suffix / prefix GreenStack build。
- end-of-sweep one-sided rebuild。
- boundary two-sided rebuild。
- PH/non-PH の up/down factors。

対象外:

- beta=100 の保証。
- spread が centered double range を越えた場合の truncation。
- structured chain solver の代替。
- default mode の変更。

## 2. Representation

Track A factor は次を表す。

```text
P = exp(log_offset) U diag(D) T
```

```c
typedef struct {
    int n;
    double *U;
    double *D;
    double *T;
    double log_offset;
} UDV;
```

Invariants:

1. `log_offset` は finite。scalar scale は正なので determinant sign に影響しない。
2. `D[i]` は finite、nonzero、符号を保持する。
3. centered mode では `log(abs(D))` の中心を原則0に保つ。
4. effective log scale は
   `log(abs(D[i])) + log_offset`。
5. existing mode では全factorの `log_offset==0` を維持し、従来演算順を変えない。

`udv_init()` / `udv_identity()` は offset=0、`udv_copy()` は offset をコピーする。

## 3. Recenter Operation

```c
int udv_recenter(UDV *s, double hard_margin, double *radius,
                 double *remaining_margin);
```

Algorithm:

1. `lo=min(log(abs(D[i])))`, `hi=max(...)`。
2. `center=(lo+hi)/2`, `radius=(hi-lo)/2`。
3. `remaining_margin=ln(DBL_MAX)-radius`。
4. `remaining_margin < hard_margin` なら変更せずfail-fast。
5. `D[i] *= exp(-center)`、`log_offset += center`。
6. finite/nonzeroとrecenter後radiusを再検査。

Constants:

```text
hard_margin = 8 natural-log units
warning_margin = 32 natural-log units
```

hard margin 8 はexp生成前の安全域。warning 32 は既知trajectoryの最悪31.9と
ほぼ同じなので、拒否条件ではなく診断対象とする。

center の絶対値が大きく `exp(-center)` を直接作れない場合は、各要素を
`sign(D[i]) * exp(log(abs(D[i])) - center)` で再構築する。指数はradius内なので
hard gate 合格時はrepresentableである。

## 4. Update APIs

既存APIを変更せず、centered専用APIを追加する。

```c
void udv_lmul_centered_work(UDV *s, const double *B, LinalgWork *w);
void udv_rmul_centered(UDV *s, const double *B, LinalgWork *w);
```

内部は既存 lmul/rmul のQR手順を共有するが、次を守る。

- QR入力にはstored `D`だけを掛ける。`exp(log_offset)`は形成しない。
- QR前にstored Dのhard marginを検査する。
- QR後に新しいDをrecenterし、既存offsetへcenterを加える。
- U/Tの更新式は既存と同じ。
- failureは`LinalgWork.failed`へlatch。

既存`udv_lmul_work()` / `udv_rmul()`はrecenterを呼ばず、bit pathを維持する。
共通化は演算順を変えないstatic helperに限定し、既存testの結果を比較する。

## 5. One-Sided Inverse

centered factorではraw `D`の大小でDb/Dsを分けず、effective logを使う。

```text
ell_i = log(abs(D_i)) + log_offset

if ell_i > 0:
    Dbinv_i = sign(D_i) * exp(-ell_i)
    Ds_i    = 1
else:
    Dbinv_i = 1
    Ds_i    = sign(D_i) * exp(ell_i)
```

これは既存の `Db=D, Ds=1` (large) / `Db=1, Ds=D` (small) と同じ符号規約。
`Db`そのものは作らず、常に `abs(Dbinv)<=1` と `abs(Ds)<=1` を作る。

Implementation:

- `log_offset==0` は既存branchをそのまま実行する。
- nonzero offset のみeffective-log branchへ入る。
- determinant sign はbig側のD符号、det(U)、det(M)から従来どおり計算する。

## 6. Two-Sided Inverse

`udv_inv_one_plus_two_sided_work()` の左右splitも各factorのeffective logを使う。

```text
ell_l[i] = log(abs(D_l[i])) + offset_l
ell_r[i] = log(abs(D_r[i])) + offset_r
```

`Dlb^-1`, `Dls`, `Drb^-1`, `Drs` はすべて絶対値<=1で構築する。
scalar offset同士を加算してexpしない。

`offset_l==offset_r==0` は既存branchをそのまま通す。これにより現行
`green_rebuild=two_sided` の演算順と結果を維持する。

## 7. Combine Policy

ordinary combine はcentered production pathでは使用しない。

理由:

```text
C = D_l (T_l U_r) D_r
```

はstored radiiの和が709を越えるとoffsetを外出ししてもoverflowする。

Policy:

- `udv_combine()` はoffset=0 factorsに対する既存APIとして維持。
- nonzero offset入力は明示fail-fastするか、centered modeから到達不能にする。
- Track A boundary rebuildは必ずeffective-log two-sided solverを使う。
- end-of-sweepはeffective-log one-sided solverを使う。

`centered combine` は本prototypeでは実装しない。

## 8. Green and DQMC Integration

新mode:

```text
green_rebuild=centered
```

```c
typedef enum {
    GREEN_REBUILD_COMBINE = 0,
    GREEN_REBUILD_TWO_SIDED = 1,
    GREEN_REBUILD_CENTERED = 2
} GreenRebuildMode;
```

Semantics:

- `combine`: existing updates + combine rebuild。
- `two_sided`: existing updates + two-sided rebuild。
- `centered`: centered updates + effective-log two-sided/one-sided rebuild。

Integration sites:

- `green_stack_build_suffix()` -> centered r-multiply。
- `green_stack_build_prefix()` -> centered l-multiply。
- `green_from_scratch()` -> centered l-multiply when mode centered。
- `dqmc_sweep_forward()` running left up/down。
- `dqmc_backward_boundary()` and backward end-of-sweep。
- carried stack copy/store already propagates offset through `udv_copy()`。
- stab-drift reference Green receives the same mode through existing mode propagation。

Parser/header:

- default remains `combine`。
- accept `centered` only after standalone and Green tests pass。
- stdout header already prints selected string。
- `replicas.csv` schema is unchanged。

## 9. Diagnostics

Existing `udv_scale_file` currently records stored D only。centered modeでは意味が変わるため、
schemaを黙って再解釈しない。

Phase A prototypeではcompanion fileを追加する。

```text
udv_centered_file=path
```

Columns:

```text
beta_index Ltr replica_id seed sweep_count tau boundary spin direction
stored_min_logD stored_max_logD stored_radius log_offset
effective_min_logD effective_max_logD remaining_margin warning status
```

warningはremaining margin <=32。hard failureは<=8。
defaultではfileを開かずstdoutも変えない。

## 10. TDD and Commit Sequence

### A0. Representation

Tests first:

- init/identity offset=0。
- copy preserves offset。
- recenter preserves `exp(offset)*D` against long-double/mpmath-generated reference。
- mixed signs。
- radius 675 pass、radius over hard wall fail。
- no zero/inf after recenter。

Commit: `feat(linalg): add centered scale metadata to UDV`

### A1. Centered lmul/rmul

Tests:

- moderate repeated lmul/rmul product agrees with dense reference。
- centered and legacy agree at safe scale。
- 100--500 repeated graded updates keep stored D finite while offset accumulates。
- reconstructable cases agree with high precision。
- hard-margin failure latches `work.failed`。
- legacy APIs preserve prior expected values/path。

Commit: `feat(linalg): add centered UDV updates`

### A2. Effective-log one-sided inverse

Tests:

- offset=0 legacy branch equality。
- effective spread 1200/1350/1400 synthetic mixed factors。
- captured-real-factor fixture from investigation, expected Green generated offline。
- negative D and determinant sign。
- offset shift invariance: different `(offset,D)` representing same factor。

Commit: `feat(linalg): invert centered UDV factors`

### A3. Effective-log two-sided inverse

Tests:

- offset=0 legacy two-sided equality。
- one side offset、both sides offset。
- reparameterization invariance per side。
- dense-safe reference and high-precision fixed fixtures。
- sign cases。

Commit: `feat(linalg): support centered two-sided Green rebuilds`

### A4. Green/DQMC opt-in integration

Tests:

- parser default/valid/typo。
- stack suffix/prefix centered offsets propagate。
- forward and alternating mode propagation。
- PH and non-PH small cases。
- no call to ordinary combine in centered mode。
- diagnostics schema and warning/hard-fail behavior。

Commit: `feat(dqmc): add opt-in centered Green rebuild mode`

### A5. Validation

1. `make test`, `make test_mpi`, `make test_slow`, `git diff --check`。
2. beta=25 legacy/two_sided/centered comparison。
3. beta=30,31,32 same seed local statistical comparison。
4. beta=33.325 known seed: 2 sweeps, 100, 1000, then full
   `nwarm=2000,nmeas=9000,nbin=100`。
5. nrep=4 MPI only after serial target passes。
6. runtime/profile comparison。

## 11. Stop Conditions

Track Aを停止する条件:

- remaining margin <=8。
- stored D zero/inf。
- fixed real-factor relative error >1e-9。
- safe beta Green discrepancy >1e-10 at rebuild level。
- beta=30--32 observablesが2 sigma外。
- sign mismatch。
- beta=33.325 full runでnumerical fail-fast。

停止時にmarginを緩めたり、rank truncationを追加しない。結果を記録して
structured chain trackへ戻る。

## 12. Success Criteria

Track A prototype success:

1. existing combine/two_sided tests and behavior remain unchanged。
2. centered standalone fixtures pass、real-factor error<=1e-9。
3. beta=30--32 physics consistency passes。
4. beta=33.325 known seed full run completes finite。
5. all remaining margins >8 and warnings are recorded。
6. documentation states beta=33.325 experimental scope and no beta=100 claim。

## 13. Immediate Next Step

A0 representation testsから開始する。parser/DQMC integrationにはまだ触れない。
最初にreview対象とするdiffは`UDV.log_offset`, identity/copy/recenterとそのunit tests
だけに限定する。
