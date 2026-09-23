---
date: 2026-07-14
datetime: 2026-07-14 23:12 JST
model: GPT-5 Codex
status: design
topic: Phase 6 single-factor UDV scale countermeasure
summary: |
  単一 UDV の D range overflow を避けるため、長い積を有界な contiguous
  UDV chunk 列として保持し、単一 factor へ collapse せず Green を再構築する。
  まず augmented cyclic solve の standalone feasibility を dense reference と
  residual で判定し、合格後に prefix/suffix stack と sweep へ段階統合する。
---

# Phase 6: Single-Factor Scale Countermeasure Detailed Design

## 1. Goal and Non-Goal

Goal:

- `udv_lmul_work stage=B_U_D` / `udv_rmul stage=D_T_B` の単一 factor
  overflow を避ける。
- 既知の `L=4, U=16, dtau=0.025, beta=33.325`, seed
  `14012418791647386686` を finite observables で完走させる。
- forward、alternating backward、suffix build、prefix build、end-of-sweep
  rebuild の全経路を同じ表現で覆う。
- `green_rebuild=combine` と現行 `two_sided` の既定挙動を壊さない。

Non-goal:

- Phase 6 の最初から `green_rebuild=two_sided` を default にしない。
- rank truncation や singular direction の切り捨てを暗黙に導入しない。
- log-D の global offset だけで問題を解決したことにしない。
- 最初の実装で任意の beta に対する無制限安定性を主張しない。

## 2. Confirmed Failure Mechanism

現行 UDV は積 `P = U D T` を一つの factor に集約する。

`udv_lmul_work()` は次を dense に形成する。

```text
M = (B U) D
```

`udv_rmul()` は次を dense に形成する。

```text
M = D (T B)
```

beta=33.325 の既知 seed では、two-sided solver により左右 factor の
`D_l * middle * D_r` overflow を回避した後、end-of-sweep の追加 lmul で
`B_U_D` が infinity になる。

beta=30 の平衡測定でも単一 factor の `spread_logD` は約 1086--1191 で、
double の dense representable width 約 745 を既に超える。このため
`D = exp(logD - offset)` の一つの offset だけでは、小さい方向が underflow して
rank を失う。

## 3. Chosen Representation: Bounded UDV Chunk Chain

長い積を一つの UDV に集約せず、時間順序を保った contiguous chunk 列で持つ。

```c
typedef enum {
    UDV_CHAIN_FORWARD = 0, /* product = chunk[count-1] ... chunk[0] */
    UDV_CHAIN_REVERSE = 1  /* orientation is explicit; no implicit reversal */
} UdvChainOrientation;

typedef struct {
    int n;
    int capacity;
    int count;
    int tau_begin;
    int tau_end;
    UdvChainOrientation orientation;
    UDV *chunk;
    int *chunk_tau_begin;
    int *chunk_tau_end;
} UDVChain;
```

Invariants:

1. 各 chunk は連続した B block の積で、順序を入れ替えない。
2. 各 chunk の `U`, `D`, `T` は finite、`D[i] != 0`。
3. chunk を閉じた後は immutable とし、別 chunk の lmul/rmul で更新しない。
4. chain 全体を通常の `udv_combine()` で一つに collapse しない。
5. orientation、tau range、chunk count を API と診断に明示する。

### 3.1 Initial Chunk Policy

最初の policy は動的な overflow 直前判定ではなく、再現可能な固定長を使う。

```text
chunk_beta_max = chunk_slices * dtau <= 8
```

beta=30 の実測成長率 `max_logD / beta ~= 20` と spread の実測から、beta 8 の
chunk は max scale を概ね 160、spread を概ね 320 程度に抑える見込みである。
これは `ln(DBL_MAX)` から十分離れている。

実装時の既定値:

```text
chunk_slices = max(stab_interval, floor(8 / dtau) rounded down to a
                   multiple of stab_interval)
```

ただし最低 1 stabilization block とする。dtau=0.025, stab=4 では
`chunk_slices=320`、beta=33.325 では最大 5 chunks になる。

固定長を最初に選ぶ理由:

- trajectory や丸めに依存せず、combine/two-sided 比較を再現しやすい。
- 「次の lmul が overflow するか」を実行前に正確に予測する必要がない。
- threshold policy 自体と solver の数値誤差を分離できる。

追加 safety gate として、更新後の各 chunk に
`max(abs(log(abs(D)))) <= 300` および `spread_logD <= 600` を要求する。
超えた場合は自動 truncation せず fail-fast し、より短い chunk を要求する。

## 4. Why Storage Alone Is Insufficient

chunk 列を最後に

```text
udv_combine(chunk[0], chunk[1]) -> ... -> one UDV
```

とすると、元の single-factor wall を遅らせるだけで再現する。したがって
storage と Green solver は不可分である。

また、単純な global log-D は beta=30 で既に spread が double range を越える。
Phase 6 の production path には採用しない。log-D は明示的な rank/truncation
policy を別途設計する場合だけ代替候補とする。

## 5. Standalone Solver Candidate

### 5.1 Augmented Cyclic System

chain の順序付き積を

```text
P = F[m-1] F[m-2] ... F[0]
```

とする。`G = (I + P)^-1` は、次の block cyclic system の `x[0]` block として
得られる。

```text
x[0] + F[m-1] x[m-1] = b
x[1] - F[0]   x[0]   = 0
x[2] - F[1]   x[1]   = 0
...
x[m-1] - F[m-2] x[m-2] = 0
```

右辺を `b=I` として解けば `x[0]=G`。この augmented matrix を `K` とすると、
上記の符号規約では

```text
det(K) = det(I + P)
```

なので LU pivot parity と U diagonal の符号から determinant sign を得られる。
この等式は unit test で odd/even `m`、negative determinant を含めて固定する。

### 5.2 Chunk Materialization

各 `F[k] = U[k] D[k] T[k]` は chunk gate 内なので dense materialization が
可能である。ただし、最初から production solver とみなさない。

```c
int udv_chain_inv_one_plus_augmented_work(const UDVChain *chain,
                                          double *g,
                                          int *det_sign,
                                          UDVChainWork *work);
```

`UDVChainWork` は `m*n` 次元の K、複数 RHS、pivot を保持する。allocation は
solver call の外で行い、hot path で malloc/free しない。

### 5.3 Numerical Acceptance Gate

augmented solve は entries の絶対 scale が大きくなり得るため、単に finite なら
採用とはしない。次をすべて満たす必要がある。

1. `n<=8`, moderate scale で dense product reference と相対誤差 `<=1e-11`。
2. dense product が安全に形成できる stress case で `<=1e-9`。
3. product 自体は overflow するが、analytically constructed diagonal/orthogonal
   chain で期待 Green と `<=1e-9`。
4. residual
   `||K X - RHS||_inf / (||K||_inf ||X||_inf + ||RHS||_inf)` が
   `<= 1e-11`（moderate）、`<=1e-8`（stress）。
5. `G` の finite check、det sign、`G(I+P)` residual が参照可能な case で一致。
6. chunk の分け方を変えた同一 product で Green が tolerance 内一致。

LAPACK `dgesvx` の equilibration と reciprocal condition estimate を優先する。
利用環境で `dgesvx` が使えない場合だけ `dgetrf/dgetrs` + 明示 residual を使う。
`rcond` が設定 threshold を下回る場合は success にせず fail-fast する。

### 5.4 Feasibility Stop Gate

augmented dense solve は計算量 `O((m n)^3)`、memory `O((m n)^2)` である。
したがって Phase 6.1 は correctness prototype であり、無条件の large-L
production algorithm ではない。

次のどれかなら DQMC integration を止め、block-sparse / UDV-aware elimination
を再設計する。

- stress residual gate を満たさない。
- `rcond` 判定が beta=33.325 の正常 trajectory を拒否する。
- L=4 target でも rebuild time が現行 two-sided の 10 倍を超える。
- projected L=16 memory が設定上限 2 GiB/replica を超える。

この stop gate により、「overflow が消えたが信用できない値」を採用しない。

## 6. Integration Design

Standalone gate 合格後にだけ以下へ進む。

### 6.1 GreenStack

`GreenStack.S[j]` の累積単一 UDV を直ちに削除しない。新 mode 用に並行して
chain boundary view を追加する。

```c
typedef struct {
    /* existing fields */
    UDV *S;

    /* Phase 6 opt-in fields */
    UDV *block;          /* one UDV per stabilization block */
    int block_count;
    int chain_enabled;
} GreenStack;
```

各 `block[j]` は `B(b[j+1], b[j])` のみを表し、累積 suffix/prefix ではない。
boundary rebuild では index range の view を作り、コピーせず solver に渡す。

```c
typedef struct {
    const UDV *block;
    int first;
    int count;
    int step; /* +1 or -1 */
} UDVChainView;
```

これにより prefix/suffix の双方で同じ block storage を使用し、orientation bug を
index/step のテストで検出できる。

### 6.2 Running Prefix/Suffix

`Dqmc.left_u/left_d` を一つの UDV として全 beta まで延長しない。新 mode では
現在 boundary までの block/chunk view を使う。

- forward: 完了済み block `[0, j)` が prefix。
- suffix: frozen block `[j, M)` が suffix。
- backward: 同じ block array の view を逆方向 API で明示する。
- end-of-sweep: 全 block `[0, M)` を chain solver に渡す。

accepted HS flips により block 内容が変化するため、sweep 中に通過済み block は
その場で再構築し、未通過側は sweep 開始時の frozen stack を使う。これは現行の
carried prefix/suffix semantics と同じだが、保存単位を累積 UDV から block UDV
へ変える。

### 6.3 Green API

既存 API は維持し、新 API を追加する。

```c
int green_from_chain(Green *G, const UDVChainView *ordered_product,
                     int cur_l);
```

新 mode 名は standalone gate 合格後にのみ parser へ追加する。候補:

```text
green_rebuild=chunked
```

`two_sided` の意味を後から変更しない。既存結果の provenance を守るためである。

## 7. Failure Handling and Diagnostics

すべての失敗は既存 `LinalgWork.failed -> Dqmc.status -> replica fail-fast` に接続する。
新規診断は stderr に最低限次を含める。

```text
solver=udv_chain_augmented
stage=materialize|equilibrate|factor|solve|residual
n=<n> chunks=<m> tau_begin=<...> tau_end=<...>
chunk=<k> max_logD=<...> spread_logD=<...>
rcond=<...> residual=<...>
```

`udv_scale_file` は schema を壊さず、Phase 6 専用の別 TSV
`udv_chain_file` を用意する。最低列:

```text
sweep spin direction tau boundary chunk_index chunk_tau_begin chunk_tau_end
min_logD max_logD spread_logD chunk_count rcond residual status
```

default stdout と `replicas.csv` は変更しない。

## 8. Memory and Cost Model

`N` を site 数、`M=ceil(Ltr/stab)`、`m=ceil(beta/chunk_beta_max)` とする。

- block UDV storage: `O(M N^2)`。現行 cumulative stack と同じ次数。
- augmented work: `O(m^2 N^2)` doubles。
- dense augmented factorization: `O(m^3 N^3)`。

beta=33.325, chunk_beta_max=8 では `m<=5`。L=4 (`N=16`) の K は最大 80x80
で feasibility と acceptance target には十分小さい。一方 L=16 (`N=256`) では
K は最大 1280x1280 となり、boundary ごとの dense factorization は高価である。
したがって correctness gate 後に profiler で判断し、large-L には block structure を
利用した `O(m N^3)` solver が必要になる可能性を明記する。

## 9. TDD Work Breakdown

### Phase 6.0: Representation only

1. `UDVChain` alloc/free/reset/append/view tests。
2. 2--5 chunks の dense product helper で orientation と tau ranges を検証。
3. chunk gate (`max_logD`, `spread_logD`) の pass/fail tests。

Exit: DQMC code path は未変更、全既存 test が bit-compatible。

### Phase 6.1: Standalone augmented solver

1. diagonal scalar (`n=1`) cases。
2. moderate random orthogonal/triangular chunks。
3. negative determinant sign cases。
4. chunking-invariance cases。
5. overflow-scale analytic cases。
6. residual/rcond failure tests。

Exit: Section 5.3 の全 gate を通過。失敗なら integration へ進まない。

### Phase 6.2: Green block stack

1. stabilization block UDV の dense product tests。
2. prefix/suffix/backward `UDVChainView` の順序 tests。
3. existing cumulative stack と safe beta で Green 比較。

Exit: safe region で `combine`, `two_sided`, `chunked` が tolerance 内一致。

### Phase 6.3: Forward sweep integration

1. boundary rebuild。
2. end-of-sweep full chain rebuild。
3. PH / non-PH up/down coverage。
4. `green_rebuild=chunked` parser/header tests。

Exit: beta=30--32 local comparison、forward tests、fail-fast tests。

### Phase 6.4: Alternating/backward integration

1. carried prefix/suffix block validity。
2. backward boundary orientation。
3. end-of-backward-sweep rebuild。
4. alternating regression and stab-drift tests。

Exit: forward/alternating both finite and statistically consistent in safe region。

### Phase 6.5: Target validation

1. known beta=33.325 seed short smoke。
2. `nwarm=2000,nmeas=9000,nbin=100,nrep=1` target。
3. `nrep=4` MPI target including known seed。
4. `make test`, `make test_mpi`, `make test_slow`, `git diff --check`。
5. profiler and memory report。

Exit: finite observables、sign consistency、residual/rcond diagnostics valid。

## 10. Acceptance Criteria

Phase 6 complete requires all of the following:

1. No single UDV chunk crosses configured max/spread gate。
2. No chain is collapsed through ordinary `udv_combine()` in chunked mode。
3. standalone solver passes dense, analytic overflow-scale, sign, residual,
   rcond, and chunking-invariance tests。
4. beta=30--32 results agree with two-sided within statistical tolerance。
5. known beta=33.325 seed completes with finite observables。
6. alternating and forward paths both pass。
7. numerical failure remains fail-fast with stage/chunk/rcond/residual context。
8. existing `combine` default and `two_sided` opt-in behavior remain unchanged。
9. memory and runtime are recorded; large-L limitation is not hidden。

## 11. Rollback and Compatibility

- Phase 6.0--6.2 は新しい standalone types/functions のみとし、既存 path を変更しない。
- parser は Phase 6.3 まで `chunked` を受け付けない。
- integration 後も default は `combine`、現行 `two_sided` は意味を維持する。
- augmented solver が stop gate を満たさなければ、representation/tests は残しても
  DQMC dispatch は追加しない。

## 12. Immediate Next Action

Phase 6.0 と 6.1 だけを次の実装対象とする。

最初の commit:

```text
test/linalg: add bounded UDV chain representation
```

次の commit:

```text
feat(linalg): prototype augmented UDV chain Green solver
```

この2段階の数値レビュー後に、GreenStack 統合へ進むかを判断する。
