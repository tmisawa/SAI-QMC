---
date: 2026-07-03
datetime: 2026-07-03 18:13 JST
model: GPT-5 Codex
topic: acceptance rate 出力と shifted-discrete spin HST 導入計画
summary: |
  A-D の作業案を、現状実装済みの acceptance rate と、次に入れる
  shifted_spin HST / m_i=0 regression / staggered m_i 比較実験に分けて整理する。
  shifted_spin は refs/arXiv-1902.00321v1 の shifted-discrete HST spin channel
  の式に沿い、m_i=0 で現行 Hirsch spin HST に厳密に戻ることを採用条件にする。
---

# Acceptance Rate と Shifted Spin HST の作業メモ

## 背景

U/t=8 や低温側では、標準の real spin Hubbard-Stratonovich (HS) 場で
local flip の determinant ratio が大きく揺れ、acceptance が下がる可能性がある。
acceptance が低いだけなら必ずしも統計が悪いとは限らないが、energy / doublon /
staggered magnetization の autocorrelation time が長くなるなら、Markov chain の
効率が悪い。

`refs/arXiv-1902.00321v1/auxfield.tex` は shifted-discrete HST を提案しており、
real spin auxiliary field に適切な shift を入れると determinant ratio のノルム揺らぎが
小さくなり、acceptance ratio 改善が期待できる、という立場である。ここではまず
現在の標準 spin HST と exact に比較できる最小導入を考える。

## A. acceptance rate を測定・出力する

### 状態

実装済み。

### 定義

1 sweep 中に全ての時刻 slice `l` と全サイト `i` で local HS field flip を 1 回ずつ
提案する。acceptance rate は measurement sweep に限って

```text
acceptance = accepted_flips / attempted_flips
```

と定義する。warmup sweep の採択率は production output には混ぜない。

### 実装位置

- `Dqmc.accept_attempts`, `Dqmc.accept_accepted`
  - `src/dqmc.c` の forward/backward sweep で、local proposal ごとに attempt を 1 増やす。
  - Metropolis 判定を通過した場合だけ accepted を 1 増やす。
- `ReplicaBin.accept_attempts`, `ReplicaBin.accept_accepted`
  - `src/replica_run.c` で measurement sweep 前後の `Dqmc` counter 差分を bin に加える。
  - これにより warmup は除外される。
- MPI path
  - `src/replica_mpi.c` の bin pack/unpack に accepted/attempts を追加。
- stdout
  - 既存列の末尾に `acceptance dAcceptance` を追加。
  - `dAcceptance` は bin ごとの acceptance rate に対する jackknife error。
- summary.tsv driver
  - `scripts/*` の summary header と stdout 取り込みを末尾 2 列対応に更新済み。

### 出力形式

旧形式:

```text
T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign
```

新形式:

```text
T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign acceptance dAcceptance
```

既存列の順番は変えず、末尾追加だけにする。既存解析が先頭側の列名や列順を使っている場合の
影響を最小化するためである。

### 注意

- acceptance rate は sign 重み付き observable ではなく、Markov chain の診断量である。
- acceptance が高くても autocorrelation が短いとは限らない。
- D の比較では acceptance と autocorrelation time を両方見る。

## B. `hst_type=standard_spin / shifted_spin` の入力オプション

### 目的

現行の Hirsch spin HST を default のまま保ち、shifted-discrete spin HST を opt-in で
使えるようにする。

```text
hst_type=standard_spin   # default, current behavior
hst_type=shifted_spin    # shifted-discrete spin HST
hst_m_file=path/to/m.tsv # shifted_spin 用の site-dependent m_i
```

`hst_type` 未指定時は必ず `standard_spin` とし、既存入力の結果を変えない。

### standard_spin

現行実装の spin HST:

```text
cosh(lambda) = exp(dtau * U / 2)
v_up(s)      = +lambda s
v_down(s)    = -lambda s
N_sigma      = exp(-2 lambda sigma s) - 1
```

ここで `sigma=+1` が up, `sigma=-1` が down の規約に対応する。

### shifted_spin の式

shifted-discrete spin HST では、補助場を

```text
n_up - n_down - m_i
```

の揺らぎに結合させる。入力で与える自由パラメータは `m_i` であり、各サイトで
`alpha_i`, `mtilde_i`, `C_i` を以下から決める。

```text
cosh[alpha_i (1 - m_i)] cosh[alpha_i (1 + m_i)]
------------------------------------------------ = exp(dtau * U)
              cosh^2(alpha_i m_i)

mtilde_i = (1 / (2 dtau U))
           log( cosh[alpha_i (1 + m_i)] / cosh[alpha_i (1 - m_i)] )

C_i = exp(dtau U mtilde_i^2 / 2) cosh(alpha_i m_i)
```

`C_i` は全体規格化に相当し、observable の比では消える。ただし補助場に依存する
スカラー因子 `exp(-alpha_i s_i m_i)` は local update ratio に入れる必要がある。

1 slice, 1 site の one-body exponent は

```text
v_up(i,s)   = +alpha_i s - (dtau U / 2) (1 - 2 mtilde_i)
v_down(i,s) = -alpha_i s - (dtau U / 2) (1 + 2 mtilde_i)
scalar(i,s) = exp(-alpha_i s m_i)
```

local flip `s -> -s` の determinant ratio には、現行の Green ratio に加えて

```text
scalar_new / scalar_old = exp(2 alpha_i s m_i)
```

を掛ける。

### m_i=0 での既存実装への帰着

`m_i=0` では

```text
cosh^2(alpha_i) = exp(dtau U)
alpha_i = acosh(exp(dtau U / 2)) = lambda
mtilde_i = 0
scalar = 1
```

となり、現行の `standard_spin` と同一になる。これを C の必須テストにする。

### コード変更の方向

現行 `Field` は全サイト共通の `lambda` と `N_cache[spin][s]` を持つ。`shifted_spin`
では per-site パラメータが必要なので、以下のように拡張する。

- `Params`
  - `hst_type`
  - `hst_m_file`
- `Field`
  - `hst_type`
  - `alpha[n]`
  - `m[n]`
  - `mtilde[n]`
  - `scalar_ratio_cache[n][2]` または `field_scalar_ratio(i,s)`
  - `N_cache_site[n][2][2]`、または `field_N_i(f, i, sigma, s)`
- `green_build_B`, `green_build_Binv`
  - standard: 既存通り `exp(+-lambda s)` を使う。
  - shifted: `v_sigma(i,s)` から per-site diagonal factor を作る。
- `dqmc_sweep_forward/backward`
  - `field_N(...)` を site-aware にする。
  - local ratio `R = Ru * Rd` を `R *= field_scalar_ratio(f, i, s)` で補正する。

初期実装では exactness を優先し、`shifted_spin` は two-spin 経路で動かすのが安全。
現在の PH single-spin relation は standard spin HST に対して検証済みなので、
shifted で使う場合は別途 PH mapping の再導出とテストを必要条件にする。

### alpha_i の求解

`|m_i| < 1` を基本入力範囲にする。`m_i=0` では解析解 `lambda` を使う。
一般の `m_i` では Newton 法または bracket 付き bisection で `alpha_i > 0` を解く。

安全側の実装条件:

- 解が見つからない場合は input error。
- `alpha_i`, `mtilde_i`, all onsite factors が finite であることを検査。
- `m_i` file のサイト数が `L.n` と一致しない場合は input error。

## C. `shifted_spin` で `m_i=0` が既存結果と一致するテスト

### 目的

shifted HST の導入で物理量や Markov chain が変わっていないことを、`m_i=0` の極限で
厳密に保証する。これは B を入れる前に必須。

### テスト項目

1. パラメータ単体テスト
   - `m_i=0` で `alpha_i == lambda`
   - `mtilde_i == 0`
   - `scalar_ratio == 1`
   - `field_N_i(i, sigma, s)` が現行 `field_N(sigma, s)` と一致

2. B 行列テスト
   - 固定 HS field に対して `green_build_B` / `green_build_Binv` が
     `standard_spin` と `shifted_spin(m=0)` で要素ごとに一致。

3. DQMC trajectory テスト
   - 同じ seed、同じ入力、`standard_spin` と `shifted_spin(m=0)` を 10-20 sweep 回す。
   - HS field 配置、`G_up`, `G_down`, `sign`, acceptance counters が一致。
   - これが通ると、Metropolis ratio だけでなく RNG 消費順も一致していることを確認できる。

4. stdout regression
   - 小さい chain / square で同じ `beta_list` を回し、
     `E_hub`, `doublon`, `sign`, `acceptance` が印字精度内で一致。

5. MPI pack/unpack regression
   - shifted path の bins でも acceptance counts が MPI round-trip で保存されること。

### 採用条件

`m_i=0` の全テストが通るまで、`shifted_spin` を production job に使わない。

## D. staggered `m_i` を外部入力して U=8/低温で acceptance と autocorrelation を比較

### D の意味

D は「物理量を変えるための外場を入れる」のではなく、shifted-discrete HST の任意パラメータ
`m_i` を使って補助場の揺らぎを小さくし、Markov chain の効率を改善できるかを測る実験である。

典型的には 2D square の反強磁性パターン

```text
m_i = m0 * (-1)^(x_i + y_i)
```

をファイルで与える。U/t=8・低温では局所スピンが AF 的に偏りやすいため、
standard spin HST の `m_i=0` より determinant ratio の揺らぎが小さくなり、
acceptance や autocorrelation が改善する可能性がある。

### 外部入力形式案

`hst_m_file` は 1 サイト 1 行の TSV にする。

```text
# site  m
0       0.20
1      -0.20
2       0.20
...
```

または header なしで `m` だけ n 行でも読めるようにする。任意格子 `latfile` と同じ
site order を使う。2D square 用には補助 generator script を用意してもよい。

```text
python scripts/make_staggered_m.py --Lx 6 --Ly 6 --m0 0.2 > m_6x6_stag_0p2.tsv
```

### 比較するケース

まず小さく始める。

- lattice: 4x4, 6x6 square
- U/t: 8
- boundary: PP, APP
- dtau: 0.025
- beta: 低温代表点から開始。例: beta=20, 33.325, 50
- HST:
  - `standard_spin`
  - `shifted_spin` with `m0=0`
  - `shifted_spin` with staggered `m0=0.1, 0.2, 0.3`

`shifted_spin(m0=0)` は C の regression と同じ役割を持つため、比較表に常に含める。

### 何を測るか

1. acceptance
   - stdout の `acceptance dAcceptance`
   - replica / bin ごとの分布も見るとよい。

2. energy / doublon
   - shifted は exact HST なので、十分な統計では standard と一致する必要がある。
   - z-score で `|z|` を確認する。

3. autocorrelation
   - energy, doublon, 可能なら staggered magnetization の measurement time series から
     integrated autocorrelation time を推定する。

4. effective sample size
   - `N_eff ~= N_meas / (2 tau_int)` を目安に、同じ walltime あたりの有効サンプル数を比べる。

### autocorrelation を測るために必要な追加出力

現在の main output は beta ごとの jackknife summary であり、autocorrelation の直接推定には
粗すぎる。D を真面目にやるなら、以下のような optional trace 出力を追加する。

```text
measure_trace=path/to/trace.tsv
measure_trace_stride=1
```

trace columns:

```text
beta  T  replica_id  sweep  bin  E_hub  doublon  sign  acceptance_sweep
```

`acceptance_sweep` はその measurement sweep だけの accepted/attempts とする。
trace はファイルサイズが大きくなり得るため、default off にする。

### autocorrelation 推定

時系列 `x_t` に対して

```text
C(k) = average_t [(x_t - mean(x)) (x_{t+k} - mean(x))]
rho(k) = C(k) / C(0)
tau_int(W) = 1/2 + sum_{k=1..W} rho(k)
```

を計算する。window `W` は self-consistent window、または最初に `rho(k)` が負になる点で
打ち切る簡易法から始める。production 判定では bin size 依存性も見る。

### 判定基準

採用する shifted `m_i` は以下を満たす必要がある。

- energy / doublon が standard_spin と統計誤差内で一致する。
- acceptance が改善する。
- 主要 observable の `tau_int` が短くなる、または同じ walltime あたりの `N_eff` が増える。
- `m0` に対して結果が滑らかで、特定値だけの不安定な改善ではない。

### 注意

- `m_i` は任意パラメータなので、正しく式を入れれば物理量に bias は入らない。
  ただし scalar factor や static spin-dependent term を落とすと別問題になる。
- `shifted_spin` の PH single-spin 最適化は、standard spin HST と同じ写像が使えるとは
  限らない。最初は two-spin reference で正しさを確定する。
- D の目的は「低温 U=8 の sampling 効率改善」であり、B/C の correctness が先。

## 推奨順序

1. A: acceptance rate 出力。実装済み。
2. B: `hst_type` と `shifted_spin` の exact 実装。ただし初期は two-spin 経路。
3. C: `m_i=0` regression。ここを通すまで production には使わない。
4. D: staggered `m_i` 入力、trace 出力、U=8 低温で acceptance / autocorrelation 比較。
