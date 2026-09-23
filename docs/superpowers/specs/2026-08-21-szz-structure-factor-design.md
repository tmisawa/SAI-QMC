---
date: 2026-08-21
datetime: 2026-08-21 22:10 JST
model: Codex (GPT-5)
status: design
topic: equal-time longitudinal spin structure factor Szz(q)
summary: |
  等時刻 Green 関数から縦スピン相関を Wick 縮約し、chain/square の有限格子で
  S^{zz}(q) を測定する opt-in 機能を設計する。既存 stdout と既定実行を保ち、
  q ごとの sign 再重み付き bin・jackknife 結果を独立 TSV に出力する。設計レビューの
  High 5 件を反映し、入力完全性、負変位、MPI cleanup/Gatherv 検査を明文化した。
---

# Equal-Time Longitudinal Spin Structure Factor Design

## Revision Note (2026-08-21 21:31 JST)

`docs/reviews/2026-08-21-szz-structure-factor-design-plan-review.md` を反映した。

- H-1: `szz_q` の長さ・空値・内部空白・長行を manual parser で厳格検査する。
- H-2: `dqmc_run_replica()` の全 5 call sites を同一 task で更新する。
- H-3: MPI cleanup 対象ポインタは block 冒頭で `NULL` 初期化する。
- H-4: 負の C remainder を使わず、条件加算で変位を canonical 化する。
- H-5: MPI 受信バッファを `NAN` sentinel で初期化し、all-q では bin ごとの
  局所モーメント sum rule も検査する。物理的に正当な全ゼロは reject しない。
- 出力精度、effective beta、folded momentum、U=0 独立 oracle、bin-width 検証、
  collective-safe file open も出力・検証契約へ追加した。
- serial/OpenMP を Milestone 1、MPI/hybrid を Milestone 2 とする。

## 1. Goal

AF_QMC に、等時刻の縦スピン構造因子

$$
S^{zz}(\mathbf q)
=\frac{1}{N}\sum_{i,j}
e^{-i\mathbf q\cdot(\mathbf r_i-\mathbf r_j)}
\langle S_i^z S_j^z\rangle,
\qquad
S_i^z=\frac12(n_{i\uparrow}-n_{i\downarrow})
$$

を測定する機能を追加する。

設計目標:

- `chain` と `square` の任意の離散 Fourier 点を測定できる。
- 反強磁性波数だけを低コストで測る使い方と、全 Brillouin zone を測る使い方の
  両方を提供する。
- 現行の Green 関数規約、sign 再重み付け、replica/bin/jackknife 統計に従う。
- PH 対称性高速化と two-spin 経路で同一の測定コードを使う。
- 最終到達点は serial、OpenMP、MPI、hybrid の全実行形態とする。実装は
  serial/OpenMP の Milestone 1 と MPI/hybrid の Milestone 2 に分ける。
- 機能を無効にした既存 run の乱数列、標準出力、測定演算順を変えない。

## 2. Current State

現行の測定経路は次の scalar observables のみを保持する。

```text
MeasSample: E, ekin, eint, ntot, doublon
ReplicaBin: Ehub, Egc, Eph, N, D, sign, acceptance の集計量
stdout:     上記 scalar observables の平均と誤差
```

関連箇所:

- `src/measure.c`: 等時刻 Green 関数から scalar observables を計算。
- `src/replica_run.c`: sweep ごとの測定と replica bin への加算。
- `src/replica.c`: sign 付き numerator と denominator の保持。
- `src/replica_mpi.c`: 固定幅 8 double の bin pack/unpack。
- `src/main.c`: 全 replica の bin を global replica id 順に連結し jackknife。

PH 経路では `dqmc_sweep()` の境界再構築時に `dqmc_map_ph_down()` が呼ばれ、
測定前の `D.Gd.g` は

$$
G_\downarrow=I-PG_\uparrow^T P
$$

で構成済みである。したがって、測定関数は常に `G_up` と `G_down` を入力とし、
PH 専用の物理式を持たない。詳細は
`docs/2026-07-02-ph-symmetry-spin-correlations-note.md` を参照する。

## 3. Scope

### 3.1 Included in v1

- equal-time の full（非 connected）$S^{zz}(\mathbf q)$。
- `lattice=chain` と `lattice=square`。
- 単位格子間隔の整数座標。
- 離散 Fourier grid
  $q_x=2\pi m_x/L_x$, $q_y=2\pi m_y/L_y$。
- `af`、`all`、明示した momentum-index list。
- periodic/open boundary。open boundary の $\mathbf q$ は並進対称性の量子数ではなく、
  指定した座標埋め込み上の離散 Fourier サンプルと解釈する。
- sign 付き bin 集約、replica 連結、jackknife error。
- serial/OpenMP（Milestone 1）、MPI/hybrid（Milestone 2）。
- 独立した long-form TSV 出力。

### 3.2 Not included in v1

- $3S^{zz}(\mathbf q)$ や $S^{xx}+S^{yy}+S^{zz}$ の自動出力。
- connected structure factor。
- imaginary-time-displaced correlation、動的構造因子、解析接続。
- 任意の実数 $\mathbf q$。
- multi-orbital/unit-cell form factor。
- `lattice=file` の momentum measurement。現行 hopping file はサイト座標を
  持たないため、$\mathbf q\cdot\mathbf r_i$ を一意に定義できない。
- FFT 外部依存の導入。

SU(2) 対称な模型で慣例的に用いる $S(\mathbf q)=3S^{zz}(\mathbf q)$ は、
v1 出力を 3 倍すれば得られる。ただしユーザー要求は $S^{zz}$ なので、出力値に
factor 3 は入れない。

## 4. Physical Convention and Estimator

### 4.1 Source Convention

大塚博士論文の式 (2.36) と同じ $1/N$ normalization を使う。

$$
S(k)=\frac1N\sum_{i,j}e^{-ik\cdot(r_i-r_j)}
\langle S_{iz}S_{jz}\rangle.
$$

AF_QMC の Green 関数規約は

$$
g^\sigma_{ij}=\langle c_{i\sigma}c^\dagger_{j\sigma}\rangle_s,
\qquad
\langle c^\dagger_{i\sigma}c_{j\sigma}\rangle_s
=\delta_{ij}-g^\sigma_{ji}
$$

である。添字 $s$ は固定した補助場配置を表す。これは博士論文の式
(A.53)、(A.55)、(A.56) および現行 `src/measure.c` と一致する。

### 4.2 Fixed-Auxiliary-Field Wick Estimator

各 spin の局所密度を

$$
n^\sigma_i=1-g^\sigma_{ii}
$$

と置く。同一 spin は Wick 縮約により

$$
\langle n_{i\sigma}n_{j\sigma}\rangle_s
=n^\sigma_i n^\sigma_j
+(\delta_{ij}-g^\sigma_{ji})g^\sigma_{ij},
$$

異なる spin は固定補助場で factorize して

$$
\langle n_{i\uparrow}n_{j\downarrow}\rangle_s
=n^\uparrow_i n^\downarrow_j
$$

となる。従って実装する estimator は

$$
\boxed{
C^{zz}_{ij}(s)=\frac14\left[
(n^\uparrow_i-n^\downarrow_i)(n^\uparrow_j-n^\downarrow_j)
+(\delta_{ij}-g^\uparrow_{ji})g^\uparrow_{ij}
+(\delta_{ij}-g^\downarrow_{ji})g^\downarrow_{ij}
\right]
}
$$

および

$$
S^{zz}(\mathbf q;s)
=\frac1N\sum_{i,j}
\cos[\mathbf q\cdot(\mathbf r_i-\mathbf r_j)]C^{zz}_{ij}(s)
$$

である。$C^{zz}_{ij}=C^{zz}_{ji}$ なので imaginary part は相殺される。
実装は `cos` 部分だけを集計し、複素数型を導入しない。

### 4.3 Normalization Checks

この定義では次が成立する。

- 無相関な対角 Green $G_\uparrow=G_\downarrow=\tfrac12I$
  （$\beta\to0$ 相当）: $S^{zz}(\mathbf q)=1/8$。
- 全サイト up の product state:
  $S^{zz}(0)=N/4$、その他の離散 $\mathbf q$ は 0。
- Néel product state:
  $S^{zz}(\mathbf Q_\mathrm{AF})=N/4$、$S^{zz}(0)=0$。
- 全 momentum を測る場合の sum rule:

$$
\sum_{\mathbf q}S^{zz}(\mathbf q)
=\sum_i\langle(S_i^z)^2\rangle
=\frac14\left(\langle N_e\rangle-2N\langle D\rangle\right),
$$

  ここで `doublon` は現行出力と同じ site average である。

反強磁性 order parameter のサイズ解析で使う量は
$S^{zz}(\mathbf Q_\mathrm{AF})/N$ であり、v1 TSV の `Szz` をさらに `N` で
割って得る。二重 normalization を避けるため、v1 は派生列を自動出力しない。

## 5. Momentum Specification

### 5.1 Site Coordinates

`Lattice` に regular-lattice metadata を追加する。

```c
typedef struct {
    int n;
    double *t;
    int *bipart;
    int is_bipartite;
    LatType type;       /* LAT_CHAIN, LAT_SQUARE, LAT_FILE */
    int Lx;
    int Ly;
    int has_coordinates;
} Lattice;
```

cleanup 後の neutral state を明示するため、実装 enum には `LAT_NONE` も持たせる。
regular constructors は `LAT_CHAIN`/`LAT_SQUARE`、file constructor は `LAT_FILE`、
`lattice_free()` は `LAT_NONE` へ戻す。

座標は site ordering から決まるため、v1 では site ごとの配列を持たない。

```text
chain:  i=x,          r_i=(x,0), 0<=x<Lx, Ly=1
square: i=x+y*Lx,     r_i=(x,y), 0<=x<Lx, 0<=y<Ly
file:   coordinates unavailable
```

`lattice=file` を将来対応するときは、hopping file と独立の coordinate file を
追加する。`Lx`/`Ly` の値だけから file lattice の site ordering を推測しない。

### 5.2 Momentum Selectors

`Params` に次を追加する。

```c
char szz_q[256];
char szz_file[256];
```

既定値:

```text
szz_q=none
szz_file=szz.tsv
```

指定例:

```text
# 2D AF point q=(pi,pi)
szz_q=af
szz_file=data/szz_af.tsv

# full finite-size Fourier grid
szz_q=all
szz_file=data/szz_all.tsv

# selected integer momentum indices
# q=(2*pi*mx/Lx, 2*pi*my/Ly)
szz_q=0:0,2:0,2:2
szz_file=data/szz_selected.tsv
```

Semantics:

- `none`: disabled。Szz 用の allocation、演算、file I/O を行わない。
- `af`: active な各方向で $q=\pi$。非縮退方向の長さが偶数であることを要求する。
  長さ 1 の方向は inactive として `m=0` を使う。chain では `(Lx/2,0)`、
  通常の square では `(Lx/2,Ly/2)`。
- `all`: `my` outer、`mx` inner で全 `Lx*Ly=N` 点を生成する。
- list: `mx:my` の comma-separated list。入力順を出力順として保持する。
- canonical index `0<=mx<Lx`, `0<=my<Ly` を要求する。
- chain では `my=0` を要求する。
- duplicate momentum は typo として reject する。
- Szz 有効時の `lattice=file` は setup error にする。
- 空の `szz_file` は parser error とし、Szz 有効時の `szz_file=none` も reject する。

`szz_q`/`szz_file` parser contract:

- `=` より後ろの値全体を manual に取り出し、外側の空白だけを trim する。
- 空値、255 文字を超える値、内部空白、trailing junk、512-byte input buffer に
  収まらない長行は fail-fast する。255 文字ちょうどの値は終端 NUL 用領域が
  あるため受理する。`%255s` の silent truncation は使わない。
- explicit list の上限は selector 全体で 255 文字。多数の点には文字数制限のない
  generator `all` を使う。
- blank/comment line だけを無視し、`szz_q=` を既定 `none` へ黙って戻さない。
- `szz_file` の内部空白は未対応なので、空白を含む path は v1 では使えない。
- `szz_file=none` の reject は意図的である。測定を無効化する指定は
  `szz_q=none` に一本化する。

明示 index を採用する理由は、入力 parser が key/value 形式であること、`pi` の
文字列解釈や degree/radian の曖昧さを避けること、有限 PBC 格子で許される点を
正確に表せることである。

## 6. Output Contract

既存 stdout の scalar 列は変更しない。Szz は root process が `szz_file` に
long-form TSV として出力する。

Header:

```text
# definition=Szz(q)=N^-1 sum_ij exp[-iq.(ri-rj)] <Szi Szj>
# spin_operator=Szi=(n_up-n_down)/2 factor3_applied=0
# lattice=square Lx=4 Ly=4 n=16 pbc=1 t=-1 U=4 mu=2 dtau=0.1
# szz_q=all seed=12345 parallel=mpi nrep=4 bins=80 nbeta=3
beta_requested\tbeta\tT\tq_index\tmx\tmy\tqx_over_pi\tqy_over_pi\tqx_folded_over_pi\tqy_folded_over_pi\tSzz\tdSzz
```

Data row example:

```text
4\t4\t0.25\t0\t2\t2\t1\t1\t1\t1\t1.2345678901234567\t0.012345678901234568
```

Rules:

- 起動時に root が `w` mode で一度だけ作成する。
- beta ごと、momentum selector の順に 1 row を書く。
- `beta_requested` は `p.beta_list[b]`、`beta=Ltr*dtau`、`T=1/beta`。
  effective `beta` と `T` は同じ値から作り、TSV 内部の整合を保つ。
- `q_index` は selector 内の 0-origin 順序。
- `qx_over_pi=2*mx/Lx`, `qy_over_pi=2*my/Ly` は $[0,2\pi)$ 表現。
- folded 列は `m>L/2` のとき `m-L` を使い、$(-\pi,\pi]$ 表現にする。
  長さ 1 の方向はすべて 0。
- 全 double 列は `%.17g` で出力する。index 列は decimal integer。
- `Szz` は各 bin の sign-reweighted ratio を replica id/bin id 順に連結して得る。
- `dSzz` は現行 scalar observables と同じ bin jackknife error。
- file open/write/close error、non-finite 値、zero-sign bin は fail-fast する。
- Szz 無効時はファイルを作らず、現行 stdout を byte-level で維持する。
- Szz 有効時だけ、stdout の最初の metadata comment に
  `szz_file=<path> szz_nq=<nq>` を追記してよい。data columns は変えない。

## 7. Data Flow and Ownership

```text
Params.szz_q
    |
    v
SzzPlan (main で1回構築、read-only)
    |  momentum indices + displacement phases
    |  shared safely by serial/OMP/MPI rank-local replicas
    v
dqmc_run_replica()
    |
    | each measured sweep
    +--> measure_sample()       -> scalar MeasSample
    +--> measure_szz_sample()   -> nq doubles
    |
    v
ReplicaResult
    +-- bins[nbin]                    existing scalar/sign accumulators
    +-- sum_sign_szz[nbin * nq]       new contiguous numerator storage
    |
    +--> serial/OMP: main reads results in global replica-id order
    +--> MPI: scalar pack + separate dynamic-width Szz Gatherv
    v
root: bin ratios -> jackknife per q -> szz.tsv
```

### 7.1 Immutable Plan

新しい `src/structure_factor.h/.c` に次を置く。

```c
typedef struct {
    int enabled;
    int n;
    int Lx;
    int Ly;
    int nq;
    int *mx;
    int *my;
    double *phase_cos; /* [disp + q*n] */
} SzzPlan;

int szz_plan_init(SzzPlan *plan, const Lattice *L, const char *selector);
void szz_plan_free(SzzPlan *plan);
```

`phase_cos[disp + q*n]` は

$$
\cos\left[2\pi\left(
\frac{m_x\Delta x}{L_x}+\frac{m_y\Delta y}{L_y}
\right)\right]
$$

を保持する。column-major project convention に合わせ、displacement を leading
dimension とする。plan は beta、replica、thread 間で read-only 共有する。

### 7.2 Replica-Local Workspace

thread safety のため scratch は plan に置かず replica ごとに確保する。

```c
typedef struct {
    double *corr_disp; /* n */
} SzzWorkspace;
```

`dqmc_run_replica()` の setup/finalize で一度だけ allocate/free し、測定 sweep ごとに
再利用する。sample vector は呼び出し側が `double *out` として持ち、workspace と
重複させない。hot loop 内で `malloc()` しない。

### 7.3 Replica Result

既存 scalar `ReplicaBin` の fixed layout は維持する。`ReplicaResult` に optional な
contiguous storage を追加する。

```c
typedef struct {
    ReplicaBin *bins;
    int nbin;
    double *sum_sign_szz; /* [q + nq*bin] */
    int nq;
    int replica_id;
    unsigned long long seed;
    int status;
} ReplicaResult;
```

新 API:

```c
int replica_result_enable_szz(ReplicaResult *result, int nq);
int replica_result_add_szz(ReplicaResult *result, int bin,
                           const double *szz, int nq, double sign);
int replica_result_szz_bin_values(const ReplicaResult *result, int bin,
                                  double *out, int nq);
int szz_bin_ratio(const double *num, int nq, double sum_sign, double *out);
```

`replica_result_alloc(result, nbin)` は既存 signature を保ち、Szz 有効時だけ
`replica_result_enable_szz()` を追加で呼ぶ。free は scalar/vector の両方を解放する。

vector API は `nq == result->nq` を検査し、shape mismatch を reject する。
serial/OpenMP の result と MPI root の raw gather は共通 `szz_bin_ratio()` だけで
ratio を作り、分母・finite・zero-sign の規約を二重実装しない。

runtime は scalar と Szz の全値を finite-check してから accumulator を変更する。
これにより、一方だけが bin に入る partial sample を作らない。

## 8. Measurement Algorithm

### 8.1 Direct Oracle

検証用 oracle は定義どおり `q,i,j` の三重 loop を使う。

```text
for q:
    for i:
        for j:
            Szz[q] += cos(q.(ri-rj)) * Czz(i,j) / N
```

cost は $O(N_qN^2)$。これは test only とし production path では使わない。

### 8.2 Production Displacement Accumulation

regular rectangular grid と離散 momentum index を利用し、まず同じ periodic
displacement の相関を集約する。

```text
zero corr_disp[0..N-1]
for i=(xi,yi):
    for j=(xj,yj):
        dx = xi-xj
        if dx < 0: dx += Lx
        dy = yi-yj
        if dy < 0: dy += Ly
        disp = dx + dy*Lx
        corr_disp[disp] += Czz(i,j)

for q:
    Szz[q] = dot(phase_cos[:,q], corr_disp[:]) / N
```

open boundary でも、選ぶ $q$ が $2\pi(m_x/L_x,m_y/L_y)$ なので、displacement を
modulo $L$ でまとめても位相は変わらず、元の double sum と厳密に同値である。
ここで C の `(xi-xj)%Lx` は使わない。C の負 remainder による負 index を避けるため、
$x_i-x_j\in[-(L_x-1),L_x-1]$ を利用した上記の条件加算を使う。

`corr_disp` は PBC では periodic displacement correlation と解釈できる。OBC では
Fourier sum の中間 accumulator としては厳密だが、対の多重度が変位依存なので、
そのまま実空間 $C(r)$ として出力・解釈しない。

Complexity:

```text
time:   O(N^2 + N*Nq)
memory: O(N*Nq) shared plan + O(N+Nq) per replica
```

`szz_q=af` は $O(N^2)$、`szz_q=all` も $O(N^2)$ であり、全 q に対して
$O(N^3)$ の測定を避ける。外部 FFT dependency は不要である。

## 9. Sign, Binning, and Error Bars

各 sample $x_s=S^{zz}(\mathbf q;s)$ は現行 observable と同じく

```text
numerator_bin[q] += sign_s * x_s[q]
denominator_bin   += sign_s
```

で集計する。bin estimate は

$$
x_{b,q}=\frac{\sum_{s\in b}\mathrm{sign}_s x_{s,q}}
               {\sum_{s\in b}\mathrm{sign}_s}
$$

とする。最終的に `nrep*nbin` 個の $x_{b,q}$ を global replica id、bin id の順に
並べ、q ごとに現行 `jackknife()` を呼ぶ。

現行 scope は半充填二部格子で sign=1 だが、Szz だけ arithmetic mean に固定しない。
将来の signful scope と scalar observables の統計規約を揃える。

## 10. Parallel and PH Behavior

### 10.1 PH/two-spin

- `measure_szz_sample()` は `D.Gu.g`, `D.Gd.g` のみを見る。
- `D.use_ph=1` では現行 `dqmc_map_ph_down()` が用意した `D.Gd.g` を使う。
- `D.use_ph=0` では直接更新・再構築した `D.Gd.g` を使う。
- measurement layer は `use_ph` を分岐条件にしない。
- 同一 field configuration で mapped down と direct down の Szz が一致するテストを
  追加する。

### 10.2 OpenMP

- `SzzPlan` は immutable なので共有可能。
- `SzzWorkspace` と `ReplicaResult.sum_sign_szz` は replica-local。
- file I/O は parallel region の外で root/main thread のみが行う。

### 10.3 MPI/hybrid

MPI/hybrid は serial/OpenMP 完了後の Milestone 2 とする。物理 estimator と
`ReplicaResult` を確立してから通信層だけを追加する。

既存 8-double scalar pack format は変更しない。Szz numerator は別の
dynamic-width buffer で gather する。

```c
void replica_mpi_pack_szz(const ReplicaResult *results, int local_nrep,
                          int nbin, int nq, double *values);
```

MPI layout は既存 `replica_mpi_gatherv_layout()` に `item_width=nq` を渡す。
呼び出し前に `size_t` で `count*nbin*nq` と displacement を検査し、MPI の
`int` count/displacement に収まらない構成は collective-safe に reject する。
root は `total_bins*nq` doubles を受信する。scalar bin から受け取った
`sum_sign` を denominator とし、Szz 専用 count/denominator は重複送信しない。

MPI cleanup invariants:

- Szz 用の全 pointer は既存 MPI beta block 冒頭の宣言群に置き、`NULL` 初期化する。
- block 中途の宣言を先行 `goto mpi_beta_cleanup` が飛び越す構造を作らない。
- allocation/file/root-finalization failure は既存 `mpi_any_failed()` の collective
  順序へ載せ、rank-local early return をしない。

Gatherv integrity:

- root の `all_szz` は `calloc()` の 0 ではなく、全要素を `NAN` で初期化する。
- `MPI_Gatherv` 後、期待する全 `total_bins*nq` 要素が finite に上書きされたことを
  検査する。受信欠落で未書き込み部分が残れば必ず検出する。
- `szz_q=all` では、gather 後の各 bin に対して
  $\sum_q S^{zz}_b(q)=\tfrac14(N_{e,b}-2ND_b)$ を
  `abs(diff) <= 1e-12 + 1e-10*max(1,abs(rhs))` で検査する。
- selected q では sum rule が使えないため sentinel/finite 検査と
  serial-vs-MPI fixed-seed comparison を通信検証とする。
- 全 Szz が 0 であること自体は物理的に正当なので fail condition にしない。
- 同じ故障を受け得る別 `MPI_Gatherv` checksum は integrity の唯一の根拠にしない。

global ordering は既存と同じ `replica_id -> bin_id -> q_id` とする。これにより
serial/OMP/MPI/hybrid の jackknife input order が一致する。

Szz plan は全 rank が同じ入力から構築し、その status を `mpi_any_failed()` で
同期する。startup 順序は lattice 構築・検証 → plan 構築 → plan status 同期 →
root stdout metadata（ここで `szz_nq` が確定済み）→ root `szz_file` open →
`setup_failed` 同期 → replica 実行とする。`close_outputs()` は Szz stream も受け取り、
root の file close failure を全 rank へ伝播する。

## 11. Profiler

`ProfRegion` に `PROF_MEASURE_SZZ`、CSV name に `measure_szz` を追加する。

- phase は `measurement`。
- `SzzWorkspace` allocation と plan creation は `setup` に含めるが、初回は専用 region
  を増やさない。
- feature disabled 時は timer call 自体を Szz branch 内に置き、hot-path overhead を
  発生させない。
- `szz_q=af` と `all` の実測 overhead を implementation validation に記録する。

## 12. Failure Policy

以下は silent fallback せず nonzero exit にする。

- malformed selector、範囲外/重複 momentum index。
- empty/overlong/truncated/internal-whitespace `szz_q`、長すぎる input line。
- `af` に必要な active lattice extent が odd。
- Szz enabled with `lattice=file`。
- size multiplication overflow または allocation failure。
- non-finite Green input、$C^{zz}_{ij}$、sample、bin numerator、bin ratio、jackknife output。
- zero sign denominator。
- `szz_file` open/write/close failure。
- MPI rank の Szz allocation/pack failure。
- MPI count/displacement overflow、受信後に残った `NAN` sentinel、all-q bin sum-rule
  failure。

error message には可能な範囲で `beta_index`, `replica`, `seed`, `bin`, `meas`,
`q index` を含める。

## 13. Backward Compatibility

`szz_q=none` を既定値とし、次を acceptance condition とする。

- `Params` の既定実行で Szz allocation と file creation がない。
- `dqmc_run_replica()` の measurement order は scalar 測定まで現行と同じ。
- Szz 測定は scalar sample の後に入り、RNG/field/Green を変更しない。
- 現行 stdout header/data は byte-for-byte identical。
- `ReplicaBin` と `REPLICA_MPI_BIN_DOUBLES=8` は維持する。
- 既存 input files は変更なしで通る。
- `make test`, `test_omp`, `test_mpi`, `test_hybrid` の既存 tests が通る。

## 14. Validation and Acceptance Criteria

### 14.1 Unit Physics

- uncorrelated half-filled diagonal Green: all q = 1/8。
- fully polarized product state: q=0 is N/4, other q zero。
- Néel product state: AF q is N/4, q=0 zero。
- optimized displacement estimator equals direct oracle for deterministic
  nontrivial `G_up/G_down` on chain and rectangular square。
- negative displacement `(xi-xj)=-1` maps to `Lx-1` without out-of-bounds access。
- deterministic non-symmetric dense Green でも $C^{zz}_{ij}=C^{zz}_{ji}$、かつ
  sine-weighted imaginary part が機械精度で 0。
- $S^{zz}(q)=S^{zz}(-q)$。cos 実装では恒真なので、上の symmetry/sine test を
  正当性の本検証とする。
- all-q sum rule agrees sample/bin ごとに onsite moment と `ntot/doublon` expression。

### 14.2 DQMC Physics

- U=0 finite-temperature chain/square: measured Szz agrees with the independent
  momentum-space formula
  $S^{zz}(q)=(2N)^{-1}\sum_k f_k(1-f_{k+q})$ and is seed independent。
- same auxiliary field: direct two-spin down Green and PH-mapped down Green give
  the same Szz for chain and square。
- small interacting chain/square: local exact diagonalization reference and DQMC
  agree after accounting for Trotter/statistical error。これは slow validation とする。

### 14.3 Statistics and Parallelism

- sign=-1 synthetic samples validate numerator/denominator handling。
- replica bin concatenation and q ordering are deterministic。
- scalar and Szz MPI buffers round-trip independently。
- MPI receive buffer の `NAN` sentinel が全要素で上書きされ、all-q bin sum rule が
  root でも成立する。
- fixed seeds with single-threaded BLAS give matching serial/OMP/MPI/hybrid Szz
  means and errors。
- a rank with zero local replicas participates without invalid buffer access。

### 14.4 Compatibility and Performance

- disabled run stdout diff is empty against pre-change baseline。
- enabled run leaves scalar output unchanged for the same seeds。
- profiler records separate `measure_szz` rows only when enabled。
- measured scaling is consistent with $O(N^2+NN_q)$; no per-sample allocation。
- `af` and `all` overhead for representative L=8/L=16 cases is recorded before
  production use。これは記録項目であり、採否 gate にはしない。
- representative AF case で bin を再結合し、`dSzz` が bin-width plateau に達する
  ことを確認する。未到達ならより長い `nmeas` が必要と利用文書に警告する。

## 15. Future Extensions

- `lattice=file` coordinate file and arbitrary real q。
- real-space $C^{zz}_{ij}$ / displacement correlation output。
- connected charge/spin structure factors。
- transverse correlations and SU(2) consistency checks。
- imaginary-time correlation and susceptibility。
- FFT backend。現行 direct displacement DFT が bottleneck と実測された場合のみ導入する。
- `phase_cos` の $N\times N_q$ memory が大サイズで問題になった場合、
  $O(L_x^2+L_y^2)$ の separable 1D cosine/sine table または on-the-fly phase へ替える。
