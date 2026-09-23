---
date: 2026-08-22
datetime: 2026-08-22 11:47 JST
model: Codex (GPT-5)
status: plan
topic: equal-time transverse spin structure factor Sperp(q)
summary: |
  現行の Szz(q) と同じ equal-time Green 関数から、
  Sperp(q)=(S+-(q)+S-+(q))/2=Sxx(q)+Syy(q) を同時測定する。
  momentum plan の共有、sign 付き bin、MPI gather、独立 TSV、sum rule、paired SU(2)
  診断、serial/OpenMP/MPI/hybrid 回帰までを TDD で段階実装する。計画レビューのうち
  有効な指摘を反映し、有限 dtau ensemble の SU(2) に関する誤指摘は採用しない。
---

# Transverse Spin Structure Factor Implementation Plan

## Revision Note (2026-08-22 11:47 JST)

`docs/reviews/2026-08-22-sperp-structure-factor-plan-review.md` と、その再確認結果を
反映した。

- dense asymmetric Green、実空間 pair kernel、product state、sum rule を検証の主柱にし、
  U=0 relation は end-to-end 配線確認と位置づける。
- 同一 selector では immutable momentum plan を共有し、同時測定時の root memory
  増加を明記する。
- shell tests の Makefile 接続、Sperp 有効時の Szz byte 回帰、stdout metadata、
  cross-mode gate を明文化する。
- $S_\perp/2-S^{zz}$ は同一 bin から paired jackknife する opt-in 診断とする。
- spin-channel HS は configuration ごとには z 軸を選ぶが、HS 和は相互作用指数への
  厳密な演算子恒等式である。$K$ と $V$ も個別に SU(2) 不変なので、現行模型の
  Trotterized ensemble は有限 $\Delta\tau$ でも SU(2) 不変である。レビュー H-2 の
  「有限 $\Delta\tau$ で成分が系統的に分裂する」という主張は採用しない。
- 別ファイルの誤差を二乗和する方法は共分散の符号が未知で、保守的とは限らない。
  paired 診断を有効にしない場合、成分差の有意性は評価しない。

## 1. Goal and Convention

AF_QMC に等時刻の横スピン構造因子

$$
S_\perp(\mathbf q)
=\frac1N\sum_{i,j}e^{-i\mathbf q\cdot(\mathbf r_i-\mathbf r_j)}
\left\langle S_i^xS_j^x+S_i^yS_j^y\right\rangle
$$

を追加する。ladder operator は

$$
S_i^+=c^\dagger_{i\uparrow}c_{i\downarrow},\qquad
S_i^-=c^\dagger_{i\downarrow}c_{i\uparrow},\qquad
S^\pm=S^x\pm iS^y
$$

と定義し、出力規約を

$$
\boxed{
S_\perp(\mathbf q)
=S^{xx}(\mathbf q)+S^{yy}(\mathbf q)
=\frac12\left[S^{+-}(\mathbf q)+S^{-+}(\mathbf q)\right]
}
$$

に固定する。raw の $S^{+-}+S^{-+}$ は出力しない。固定場では $S^{+-}$ と
$S^{-+}$ は異なり得るが、$s\leftrightarrow-s$ で両者が交換され、weight は不変である。
従って対称化は規約を固定すると同時に symmetry-orbit averaging による分散低減になる。

現行の spin-rotation-invariant Hubbard 模型では ensemble expectation に対して

$$
S_\perp(\mathbf q)=2S^{zz}(\mathbf q)
$$

が任意の $\Delta\tau$ で成立する。すなわち $S_\perp/2$ と既存 $S^{zz}$ の比較で
SU(2) consistency を診断する。

## 2. Scope

### 2.1 Included

- equal-time、full（非 connected）の $S_\perp(\mathbf q)$。
- `chain` / `square`、PBC / OBC、`af|all|mx:my,...` selector。
- 既存 Szz と独立または同時の opt-in 測定。
- sign 再重み付き replica/bin 集約と jackknife error。
- serial、OpenMP、MPI、hybrid。
- 定義と規格化を明記した独立 long-form TSV。
- 同一 q plan で両成分を測る場合の paired SU(2) consistency TSV。
- all-q の局所モーメント sum rule。
- dense asymmetric oracle、product states、U=0、SU(2) 関係を使う検証。

### 2.2 Excluded

- imaginary-time-displaced transverse correlation と動的構造因子。
- $S^{+-}$ と $S^{-+}$ の独立ファイル出力。
- anomalous Green 関数、spin-orbit coupling、spin-mixing Hamiltonian。
- connected structure factor、任意実数 momentum、`lattice=file` の座標推測。
- 今回と独立な replica-blocked error estimator の改修。
- $S^{+-}$ と $S^{-+}$ の個別分散・covariance 出力。

## 3. Fixed-Auxiliary-Field Estimator

AF_QMC の規約は

$$
g^\sigma_{ij}=\langle c_{i\sigma}c^\dagger_{j\sigma}\rangle_s,
\qquad
\langle c^\dagger_{i\sigma}c_{j\sigma}\rangle_s
=\delta_{ij}-g^\sigma_{ji}
$$

である。固定した補助場では spin-flip 一体期待値がゼロなので、Wick 縮約から

$$
C^{+-}_{ij}(s)
=\langle S_i^+S_j^-\rangle_s
=(\delta_{ij}-g^\uparrow_{ji})g^\downarrow_{ij},
$$

$$
C^{-+}_{ij}(s)
=\langle S_i^-S_j^+\rangle_s
=(\delta_{ij}-g^\downarrow_{ji})g^\uparrow_{ij}
$$

を得る。実装する実空間 estimator は

$$
\boxed{
C^\perp_{ij}(s)
=\frac12\left[
(\delta_{ij}-g^\uparrow_{ji})g^\downarrow_{ij}
+(\delta_{ij}-g^\downarrow_{ji})g^\uparrow_{ij}
\right]
}
$$

および

$$
S_\perp(\mathbf q;s)
=\frac1N\sum_{i,j}
\cos[\mathbf q\cdot(\mathbf r_i-\mathbf r_j)]C^\perp_{ij}(s)
$$

である。$i\ne j$ では

$$
C^\perp_{ij}
=-\frac12\left(g^\uparrow_{ji}g^\downarrow_{ij}
+g^\downarrow_{ji}g^\uparrow_{ij}\right)
$$

となり、積の交換則から $C^\perp_{ij}=C^\perp_{ji}$ である。従って現行 Szz と同様に
cosine table のみで計算できる。新しい propagation、matrix inversion、spin-mixing
行列は導入しない。

### 3.1 Exact Checks and Their Detection Role

優先度の高い順に次を使う。

1. dense asymmetric $G_\uparrow\ne G_\downarrow$ の実空間 $C^\perp_{ij}$ と momentum
   出力が、独立な direct oracle に一致する。spin pairing、係数、$\delta_{ij}$、
   Green index を検出する主テストである。
2. 1 サイト 1 電子の任意の対角 product state は全 q で $S_\perp=1/2$。fully
   polarized と Néel の両方を固定 test にする。
3. all-q sum rule:

$$
\sum_{\mathbf q}S_\perp(\mathbf q)
=\sum_i\langle(S_i^x)^2+(S_i^y)^2\rangle
=\frac12\left(\langle N_e\rangle-2N\langle D\rangle\right).
$$

   この右辺は既存 Szz sum rule のちょうど 2 倍であり、係数、q 生成漏れ、MPI 受信欠落を
   検出する。
4. $G_\uparrow=G_\downarrow=\tfrac12I$ では全 q で $S_\perp=1/4$。
5. $G_\uparrow=G_\downarrow$ なら sample ごとに $S_\perp=2S^{zz}$。U=0 の
   DQMC Green を使う check は end-to-end 配線を検証するが、spin pairing の誤りは
   検出しない。

spin label swap 不変性と $q\leftrightarrow-q$ は式または cosine 実装から恒真なので、
physical property の documentation test とし、acceptance の独立根拠には数えない。

### 3.2 Configuration-Level and Ensemble-Level SU(2)

spin-channel HS の各 configuration は z 軸を選ぶため、sample ごとの Sperp と Szz は
一般に一致しない。一方、Hirsch 変換は

$$
e^{-\Delta\tau U n_{i\uparrow}n_{i\downarrow}}
=\frac12\sum_{s_i=\pm1}
e^{(\lambda s_i-\Delta\tau U/2)n_{i\uparrow}}
e^{(-\lambda s_i-\Delta\tau U/2)n_{i\downarrow}}
$$

という厳密な演算子恒等式である。現行模型の $K$ と $V$ はそれぞれ total spin と可換なので、

$$
\rho_{\Delta\tau}
=\left(e^{-\Delta\tau K}e^{-\Delta\tau V}\right)^{L_\tau}
$$

も有限 $\Delta\tau$ で SU(2) 不変である。従って完全な補助場 ensemble では q ごとに
$S_\perp=2S^{zz}$ が成立する。有限 $\Delta\tau$ は各成分に共通の Trotter error を与えて
ED 値を動かし得るが、成分間の系統的 splitting は作らない。

有限 sample での差は統計揺らぎ、autocorrelation、未収束、estimator/通信/数値実装の
診断量である。この区別を TSV header、usage、validation に同じ表現で記載する。

## 4. Input and Output Contract

### 4.1 New Parameters

`Params` に次を追加する。

```text
sperp_q=none
sperp_file=sperp.tsv
spin_consistency_file=none
```

- `sperp_q` は既存 `szz_q` と同じ `none|af|all|mx:my,...` 文法。
- `none` が既定で、既存 run の乱数列、stdout、Szz TSV を変えない。
- `sperp_q!=none` かつ `sperp_file=none` は setup error。
- Szz と Sperp は独立 selector を許す。同じ q で比較する production input では
  両方に同じ selector を明示する。
- `spin_consistency_file!=none` は Szz/Sperp の両方が enabled で、`nq,mx,my` の順序が
  一致するときだけ許す。これは同一 bin の paired difference を出力する opt-in 指定である。
- 同時有効時に `sperp_file` と `szz_file` が同一文字列なら、上書きを避けるため
  setup で reject する。consistency file と他 output の衝突も同様に検査する。
- file 衝突は v1 では文字列一致だけを検査する。`./a.tsv` と `a.tsv` の canonical path
  同一性までは判定しないことを usage に明記する。
- 空値、内部空白、255 byte 超、trailing junk、長すぎる物理行は既存 Szz と同じく
  fail-fast する。
- stdout の scalar column header/data rows は変更しない。Sperp enabled 時だけ既存 metadata
  行の末尾へ `sperp_file=<path> sperp_nq=<nq>` を追加し、paired 診断 enabled 時は
  `spin_consistency_file=<path>` も追加する。Szz-only metadata は byte-for-byte 保つ。

### 4.2 TSV Schema

`sperp.tsv` は既存 `szz.tsv` と同じ momentum metadata を持つ。

```text
beta_requested beta T q_index mx my qx_over_pi qy_over_pi
qx_folded_over_pi qy_folded_over_pi Sperp dSperp
```

header に少なくとも次を明記する。

```text
# definition=Sperp(q)=N^-1 sum_ij exp[-iq.(ri-rj)] <Sxi Sxj + Syi Syj>
# ladder_definition=Sperp(q)=0.5*(S+-(q)+S-+(q)); Splus=Sx+iSy
# su2_relation=ensemble Sperp(q)=2*Szz(q) at any dtau for the current SU(2)-invariant model
# hs_caveat=individual spin-channel-HS samples select the z axis; equality is restored by ensemble averaging
```

値の列名は `Sperp` / `dSperp` とし、`Sxx` や raw ladder sum と呼ばない。

`spin_consistency_file` は同じ momentum metadata と次の値を持つ。

```text
beta_requested beta T q_index mx my qx_over_pi qy_over_pi
qx_folded_over_pi qy_folded_over_pi DeltaSU2 dDeltaSU2
```

各 sign-reweighted bin $b$ で

$$
\Delta_{\mathrm{SU2},b}(\mathbf q)
=\frac12S_{\perp,b}(\mathbf q)-S^{zz}_b(\mathbf q)
$$

を先に作り、その同一 bin 配列を jackknife して `DeltaSU2` / `dDeltaSU2` を得る。
Szz と Sperp の別々の error を二乗和して差の error とすることは禁止する。共分散の符号は
一般に既知でなく、二乗和は過大・過小評価のどちらにもなり得る。

## 5. Internal Design

### 5.1 Share Momentum Infrastructure

`SzzPlan` / `SzzWorkspace` の内容は observable に依存しないため、先に
`StructureFactorPlan` / `StructureFactorWorkspace` へ機械的に一般化する。

- selector parser、momentum index、phase table、workspace allocation を一つに保つ。
- `szz_q` と `sperp_q` の selector 文字列が一致するときは、一つの immutable plan
  instance と phase table を共有する。ownership flag を一箇所に持ち、free は一度だけ行う。
- selector が異なる場合は独立 plan を持つ。workspace/correlation buffer は plan 共有の有無に
  かかわらず observable ごとに保持する。
- `measure_szz_sample()` は generic plan/workspace を受ける形に変更する。
- `measure_sperp_sample()` を追加する。
- Szz の数値式、q ordering、TSV schema は変更しない。
- 一般化 commit と Sperp estimator commit を分け、regression の原因を分離する。

### 5.2 Replica Storage

`ReplicaResult` に可変長 q observable 用の小さい構造体を導入する。

```c
typedef struct {
    double *sum_sign_values;
    int nq;
} ReplicaQObservable;
```

`ReplicaResult` は `szz` と `sperp` を別々に保持する。allocation、finite validation、
sign 付き加算、bin ratio は共通 helper にする。

重要な更新規約:

- scalar、Szz、Sperp の全入力を先に検査する。
- いずれかが不正なら何も加算しない。
- 全検査後に一度だけ bin と両 observable を更新する。
- 一方だけ enabled の場合も NULL/nq 契約を厳格に検査する。
- disabled は `nq==0 && sum_sign_values==NULL`、enabled は
  `nq>0 && sum_sign_values!=NULL` とする invariant を一つの helper に集約する。

### 5.3 Replica Run

`dqmc_run_replica()` は Szz/Sperp の二つの plan pointer を受ける。同一 selector では同じ
read-only pointer を渡してよい。

- enabled component の workspace/sample buffer だけ確保する。
- 同じ `D.Gu.g` / `D.Gd.g` から同じ measurement sweep 上で両方を測る。
- 両方 enabled の場合は ordered $(i,j)$ loop を一度だけ走査し、`corr_disp_zz` と
  `corr_disp_perp` を同時に埋める。selector が異なっても実空間 buffer は q 非依存なので
  1 pass 化できる。Fourier contraction は各 plan に対して別々に行う。
- pair estimator kernel を直接 unit test 可能な境界に置き、Fourier 後には見えない
  `g_ij/g_ji` 転置誤りを実空間要素比較で検出する。
- 追加の RNG 呼び出しは行わない。
- failure path は両 workspace/sample/ReplicaResult を idempotent に解放する。
- profiler に `measure_sperp` を追加する。

### 5.4 MPI and Root Statistics

- Sperp numerator は Szz と独立の dynamic-width `MPI_Gatherv` とする。
- q 数が異なる場合を許すため、count/displacement も component ごとに計算する。
- root receive buffer を `NAN` で初期化し、全要素 finite を検査する。
- flat-bin ordering は既存どおり global replica id、次に local bin とする。
- 各 bin で sign ratio を作った後、all-q の Sperp sum rule を検査する。
- jackknife と TSV writer は generic q-observable helper を使い、label と column 名だけを
  component ごとに渡す。
- plan の q ordering が一致し consistency output が enabled なら、root 上の同一 flat-bin
  Szz/Sperp ratio から `DeltaSU2` を作り paired jackknife する。追加の MPI payload は不要。
- file open/close failure は collective-safe に全 rank へ伝播する。

### 5.5 Memory Contract

- phase table は plan ごとに `8*N*nq` bytes。selector 一致時は Szz/Sperp で共有する。
- correlation workspace は observable ごとに `8*N` bytes。
- MPI/hybrid root の gathered numerator と q-major bin ratio は、各 observable について概ね
  `16*nrep*nbin*nq` bytes。両方を all-q で測るとこの部分は原理的に 2 倍になる。
- 32x32、`nrep=120`、`nbin=100` では root vector は約197 MB/component、両方で約394 MB。
- 48x48 all-q phase table は約42 MB/plan。selector 共有なら約42 MB/rank、共有しなければ
  約85 MB/rank。64x64では共有時約134 MB/rank、非共有時約268 MB/rank。
- 大規模格子で selector が異なる場合は、一方を selected q にする運用を usage に書く。

## 6. TDD Task Sequence

### Task 0: Baseline and Worktree Protection

Files: none（temporary baseline only）

1. `git status --short` と `git diff -- LOG.md` を保存する。
2. user-owned の `LOG.md`、基礎ノート、`jobs/` を reset/cleanup しない。
3. Szz 実装を含む local base commit を確認し、`feat/sperp-structure-factor` branch を作る。
4. `make test` を実行し、Szz enabled/disabled の代表出力を temporary directory に保存する。
5. toolchain が利用可能なら `make test_omp test_mpi test_hybrid` も baseline 化する。

Gate: 既存 failure があれば新機能の regression と分離して記録できること。

### Task 1: Generalize Momentum Plan Without Physics Changes

Files:

- Modify: `src/structure_factor.h`, `src/structure_factor.c`
- Modify: Szz plan/workspace を参照する source と tests

Steps:

1. existing Szz tests をそのまま oracle として `StructureFactorPlan/Workspace` へ移行する。
2. selector/phase/workspace API を observable-neutral にする。
3. selector 同一時の plan sharing と ownership cleanup を test する。
4. Szz-only input で出力を baseline と byte comparison する。

Gate:

```sh
make tests/test_structure_factor tests/test_integration
./tests/test_structure_factor
./tests/test_integration
make test test_omp test_mpi test_hybrid
```

Suggested commit: `refactor(measure): generalize spin momentum workspace`

### Task 2: Add the Sperp Physics Kernel

Files:

- Modify: `src/structure_factor.h`, `src/structure_factor.c`
- Modify: `tests/test_structure_factor.c`, `tests/test_integration.c`
- Modify: `tests/test_ph_symmetry.c`

Tests first:

1. test-local literal real-space pair oracle と literal `q,i,j` Fourier oracle。
2. deterministic dense non-symmetric $G_\uparrow\ne G_\downarrow$ について、production pair
   kernel の各 $C^\perp_{ij}$ と最終 q 出力を独立 oracle と比較する。これは spin pairing、
   係数、$\delta$、index reversal を検出する最優先 test であることをコメントに残す。
3. fully polarized と Néel を含む複数の 1-electron/site diagonal product state が全 q で
   $1/2$ を与えることを確認する。
4. onsite/all-q sum rule equals $\tfrac12(N_e-2ND)$。
5. $G_\uparrow=G_\downarrow=0.5I$ gives $1/4$ at every q。
6. $G_\uparrow=G_\downarrow$ gives sample-wise `Sperp == 2*Szz`。これは配線と係数の
   test であり、spin pairing は検出しないことをコメントする。
7. spin swap invariance and $q\leftrightarrow-q$ equality。これらは documentation test で
   独立な bug 検出力を持たないことをコメントする。
8. direct two-spin path and PH-mapped down Green agree where the existing PH oracle applies。
   これは Green mapping 一致の下流確認であり、独立な物理 oracle ではないと明記する。
9. NULL、size mismatch、non-finite input reject。

Implementation:

- directly testable pair kernel uses the boxed estimator and is the only production definition of
  $C^\perp_{ij}$。
- `measure_sperp_sample()` uses that kernel and the existing displacement/phase algorithm。
- both-enabled path fills Szz/Sperp displacement arrays in one ordered-pair loop。
- hot path has no allocation and no trigonometric call。
- raw `S+-` / `S-+` arrays are not stored。
- `sperp_sum_rule_check()` encodes the factor $1/2$ explicitly。

Gate: focused tests above pass under sanitizer configuration if the repository provides it。

Suggested commit: `feat(measure): add equal-time transverse spin estimator`

### Task 3: Input, Replica Bin, and Replica Run

Files:

- Modify: `src/io.h`, `src/io.c`, `tests/test_io.c`
- Modify: `src/replica.h`, `src/replica.c`, `tests/test_replica.c`
- Modify: `src/replica_run.h`, `src/replica_run.c`
- Modify: all direct `dqmc_run_replica()` test call sites
- Modify: `src/profiler.h`, `src/profiler.c`, `tests/test_profiler.c`

Steps:

1. add `sperp_q`、`sperp_file`、`spin_consistency_file` parser failures and defaults before fields。
2. introduce generic `ReplicaQObservable` and preserve transactional bin updates。
3. add Sperp workspace/sample lifecycle to replica run。
4. prove disabled mode makes no additional allocation or RNG call。
5. exercise Szz-only、Sperp-only、both-enabled combinations。
6. test the `nq/pointer` invariant through the shared helper and reject half-enabled states。

Gate:

```sh
make tests/test_io tests/test_replica tests/test_profiler
./tests/test_io
./tests/test_replica
./tests/test_profiler
make test
```

Suggested commit: `feat(replica): accumulate sign-weighted Sperp bins`

### Task 4: Serial/OpenMP Output

Files:

- Modify: `src/main.c`
- Modify: `Makefile`
- Add: `tests/test_sperp_output.sh`
- Add: a small U=0 sample input under `input/`

Steps:

1. initialize both plans、share identical selectors、and reject invalid file/consistency combinations
   before sampling。
2. open Sperp TSV only on root and propagate setup failure safely。
3. collect serial/OpenMP bin ratios and check the all-q sum rule per bin。
4. write the documented header and long-form rows at 17-digit precision。
5. when requested, form paired `DeltaSU2` bins and write the consistency TSV。
6. add `test_sperp` to `test:` and `.PHONY` in the same explicit style as `test_szz`。
7. compare serial and OpenMP fixed-seed output。
8. enforce three backward-compatibility cases with the same seed:
   - both disabled: full stdout byte-equals baseline and no new TSV exists。
   - Szz only: full stdout and `szz.tsv` byte-equal baseline。
   - both enabled: `szz.tsv` and scalar stdout header/data rows byte-equal Szz-only; only the
     documented metadata suffix and new files differ。

Gate:

```sh
make dqmc dqmc_omp
make test test_omp
```

Suggested commit: `feat(output): write transverse structure factor TSV`

### Task 5: MPI/Hybrid Gather

Files:

- Modify: `src/replica_mpi.h`, `src/replica_mpi.c`
- Modify: `src/main.c`
- Modify: `Makefile`
- Modify: `tests/test_mpi_replica.c`
- Add: `tests/test_sperp_parallel.sh`

Tests first:

1. pack ordering for Sperp with `nrep>1`, `nbin>1`, `nq>1`。
2. Szz/Sperp with different `nq` values。
3. zero-local-replica rank and disabled component。
4. `NAN` sentinel detects incomplete receive。
5. serial/MPI and OpenMP/hybrid fixed-seed Sperp TSV comparison。
6. one rank failing setup terminates collectively without hang。
7. matching q plans produce identical paired `DeltaSU2` rows in serial/MPI and OpenMP/hybrid。
8. add `test_sperp_parallel` to `test_hybrid:` and `.PHONY` in the same explicit style as
   `test_szz_parallel`。

Gate:

```sh
make dqmc_mpi dqmc_hybrid
make test_mpi test_hybrid
```

Suggested commit: `feat(mpi): gather transverse structure-factor bins`

### Task 6: Full Validation and Documentation

Files:

- Rename and expand: `docs/2026-08-21-szz-structure-factor-usage.md` to
  `docs/2026-08-22-spin-structure-factor-usage.md`
- Modify: every repository-local link to the renamed usage guide
- Add: `docs/2026-08-22-sperp-structure-factor-validation.md`
- Modify: `LOG.md`

Validation ladder:

1. all four build variants pass their default test suites, including both new shell-test targets。
2. the three disabled/Szz-only/both-enabled backward-compatibility cases pass。
3. dense asymmetric pair/momentum oracle and polarized/Néel analytic tests pass。
4. interacting 4x4 short run satisfies the Sperp all-q sum rule in every valid bin。
5. U=0 all-q run satisfies `Sperp(q)=2*Szz(q)` within numerical precision as an end-to-end
   wiring check; this is not treated as proof of spin pairing。
6. an interacting 4x4 run produces paired $\Delta_\mathrm{SU2}$ statistically consistent with zero。
   A nonzero finite-sample value is not a fail-fast invariant; report estimate, paired error, z score,
   autocorrelation/binning caveat, and sample count。
7. no $\Delta\tau^2$ splitting fit is performed: for the current model SU(2) forbids component
   splitting at every $\Delta\tau$。Absolute Szz/Sperp values still require the normal $\Delta\tau\to0$
   extrapolation before ED comparison。
8. same seed gives serial/MPI and OpenMP/hybrid agreement under existing tolerance。
9. profiler confirms one-pass joint measurement is small relative to sweep/Green rebuild cost。

No production/HPC job is part of implementation completion。必要な場合は、build artifact、
input、host、resource、walltime、run path、job count を別途提示し、ユーザーの新しい `GO`
を得てから投入する。

Suggested commit: `docs(spin): document and validate Sperp measurement`

## 7. Final Acceptance Criteria

- `sperp_q=none` is fully backward compatible。
- `sperp_q=af|all|list` works independently and simultaneously with Szz。
- output definition is exactly $S_\perp=(S^{+-}+S^{-+})/2=S^{xx}+S^{yy}$。
- no raw-sum factor-of-two ambiguity remains in code, tests, headers, or documentation。
- all-q sum rule uses $\tfrac12(N_e-2ND)$。
- dense asymmetric real-space and q-space outputs match an independent oracle。
- fully polarized、Néel、and a general diagonal 1-electron/site state give the analytic product-state
  value at every q。
- identical-q simultaneous measurement can output paired
  `DeltaSU2=Sperp/2-Szz` with a covariance-preserving jackknife error。
- U=0 verifies `Sperp == 2*Szz` at every q as an end-to-end wiring check。
- sign weighting、bin ordering、MPI gather、jackknife match existing Szz semantics。
- same-selector plan sharing and distinct-selector ownership both pass cleanup tests。
- enabling Sperp does not change Szz TSV or scalar data rows for the same seed。
- new shell tests are dependencies of the default serial/hybrid test targets。
- serial/OpenMP/MPI/hybrid tests pass。
- no additional propagation, matrix inversion, or RNG call is introduced。

## 8. Estimated Change Surface and Risk

Expected source/test touch points are roughly 12--16 files. The estimator itself is a small
$O(N^2+Nn_q)$ kernel; most work is safe plumbing and regression coverage。

Main risks and controls:

- factor-of-two convention: boxed definition、TSV header、analytic testsで固定する。
- spin pairing: dense asymmetric Green and product statesを主oracleにし、U=0だけに依存しない。
- Green index reversal: production pair kernelをFourier前に要素比較し、`g_ij` / `g_ji` を固定する。
- partial bin mutation: validate-all-then-commit を unit test する。
- MPI missing tail: `NAN` sentinel と全要素 finite check を維持する。
- cleanup regression: component pointersを block 冒頭で NULL 初期化する。
- accidental Szz change: generalizationを独立commitにし、baseline diffをgateにする。
- SU(2) interpretation: configuration-level anisotropyとensemble-level exact relationをheader、
  usage、validationで明確に分ける。
- difference error: separate errorsの二乗和を使わず、同一binのpaired jackknifeを使う。
- memory growth: same-selector planを共有し、不可避なroot vector倍増をusageに数値で明記する。

4x4 production では Green rebuild/sweep が支配的で、Sperp の計算時間と memory 増加は
小さい見込みである。大規模 all-q では §5.5 の memory contract を適用し、最終的な時間は
profiler の実測で判断する。
