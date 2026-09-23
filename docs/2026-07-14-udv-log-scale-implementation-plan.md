---
date: 2026-07-14
datetime: 2026-07-14 JST
model: GPT-5 Codex
summary: |
  UDV scale overflow 対策の実装計画をレビュー指摘に基づいて改訂。
  log-scale UDV を第一候補から外し、既存 UDV factor を保った two-sided Db/Ds
  Green 再構成を opt-in 第一候補とする。log-scale は単一 factor D の限界が
  実測で問題化した場合の第二段と位置づける。
---

# UDV Scale Overflow 対策 実装計画（改訂版）

対象: AF_QMC の低温強結合 DQMC における UDV scale overflow 対策

関連文書:

- `docs/2026-07-13-l4-u16-low-temperature-nan-report.md`
- `docs/2026-07-14-udv-log-scale-plan-review.md`

## 背景

L=4, U=16, dtau=0.025, beta=33.325 の再現 seed
`14012418791647386686` で、現行 UDV 経路は `udv_combine()` 中に overflow する。

直接の非有限化箇所:

```c
C[i + j * n] = l->D[i] * (l->T * r->U)[i + j * n] * r->D[j];
```

診断結果:

```text
ERROR: udv_combine non-finite matrix at stage=C row=0 col=0 value=-inf n=16
```

原因は、左右 UDV factor の diagonal scale `D` を double の通常積として再構成していること。低温・強結合では時間発展行列積の特異値スケールが指数的に広がり、`l->D[i] * r->D[j]` が `DBL_MAX` を超える。

これは行列式そのものの問題ではなく、行列積の scale separation / condition number の問題である。`U` が大きいほど HS 対角因子の伸び縮みが強くなり、同じ beta でも特異値スケールの広がりが速くなる。

再現ログの区別:

- 測定側 fail-fast のみでは、`sweep_count=10397`, `bin=93`, `meas=26` で `non-finite Green/sign before measurement` として停止。
- UDV 段階診断後は、同じ条件が最初の warmup sweep 中に `udv_combine stage=C row=0 col=0 value=-inf` で停止。

## レビュー反映後の方針

当初計画では log-scale UDV を第一候補とした。しかしレビューで次の重大懸念が示された。

- global offset 付き log-scale combine は、overflow を underflow / rank 欠損に置き換えるだけになる可能性が高い。
- `udvlog_lmul_work()` / `udvlog_rmul()` で、入力 `Dlog` を dense 行列に適用する方法が未設計。
- `prefix * suffix` を dense combine してから Green を作る構造自体が問題であり、標準的な two-sided Db/Ds 分割なら cross product を作らずに済む。

したがって、改訂後の実装方針は次の通り。

1. **既存経路を default として保持する。**
2. **opt-in の第一候補は two-sided Db/Ds Green 再構成とする。**
3. **log-scale UDV は第二段とし、単一 factor の `D` 自体が double 範囲を超える必要が実測で確認された場合に進める。**

## User-facing option

新しい option は Green 再構成方式を選ぶものとして設計する。

```ini
green_rebuild=combine
```

初期実装で受け付ける値:

- `combine`: 現行経路。`udv_combine()` で prefix/suffix を合成し、`udv_inv_one_plus_work()` で Green を作る。default。
- `two_sided`: prefix/suffix を combine せず、two-sided Db/Ds 分割で Green を作る。

将来予約:

- `log`: log-scale UDV 経路。Phase 6 以降で必要性が確定するまで parser では受け付けない。

stdout header には mode を出す。

```text
# ... green_rebuild=combine
```

または

```text
# ... green_rebuild=two_sided
```

`replicas.csv` の列追加は下流解析を壊しうるため、初期実装では避ける。`run_info.txt` は C コード側の生成物ではないため、HPC script 側の別変更として扱う。

## Core Numerical Design

### 現行 combine 経路

現行 `green_from_boundary_factors()` は概念的に次を行う。

```text
L = U_l D_l T_l
R = U_r D_r T_r
A = L R
combined = UDV(A)
G = (I + A)^-1
```

実装上は `udv_combine(l, r, combined)` で

```text
TLUR = T_l U_r
C[i,j] = D_l[i] * TLUR[i,j] * D_r[j]
QR(C)
```

を作る。この `D_l[i] * D_r[j]` が今回の overflow 原因である。

### two-sided Db/Ds 経路

combine を行わず、2 つの UDV factor のまま Green を作る。

```text
A = (U_l D_l T_l)(U_r D_r T_r)
M = T_l U_r
```

各 diagonal scale を large / small に分ける。

```text
D_l = D_lb D_ls
D_r = D_rb D_rs
```

分割規則:

```text
if |D[i]| > 1:
    D_b[i] = D[i]
    D_s[i] = 1
else:
    D_b[i] = 1
    D_s[i] = D[i]
```

すると、

```text
I + A
= U_l D_lb H D_rb T_r

H = D_lb^-1 U_l^T T_r^-1 D_rb^-1 + D_ls M D_rs
```

したがって、

```text
G = (I + A)^-1
  = T_r^-1 D_rb^-1 H^-1 D_lb^-1 U_l^T
```

重要点:

- `D_lb^-1`, `D_rb^-1`, `D_ls`, `D_rs` はすべて絶対値 1 以下。
- `l->D[i] * r->D[j]` の cross product を作らない。
- `H` は現行の raw combine 行列 `C` より well-scaled で、overflow の直接原因を避ける。
- 既存 `UDV` 構造体をそのまま使える。
- 主な変更箇所は `green_from_boundary_factors()` の combine+inv 経路に閉じる。

det sign:

```text
sign(det(I + A))
= sign(det(U_l)) * sign(prod(D_lb)) * sign(det(H)) * sign(prod(D_rb))
```

`det(T_r)=1` なので不要。`sign(det(U_l))` と `sign(det(H))` は既存の `la_logdet_work()` で求める。

### log-scale UDV の位置づけ

two-sided 経路でも、単一 factor `D_l` または `D_r` 自体が `DBL_MAX` を超える場合は解決できない。その場合は `D` を `sign(D), log|D|` で保持する log-scale UDV が必要になる。

ただし、今回の直接原因は left/right cross product であり、単一 factor の `D` overflow はまだ実測されていない。よって log-scale UDV は第二段に置く。

## Data Structure Plan

Phase 1-5 では既存 `UDV` を維持する。

```c
typedef struct {
    int n;
    double *U;
    double *D;
    double *T;
} UDV;
```

追加するのは Green 再構成 mode と、two-sided 用 scratch のみ。

候補 enum:

```c
typedef enum {
    GREEN_REBUILD_COMBINE = 0,
    GREEN_REBUILD_TWO_SIDED = 1
} GreenRebuildMode;
```

mode の所在:

- `Params` に `green_rebuild` を持たせる。
- `Dqmc` に `green_rebuild` を保持する。
- `Green` または `GreenStack` にも mode を伝播する。`green_from_scratch()`, `green_from_stack()`, `green_from_boundary_factors()` は `Dqmc` を見ないため、mode は `Green` 側に置くか、関数シグネチャに渡す必要がある。

推奨:

```c
typedef struct {
    ...
    GreenRebuildMode rebuild_mode;
} Green;
```

理由:

- `dqmc_record_stab_drift()` は `green_from_scratch()` を直接呼ぶ。
- `green_from_stack()` / `green_from_boundary_factors()` は `Green` を受け取る。
- `GreenStack` は UDV factor の storage であり、two-sided mode では構造を変えない。

## Function Plan

### New two-sided function

追加候補:

```c
int udv_inv_one_plus_two_sided_work(const UDV *l,
                                    const UDV *r,
                                    double *g,
                                    int *det_sign,
                                    LinalgWork *w);
```

責務:

1. `M = T_l U_r` を作る。
2. `D_lb`, `D_ls`, `D_rb`, `D_rs` の sign/value を作る。
3. `T_r^-1` を `dtrtri` で作る。
4. `H = D_lb^-1 U_l^T T_r^-1 D_rb^-1 + D_ls M D_rs` を作る。
5. `H^-1` を作る。
6. `g = T_r^-1 D_rb^-1 H^-1 D_lb^-1 U_l^T` を作る。
7. det sign を返す。
8. すべての中間行列で finite check し、failure は `LinalgWork.failed` に latch する。

scratch:

- `LinalgWork` の既存 n x n buffers を割り当てて使う。
- 追加 buffer が必要なら `LinalgWork` に明示的に増やす。
- row/column scaling vectors は既存 `v1`, `v2` に加え、必要なら追加する。

### Green integration

現行:

```c
udv_combine(prefix, suffix, combined, &G->work);
rc = udv_inv_one_plus_work(combined, G->g, &sgn, &G->work);
```

two-sided mode:

```c
rc = udv_inv_one_plus_two_sided_work(prefix, suffix, G->g, &sgn, &G->work);
```

`combined` は不要になるが、既存 API 互換のため引数には残してよい。

`green_from_scratch()` と `green_from_left_udv()` は単一 UDV factor なので、初期実装では現行 `udv_inv_one_plus_work()` のままとする。問題が出た場合に log-scale UDV を検討する。

## Diagnostics Before Implementation

two-sided を実装する前に、現在の UDV scale spread を測る軽量診断を追加する。

目的:

- global offset log-combine が本当に不足するか実測で確認する。
- 単一 factor `D` 自体が `DBL_MAX` に近づいているか確認する。
- two-sided 実装後の残リスクを把握する。

記録したい値:

```text
beta_index
Ltr
replica_id
seed
sweep_count
tau / boundary index
left_min_logD
left_max_logD
right_min_logD
right_max_logD
left_spread_logD
right_spread_logD
cross_max_logD = max_left + max_right
```

ここで `logD = log(fabs(D[i]))`。単位は natural log units と明記する。`decades` とは書かない。

この診断は production default では無効にし、明示 option または compile-time flag で使う。

## Implementation Phases

### Phase 0: Plan and baseline preservation

- 本計画に従い、既存 fail-fast / UDV diagnostics は保持。
- `combine` default を明文化。
- `make test`, `make test_mpi`, `make test_slow`, `git diff --check` が通る状態を baseline とする。

### Phase 1: `green_rebuild` option

- `Params` に `green_rebuild` を追加。
- parser に `green_rebuild=combine|two_sided` を追加。
- default は `combine`。
- typo は error。
- stdout header に `green_rebuild=` を出す。
- `replicas.csv` には列を足さない。

tests:

- default is `combine`
- accepts `combine`
- accepts `two_sided`
- rejects typo

### Phase 2: UDV scale spread diagnostics

- `udv_combine()` または `green_from_boundary_factors()` 直前で left/right `D` spread を記録する診断を追加。
- 再現 seed の短縮 run で `left/right max/min logD` を取得。
- global offset log-combine の viability を実測で判定する。

この Phase は two-sided 実装の設計確認であり、production default には影響させない。

### Phase 3: standalone two-sided Green reconstruction

- `udv_inv_one_plus_two_sided_work()` を追加。
- まずテストから作る。

tests:

- moderate scale case で dense reference `(I + L R)^-1` と比較。
- existing `udv_combine()` + `udv_inv_one_plus_work()` と Green が一致することを比較。
- det sign が dense `det(I + L R)` と一致。
- QR の符号不定性を避けるため、UDV factor そのものではなく、Green / dense product / observables 単位で比較する。

### Phase 4: integrate `green_rebuild=two_sided`

- `green_from_boundary_factors()` に mode 分岐を追加。
- `combine` mode は現行挙動。
- `two_sided` mode は `udv_inv_one_plus_two_sided_work(prefix, suffix, ...)` を使う。
- `green_from_stack()` と alternating backward 経路は `green_from_boundary_factors()` 経由なので同時にカバーされる。
- `green_from_scratch()` / `green_from_left_udv()` は単一 factor のため現行のまま。

### Phase 5: DQMC validation

最低限:

- `make test`
- `make test_mpi`
- `make test_slow`
- `git diff --check`

numerical / physics validation:

- `green_rebuild=combine`, beta=25, L=4,U=16 smoke: 現行通り finite。
- `green_rebuild=two_sided`, beta=25, L=4,U=16 smoke: finite。
- `green_rebuild=two_sided`, beta=33.325, seed `14012418791647386686`, `nwarm=2000,nmeas=9000,nbin=100`: run が完走し、observables が finite。
- `nrep=4` MPI 再現条件で、従来 NaN seed を含む全 replica の status が正しく出る。
- 安定な既知領域では、`combine` と `two_sided` の observables が統計誤差内で一致。

bitwise 一致は要求しない。浮動小数点演算順が変わるため、同じ seed でも accept/reject trajectory が分岐し得る。

### Phase 6: decide whether log-scale UDV is necessary

two-sided 実装後も、単一 factor の `D` が double 範囲を超える、または `udv_lmul_work()` / `udv_rmul()` で overflow することが実測された場合のみ log-scale UDV を進める。

この段階で初めて次を検討する。

```c
typedef struct {
    int n;
    double *U;
    int *Dsign;
    double *Dlog;
    double *T;
} UDVLog;
```

log-scale UDV を進める場合の注意:

- `udvlog_lmul_work()` / `udvlog_rmul_work()` の入力 `Dlog` 適用方法を先に設計する。
- global offset 方式は underflow / rank 欠損の危険が高いため、column-wise scaling、pivoted QR (`dgeqp3`)、または別の balancing を検討する。
- `log` mode を parser で受け付けるのは、この設計が固まってからにする。

## Acceptance Criteria

two-sided 実装完了とみなす条件:

1. `green_rebuild=combine` が default で、既存 tests が全通過。
2. `green_rebuild=two_sided` が input option として選べる。
3. `two_sided` mode の unit tests が dense reference と一致。
4. L=4,U=16,beta=33.325 の再現 seed run が完走し、observables がすべて finite。
5. `two_sided` mode でも non-finite が発生した場合、fail-fast で具体的 stage を出す。
6. stdout header に `green_rebuild=` が残り、結果の mode を混同しない。

## Risks

- two-sided は combine overflow を避けるが、単一 factor の `D` overflow は避けられない。
  - Phase 2 diagnostics で単一 factor の scale headroom を測る。
- `H` は raw combine より well-scaled だが、任意 trajectory で well-conditioned と保証されるわけではない。
  - `H`, `Hinv`, final `g` の finite check と residual-style tests を追加する。
- `green_rebuild` mode の所在を誤ると、stab drift / scratch Green 経路だけ旧 mode になる可能性がある。
  - `Green` に mode を持たせる設計を優先する。
- non-PH 経路でも up/down 双方に同じ mode が必要。
  - two-sided は既存 `UDV` のままなので non-PH でも原理的に対応可能。
  - log-scale UDV を後で入れる場合は down-spin 用 fields も忘れない。

## Rollback Plan

- default は常に `green_rebuild=combine`。
- `two_sided` に問題があれば option を使わなければ既存経路に戻れる。
- 実装中に不安定な場合は、parser は残しつつ `two_sided` を runtime error にするのではなく、該当 commit を戻す。default 経路への影響を最小化するため。

## Next Action

最初の PR / commit 単位:

- `green_rebuild=combine|two_sided` option 追加。
- default `combine`。
- parser tests。
- stdout header metadata。

次の PR / commit 単位:

- UDV scale spread diagnostics。
- 再現 seed の短縮 run で natural-log scale spread を記録。

その後:

- `udv_inv_one_plus_two_sided_work()` の unit tests と実装。
- `green_from_boundary_factors()` への opt-in 統合。
