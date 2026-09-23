---
date: 2026-08-21
datetime: 2026-08-21 21:01 JST
model: Claude Opus 5 (1M context)
status: review
topic: Szz(q) structure factor design and TDD plan review
target: |
  docs/superpowers/specs/2026-08-21-szz-structure-factor-design.md
  docs/superpowers/plans/2026-08-21-szz-structure-factor-implementation.md
reviewer: Claude Opus 5
summary: |
  等時刻 longitudinal spin structure factor S^{zz}(q) の設計書と TDD 実装計画の
  静的レビュー。物理 estimator・規格化・sum rule・PH 経路・displacement 集約の
  厳密同値性は全て導出および現ソースとの照合で確認でき、採用可。一方で
  入力パーサの無言切り捨て、シグネチャ変更で壊れる未列挙テスト、MPI cleanup の
  goto 越え宣言、負の modulo、Gatherv 整合性検査の欠落など High 5 件、
  Medium 10 件、Low 10 件を指摘する。MPI 統合 (Task 7) の分離も推奨する。
---

# Szz Structure Factor 設計・実装計画レビュー

- 日時: 2026-08-21 21:01 JST
- 使用AI: Claude Opus 5 (1M context)
- 要約: 設計書と TDD 計画の静的レビュー。物理・アルゴリズムの中核は正しく、
  現ソースとの整合も確認できた。実装着手前に修正すべき High 5 件を含む
  25 件の指摘と、段階分割の提案をまとめる。

## 1. レビュー範囲と方法

対象文書:

- `docs/superpowers/specs/2026-08-21-szz-structure-factor-design.md`
- `docs/superpowers/plans/2026-08-21-szz-structure-factor-implementation.md`

照合した実装:

- `src/measure.c`, `src/measure.h`
- `src/replica.c`, `src/replica.h`, `src/replica_run.c`
- `src/replica_mpi.c`, `src/replica_mpi.h`
- `src/main.c`
- `src/lattice.c`, `src/lattice.h`
- `src/io.c`, `src/io.h`
- `src/dqmc.c`, `src/profiler.c`, `src/profiler.h`
- `Makefile`
- `docs/2026-07-02-ph-symmetry-spin-correlations-note.md`
- `tests/test_ph_symmetry.c`, `tests/test_dqmc_alternating.c`,
  `tests/test_sign_regression_slow.c`

観点:

- 物理式 (Wick 縮約・規格化・sum rule) の導出が正しいか
- 設計の前提が現ソースの制御フロー・データ構造と一致しているか
- 後方互換の主張が実際に成立するか
- TDD 計画のタスク分割・ファイルリスト・gate に漏れが無いか
- 検証ラダーが「実装の再計算」ではなく独立検証になっているか

ビルド・テストは実行していない。**静的レビューのみ**。数値による確認は
行っていないため、§7 の未確認事項を参照すること。

## 2. 総評

**採用可。ただし着手前に High 5 件を計画へ反映すること。**

中核は堅い。特に次の 3 点は独立に導出し直して正しいことを確認した。

- boxed estimator の Wick 縮約
- 全 q 総和の局所モーメント sum rule が **sample ごとに厳密成立**すること
- displacement modulo 集約が OBC でも元の二重和と**厳密同値**であること

また設計が現ソースについて主張している前提（PH 経路で測定直前に `Gd` が
再構成済み、profiler が region 追加で既存出力を変えない、square の site
ordering が `i = x + y*Lx`、unknown key が fail-fast）は、いずれも
ソースで裏が取れた（§3）。

弱点は物理ではなく **統合層と入力層**に集中している。特に

- 入力パーサが explicit momentum list を無言で切り捨てうること (H-1)
- `dqmc_run_replica()` シグネチャ変更で壊れるテストが計画に無いこと (H-2)
- `main.c` の MPI cleanup が goto ベースであること (H-3)
- 負の modulo (H-4)
- Gatherv の整合性検査欠落 (H-5、`AGENTS.md` に実害の記録あり)

の 5 件は、いずれも「テストが緑でも本番で壊れる」種類なので、着手前に
計画へ書き込むべきである。

## 3. 検証できた設計主張

以下は設計書の主張をこちらで再導出・再確認し、**正しいと確認できた**もの。
実装時に疑って再調査する必要はない。

### 3.1 Estimator の Wick 縮約 (設計 §4.2)

$g_{ij}=\langle c_ic_j^\dagger\rangle$、$\langle c_i^\dagger c_j\rangle=\delta_{ij}-g_{ji}$
の下で

$$
\langle c^\dagger_ic_ic^\dagger_jc_j\rangle
=\langle c^\dagger_ic_i\rangle\langle c^\dagger_jc_j\rangle
+\langle c^\dagger_ic_j\rangle\langle c_ic^\dagger_j\rangle
=n_in_j+(\delta_{ij}-g_{ji})g_{ij}
$$

となり、設計の同一 spin 項と一致する。異 spin 項が固定補助場で factorize
するのも正しい。boxed 式は
`docs/2026-07-02-ph-symmetry-spin-correlations-note.md` の式とも一致する。

### 3.2 $C^{zz}_{ij}=C^{zz}_{ji}$ (設計 §4.2)

$i\ne j$ では $\delta_{ij}$ 項が消えて $-g_{ji}g_{ij}$ となり、
これは $i\leftrightarrow j$ で不変。$i=j$ は自明。よって虚部相殺の主張は正しく、
`cos` のみを集計してよい。**ただし検証テストが不足している（M-7）**。

### 3.3 Sum rule が sample ごとに厳密成立 (設計 §4.3)

$a=n^\uparrow_i$, $b=n^\downarrow_i$ とすると

$$
C^{zz}_{ii}=\frac14\left[(a-b)^2+a(1-a)+b(1-b)\right]=\frac14(a+b-2ab)
$$

であり、`src/measure.c` の `doublon` 推定量 $n^\uparrow_in^\downarrow_i$ および
`ntot` と**同一の量**から構成される。したがって

$$
\sum_{\mathbf q}S^{zz}(\mathbf q)=\frac14\left(N_\mathrm{tot}-2N D\right)
$$

は平均値レベルではなく **1 sample ごと、1 bin ごとに厳密**に成立する。
計画 Step 8.3 が bin ごとの差を見よと書いているのは正しい判断で、
実際には差は丸め誤差 (~1e-14 相対) しか出ないはずである。これは
非常に強い回帰検出器になるので、**slow test ではなく default suite に
入れる価値がある**。

### 3.4 Product state の規格化 (設計 §4.3)

- 全 up: $C^{zz}_{ij}=1/4$ (全 $i,j$)、$S^{zz}(0)=N/4$、他 0。
- Néel: $C^{zz}_{ij}=\varepsilon_i\varepsilon_j/4$、$S^{zz}(Q_\mathrm{AF})=N/4$、
  $S^{zz}(0)=0$（偶数格子）。
- $G=\tfrac12 I$: $C^{zz}_{ii}=1/8$、非対角 0、全 q で $1/8$。

いずれも再計算して一致した。

### 3.5 OBC での displacement 集約の厳密同値性 (設計 §8.2)

$q_x=2\pi m_x/L_x$ なので $\Delta x\to\Delta x\pm L_x$ で位相は $2\pi m_x$ しか
変わらず $\cos$ は不変。よって OBC でも modulo 集約は近似ではなく厳密。
**この主張は正しい**。ただし副作用として `corr_disp` 自体は OBC では
実空間 $C(r)$ として解釈できない（対ごとの多重度が変位に依存するため）。
将来 `corr_disp` を出力する場合の注意として明記すべき（L-8）。

### 3.6 PH 経路で測定直前の `Gd` が最新 (設計 §2, §10.1)

`src/dqmc.c` を追った結果、`dqmc_map_ph_down(D)` は

- `dqmc.c:476`（init 直後）
- `dqmc.c:741`（forward sweep の stab 境界）
- `dqmc.c:800`（forward sweep 末尾の無条件 rebuild）
- `dqmc.c:847`（`dqmc_backward_boundary`）
- `dqmc.c:941`（backward sweep 末尾）

で呼ばれる。forward/backward いずれも **sweep 末尾の rebuild が無条件で走る**
（`Ltr % stab != 0` でも実行される）ため、`replica_run.c:248` の測定時点で
`D.Gd.g` は常に最新。設計の主張は成立する。既に `measure_sample()` が
同じ `D.Gd.g` を使って正しいエネルギーを出している事実も傍証になる。

### 3.7 square の site ordering (設計 §5.1)

`src/lattice.c` の `lattice_square()` は `IDX(x,y) = x + y*Lx` を使う。
設計の座標規約と一致。chain は `i=x`、`Ly` 不使用。一致。

### 3.8 profiler region 追加は disabled 実行の出力を変えない (設計 §11)

`src/profiler.c:197` に `if (s.calls == 0) { continue; }` があり、
呼び出し 0 の region は CSV 行を出さない。`PROF_MEASURE_SZZ` を
`PROF_REGION_COUNT` の直前に追加すれば、無効実行の `profile.csv` は
byte 単位で不変。計画 §8 の「enabled のときだけ `measure_szz` 行が出る」は
成立する。

### 3.9 Makefile 変更が不要

`SRC = $(wildcard src/*.c)`、`ALL_TESTS = $(wildcard tests/test_*.c)`、
`LIBSRC = $(filter-out src/main.c,$(SRC))` なので、
`src/structure_factor.c` と `tests/test_structure_factor.c` は
serial/omp/mpi/hybrid の全 variant に自動で入る。`clean` も wildcard 由来なので
安全。**計画に「Makefile 変更不要」と明記しておくとよい**（L-6）。

### 3.10 既存入力ファイルの互換性

`src/io.c:234` で unknown key は fail-fast する。`szz_q` / `szz_file` を
追加しても既存入力（`input/1d_L4_U0.txt` 等 4 本）は影響を受けない。
逆に **新キーを書いた入力は旧バイナリで必ず失敗する**（silent ignore しない）
ので、これは望ましい挙動。

### 3.11 `af` の奇数長拒否は実際に必要

PBC 奇数鎖・奇数辺は `lattice_chain`/`lattice_square` が `is_bipartite=0` にし、
`main.c:429` が既に弾く。しかし **OBC 奇数鎖は `is_bipartite=1`** なので
main を通過する。設計 §5.2 の「`af` は非縮退方向の長さが偶数であることを要求」は
この経路のために実質的に必要。冗長ではない。

### 3.12 `jackknife()` の誤差は平均の標準誤差と厳密一致

$j_i-\mu=(\mu-x_i)/(N-1)$ より
$\mathrm{err}=\sqrt{\sum(x_i-\mu)^2/(N(N-1))}$。
Szz は bin ごとに比を作ってから平均するだけなので、scalar observables と
同じ統計規約で問題ない。

## 4. 指摘事項

### 4.1 High

#### H-1: `szz_q` の explicit list が入力パーサで無言切り捨てされる

`src/io.c` の該当箇所:

```c
char line[512];          /* io.c:114 */
char val[256];           /* io.c:122 */
if (sscanf(line, "%63[^= \t]=%255s", key, val) != 2) {  /* io.c:123 */
    continue;
}
```

問題は 3 つある。

1. **255 文字で無言切り捨て**。`szz_q=0:0,2:0,2:2,...` が長くなると
   `%255s` が途中で打ち切る。切れ目がたまたま `,` の直前だと
   **文法的に妥当で要素数だけ少ないリスト**になり、`szz_plan_init()` は
   エラーを返せない。ユーザーは指定したはずの q が黙って消えたことに
   気付けない。実用上 40 momenta 程度が上限になる。
2. **空白で停止**。`szz_q=0:0, 2:0` のようにカンマ後に空白を入れると
   `%255s` は `0:0,` で止まる。これも「末尾カンマの malformed」になるか、
   運が悪いと妥当な短いリストになる。
3. **長行の残りが次行として再パースされる**。`fgets` は 511 文字までしか
   読まないので、超過分は次のループで別行として処理される。`=` を含まないため
   `sscanf != 2` で `continue` され、**無言で捨てられる**。

対策（いずれも計画 Task 1 に入れる）:

- `strlen(val) == 255` を切り捨て候補として **fail-fast** する。
- `szz_q` の値に空白が含まれた場合を検出できるよう、`szz_q` だけは
  `%255[^\n]` 系で読むか、少なくとも「空白不可」を仕様として明記して
  parser test に入れる。
- `szz_q=`（空値）は現状 `sscanf != 2` で行ごと無視され、既定の `none` の
  ままになる。これも仕様として明記するか reject する。
- ドキュメントに実用上の momentum 数上限を書く。

代替案として、`szz_q=all` が `af` と同じ $O(N^2)$ で全点を出せる以上、
**v1 では `none|af|all` の 3 択に絞り、explicit list を v1.1 に送る**のも
合理的である。list を残すなら `szz_q=file:<path>` 形式（1 行 1 momentum）を
併設する方が安全。

#### H-2: シグネチャ変更で壊れる呼び出し元が計画のファイルリストに無い

計画 Step 5.1 は

```c
int dqmc_run_replica(..., const SzzPlan *szz_plan, Profiler *prof,
                     ReplicaResult *result);
```

への変更を指示するが、Task 5 の Files に挙がっているのは
`tests/test_profiler.c` と `tests/test_ph_symmetry.c` である。実際の
呼び出し元は以下:

| 呼び出し元 | 行 | 計画に記載 |
| --- | --- | --- |
| `src/main.c` (`run_replica_range`) | 307 | あり (§8 本文) |
| `src/main.c` (omp) | 929 | あり |
| `src/main.c` (serial) | 941 | あり |
| `tests/test_dqmc_alternating.c` | 170 | **なし** |
| `tests/test_sign_regression_slow.c` | 55 | **なし** |

一方 `tests/test_profiler.c` と `tests/test_ph_symmetry.c` は
`dqmc_run_replica()` を呼んでいない（前者は region 名配列の整合、後者は
Szz 比較の追加という別の理由で変更対象）。

`test_sign_regression_slow.c` は `make test` から除外されているため、
`make test` が緑でも `make test_slow` でビルド不能になる。**計画 §9 Step 9.2 の
`make test_slow` まで到達して初めて発覚する**。Task 5 の Files に両ファイルを
追加し、gate に `make tests/test_dqmc_alternating tests/test_sign_regression_slow`
を含めること。

#### H-3: `main.c` の MPI ブロックは goto 越えの宣言を許さない

`src/main.c` の MPI beta ブロックは `goto mpi_beta_cleanup;`
（`main.c:710`, `735`, ...）を多用し、cleanup ラベルで全ポインタを `free()` する。
既存コードは**全ポインタをブロック先頭で `= NULL` 初期化**している
(`main.c:670` 付近)。

Szz 用の `local_szz`, `all_szz`, `szz_value_counts`, `szz_value_displs`,
`szz_bins` などを**ブロック途中で宣言すると、先行する goto がその初期化を
飛び越え**、cleanup での `free()` が不定値ポインタに対して行われる（UB）。
コンパイラは `-Wall -Wextra` でも必ずしも警告しない。

計画 Task 7 に「新規ポインタは既存宣言群と同じ位置に、必ず `NULL` 初期化で
追加する」と明記すること。

#### H-4: 変位の modulo が負になる

計画 Step 3.3 は「add `Czz_ij` to modulo displacement bin」としか書いていないが、
C の `%` は負のオペランドに対して負を返す。

```c
int dx = xi - xj;          /* -(Lx-1) .. (Lx-1) */
if (dx < 0) dx += Lx;      /* 必須 */
```

`(xi - xj) % Lx` をそのまま使うと `corr_disp[]` の範囲外書き込みになる。
`xi, xj ∈ [0,Lx)` が保証されているので上記の条件加算で足りる（二重 `%` は不要）。
設計 §8.2 の疑似コード `dx = (xi-xj mod Lx)` も、C の `%` と読み違えられない
表記に直すこと。テストとして「`dx=-1` に相当する対が `disp=Lx-1` に入る」ことを
直接確認する unit test を Step 3.2 に加える。

#### H-5: MPI Gatherv の整合性検査が無い

`AGENTS.md` の HPC メモに、kugui の
`openmpi_intel/4.1.5 + intel/2022.2.1 classic icc` で
**「`MPI_Gatherv` の double 受信バッファ後半が 0 のまま残る受信欠落」**を
standalone smoke test で確認済み、という記録がある。

scalar 経路はこの故障を偶然検出できる。受信欠落した bin は
`sum_sign == 0` になり、`replica_bin_values()` が失敗して
`main.c` の "invalid bin at beta=..." で落ちるためである。

**Szz 経路にはこの保護が無い**。`sum_sign_szz` が 0 であることは
物理的に正当な値（例えば Néel 状態での $S^{zz}(0)$）であり、
「受信欠落」と「本当に 0」を区別できない。しかも Szz バッファは
scalar の `nq` 倍のサイズなので、この故障モードに対する露出は増える。

対策（Task 7 に追加）:

- root 側で「gather された Szz を使った sum rule」
  $\sum_q S^{zz}(q) \stackrel{?}{=} \tfrac14(N_\mathrm{tot}-2ND)$ を
  **bin ごと**に検査する（`szz_q=all` のとき）。scalar 側から独立に来る
  `sum_sign_N` / `sum_sign_D` と突き合わせるので、受信欠落を確実に捕まえる。
- `all` 以外では、各 bin の Szz numerator に対する簡単な checksum
  （例: rank 側で計算した $\sum_q |x_{b,q}|$）を scalar pack の
  余剰スロットではなく**別の小さな Gatherv** で送り、root で照合する。
- 少なくとも「全 bin の Szz が厳密に 0.0」という状態は fail-fast する。

これは過剰防衛ではなく、このプロジェクトで**実際に起きた故障**への対処である。

### 4.2 Medium

#### M-1: `replica_mpi_gatherv_layout()` の int overflow が未検査

```c
recvcounts[rank] = count * nbin * item_width;   /* replica_mpi.c:64 */
```

`int` 演算で、`item_width = REPLICA_MPI_BIN_DOUBLES = 8` の現状では実用上
到達しない。しかし `item_width = nq` を渡すと、`nq = N` (`all`) の場合
`count * nbin * N` となり桁が 2〜3 桁上がる。例えば
`nrep=1024, nbin=100, nq=1024` で `total = 1.05e8`（まだ int 内）だが、
`nq=4096` (L=64 square) なら `4.2e8`、rank 数が少なければ 1 rank 分だけで
`int` を超えうる。

計画 Step 7.2 は「checked sizes before allocation/MPI `int` count conversion」と
書いているが、**`replica_mpi_gatherv_layout()` 自身の乗算**には触れていない。
この関数に overflow 検査と失敗返却（現在は `void`）を入れるか、
呼び出し前に `size_t` で検査して弾く方針を明記すること。

#### M-2: bin ratio の計算が 2 箇所に重複する

設計 §7.3 は `replica_result_szz_bin_values()` を用意する一方、
§10.3 で「root は gathered raw buffer を直接使ってよい」としている。
結果として

- serial/OMP: `ReplicaResult` 経由のヘルパ
- MPI: root で raw buffer に対する inline 除算

という**2 実装**になる。分母の選び方、`sum_sign == 0` の扱い、
finite 検査の順序がずれると、計画 §13 Stop Conditions の
「MPI ordering / 値が serial と一致すること」が静かに破れる。

共通の下位ヘルパ

```c
int szz_bin_ratio(const double *num, int nq, double sum_sign, double *out);
```

を `structure_factor.c` に置き、両経路がこれだけを呼ぶ形にすること。

#### M-3: `replica_result_add_szz()` に `nq` 引数が無く shape 検査ができない

設計 §7.3 の API:

```c
int replica_result_add_szz(ReplicaResult *result, int bin,
                           const double *szz, double sign);
```

計画 Step 4.1 は「out-of-range bin/q and **shape mismatch** reject」を
テスト項目に挙げるが、この signature では呼び出し側が渡す配列長を
検査する手段が無い（`result->nq` を信じるしかない）。
`int nq` を引数に足し、`nq != result->nq` を reject する契約にすること。
`replica_result_szz_bin_values()` も同様。

#### M-4: TSV の数値フォーマットが未規定

設計 §6 のデータ行例は

```text
4  0.25  2  2  1  1  1.23456789  0.0123
```

だが、`printf` 書式が規定されていない。計画 Step 8.3 は **TSV から
sum rule を照合する**ことを求めているので、`%.8g` 相当では
$\sum_q S^{zz}(q)$ と $\tfrac14(N-2ND)$ の一致を丸め誤差レベルで
確認できない。

`beta`/`T` および `Szz`/`dSzz` は `%.17g`（既存の `replicas.csv`,
`stab_drift_file` と同じ）にすること。`qx_over_pi`/`qy_over_pi` は
`%.17g` か、`mx`/`my` から再計算可能なので `%g` でもよい。

#### M-5: `beta` 列が要求値か `beta_eff` か未規定

`src/main.c` は

- `beta = p.beta_list[b]`（要求値）
- `beta_eff = Ltr * p.dtau`、`T = 1.0 / beta_eff`

とし、`replicas.csv` には**要求値の `beta`** と **`beta_eff` 由来の `T`** を
書いている（`main.c` の replica log 出力）。`beta/dtau` が整数から
1e-9 以内であることは検査されるが、両者は最下位桁で一致しない。

Szz TSV は `beta` と `T` を両方持つので、どちらを書くか明記すること。
**推奨は `beta = Ltr*dtau` と `T = 1/(Ltr*dtau)` で内部整合を取る**こと
（`beta * T == 1` が TSV 上で成り立つ）。要求値を残したいなら
`beta_requested` 列を別に足す。

#### M-6: `qx_over_pi` が第一 Brillouin zone に折り畳まれていない

設計は canonical index `0 <= mx < Lx` を採用し、
`qx_over_pi = 2*mx/Lx` を出力する。したがって $m_x > L_x/2$ では
`qx_over_pi ∈ [1, 2)` になる（例: `Lx=4, mx=3` → `1.5`）。

文献の $S(\mathbf q)$ プロットは通常 $q\in(-\pi,\pi]$ に折り畳む。
また `Szz` は $q$ と $-q$ で同値なので、`all` の出力は半分が冗長である。

対策（どれか）:

- `qx_folded_over_pi`, `qy_folded_over_pi` 列を追加する
  （$m>L/2$ のとき $m-L$ を使う）。
- 少なくとも設計 §6 と利用ドキュメントに「出力は $[0,2\pi)$ 表現であり、
  第一 BZ 表示にするには $m>L/2$ で $m\to m-L$ とする」と明記する。

列を足す方が後処理スクリプトの取り違えを減らせる。

#### M-7: `Szz(q) = Szz(-q)` テストは実装上ほぼ恒真

設計 §14.1 と計画 Step 3.2 に `Szz(q)=Szz(-q)` があるが、production も
oracle (§8.1) も `cos` しか使わない。`cos` は $q\to-q$ で不変なので、
このテストは**指数関数表現に戻したときの虚部消失を一切検証していない**。
すなわち §4.2 の「$C^{zz}_{ij}=C^{zz}_{ji}$ なので虚部が相殺する」という
主張はどのテストでも守られていない。

追加すべきテスト:

- 決定論的で非対称な dense `G_up`/`G_down` に対し、
  $\sum_{ij}\sin[\mathbf q\cdot(\mathbf r_i-\mathbf r_j)]C^{zz}_{ij}=0$
  を直接確認する（機械精度）。
- ついでに $C^{zz}_{ij}-C^{zz}_{ji}=0$ も直接確認する。

これらが緑なら `cos` のみ実装の正当性が保証される。既存の
`Szz(q)=Szz(-q)` は残してよいが、恒真であることをテストのコメントに書く。

#### M-8: U=0 検証 oracle が「同じ式の再計算」になりかねない

設計 §14.2 は「U=0 chain: measured Szz equals the analytic one-body Green
result」とするが、これを「free Green を作って同じ Wick 式に入れる」と
実装すると、estimator のバグは検出できず配線しか検証しない。

独立な閉じた式を使うべきである。U=0 では $n^\uparrow_i=n^\downarrow_i$ なので
第 1 項が厳密に消え、運動量表示で

$$
\boxed{\;S^{zz}_{U=0}(\mathbf q)=\frac{1}{2N}\sum_{\mathbf k}
f_{\mathbf k}\left(1-f_{\mathbf k+\mathbf q}\right)\;}
$$

$$
f_{\mathbf k}=\frac{1}{1+e^{\beta(\varepsilon_{\mathbf k}-\mu)}},\qquad
\varepsilon_{\mathbf k}=2t(\cos k_x+\cos k_y)
$$

となる（chain は $k_y$ 項なし。`t = p.thop = -1` の符号規約に注意）。
$\beta\to0$ で $f=1/2$ を代入すると $1/8$ となり、設計 §4.3 の
第 1 チェックと整合する。これは**良い健全性確認**で、二つのチェックが
同じ極限で一致することを示す。

`U=0` では `field_init` の $\lambda=\mathrm{acosh}(e^{\Delta\tau U/2})=0$ なので
補助場が Green を変えず、seed 非依存になる。設計の主張は正しい。

#### M-9: Szz(Q_AF) の自己相関を考慮した bin 幅検証が計画に無い

既存の `nbin` / `nmeas` の既定値（20 / 2000）はエネルギーの自己相関に
合わせて調整されてきた。$S^{zz}(\mathbf Q_\mathrm{AF})$ は秩序変数の
2 乗に相当し、**エネルギーより自己相関時間が長いのが普通**である。
同じ bin 幅を流用すると `dSzz` を系統的に過小評価する。

Task 8 に「bin 幅走査（binning 解析）」を追加すること。具体的には
同一の測定列から `nbin` を変えて（あるいは bin を再結合して）
`dSzz` が plateau に達することを、代表 1 条件で確認し validation 文書に記録する。
plateau に達しない場合は、利用ドキュメントで
「Szz を使うときは E よりも長い `nmeas` が必要」と警告する。

#### M-10: `szz_file` の open は MPI 集団通信順序に載せる必要がある

`szz_file` の open は root のみ・失敗が非決定的なので、
ランク間で分岐すると **deadlock する**。既存コードには適切な受け皿がある:

- `main.c:554` で stdout metadata を出力
- その直後に `replica_fp` を root だけが open し `setup_failed` を立てる
- `if (mpi_any_failed(&mpi_env, setup_failed))` で全ランク同時に落ちる

Szz もこの `setup_failed` ブロックに乗せること。加えて:

- `close_outputs()` (`main.c:202`) に `FILE **szz_fp` を追加する。
- 初期化順序を **lattice → `szz_plan_init()` → stdout metadata printf →
  `szz_file` open** に固定する。設計 §6 が stdout metadata に
  `szz_nq=<nq>` を足すとしている以上、plan は printf より前に必要である。
- `szz_plan_init()` 自体は全ランクで決定論的に成功／失敗するので broadcast
  は不要だが、**堅牢性のため `mpi_any_failed()` を通す**ことを明記する
  （設計変更で非決定性が入ったときの deadlock を防ぐ）。

計画 Step 6.1 はこれらを部分的にしか書いていない。

### 4.3 Low

- **L-1: 「非相互作用」という表現が誤解を招く（設計 §4.3 第 1 項）。**
  $G=\tfrac12 I$ は $U=0$ ではなく**無相関極限（$\beta\to0$ / 原子極限）**である。
  $U=0$ の有限温度 chain では $G$ は非対角成分を持ち、$S^{zz}(q)$ は
  $1/8$ にならない（M-8 の式で与えられる）。
  「無相関な対角 Green（$\beta\to0$ 相当）」に書き換えること。

- **L-2: `Ly=1` の square で `af` がどうなるか未明文化。**
  設計 §5.2 は「square では `(Lx/2, Ly/2)`」と書くが、`Ly=1` では
  `(Lx/2, 0)` が正しい。「長さ 1 の方向は inactive とみなし $m=0$ を採る」
  という規則を本文に明記すること（計画 Step 2.1 には項目がある）。

- **L-3: `szz_file=none` の意味論が `replica_log=none` と非対称。**
  `replica_log=none` は「無効化」、`szz_file=none` は「エラー」。
  意図的なら利用ドキュメントで明示すること。

- **L-4: `SzzWorkspace.sample` が `measure_szz_sample(..., double *out)` と重複。**
  呼び出し側が `out` を渡すなら `sample` は不要。どちらかに寄せる。

- **L-5: TSV header の再現性情報が薄い。**
  `szz_q` selector 文字列、`seed`、`parallel`、`nbeta`、`thop`、`mu` が無い。
  少なくとも `szz_q` と `seed` は追加を推奨。データ列にも `q_index`
  （0..nq-1）があると後処理の join が安全になる。

- **L-6: 「Makefile 変更不要」を計画に明記する。**
  §3.9 の通り wildcard 収集なので追加作業は無い。読み手の不安を消すため
  Task 2 / Task 9 に 1 行入れる。

- **L-7: 性能に関する記述が過剰。**
  Szz は測定 1 回あたり $\sim 2N^2$ flop、sweep は $O(N^3L_\tau)$。
  L=16 square（$N=256$）で $L_\tau=200$ なら比は $\sim 4\times10^{-5}$。
  設計 §11 と §14.4 の「overhead を測って採否を決める」という重み付けは
  下げてよい。計測自体は残す価値があるが、gate にはしない。

- **L-8: `corr_disp` の実空間出力はほぼ無料。**
  production path で既に計算しているので、$C(r)$ 出力は追加コスト無しで
  得られる。設計 §15 に置くのは妥当だが、**PBC でのみ $C(r)$ と解釈でき、
  OBC では対の多重度が変位依存なので解釈できない**ことを付記すること。

- **L-9: `phase_cos` は `all` で $N\times N$ doubles。**
  L=16 で 512 KB、L=32 で 8 MB、L=48 で 42 MB。
  $\cos[2\pi(m_x\Delta x/L_x)]$ と $\sin$ の 1 次元表（$O(L_x+L_y)$）から
  加法定理で組めば表は不要になる（内側ループの積和が 1 回増えるだけ）。
  L≥32 を視野に入れるなら設計 §7.1 に代替案として記載しておくとよい。

- **L-10: `lattice_alloc()` は `calloc` の戻り値を検査していない（既存問題）。**
  Task 1 でこの関数に触れるので、ついでに直すかどうかを計画で明示的に
  「直さない（別スコープ）」と決めておくと、実装中の判断ブレが減る。

## 5. 計画構成への提案

### 5.1 MPI 統合 (Task 7) を別マイルストーンに分ける

現計画は 9 タスクで lattice / io / structure_factor / replica / replica_run /
profiler / main / replica_mpi を一度に触る。最もリスクが高いのは Task 7 で、
理由は

- `main.c` の MPI ブロックが goto ベースで、変更耐性が低い (H-3)
- Gatherv には**このプロジェクトで実害が出た故障モードの記録がある** (H-5)
- ローカルでは `mpirun -np 1` しか回せず（`make test_mpi` は `-np 1`）、
  複数ランクの検証は Step 7.4 の手動実行に依存する

一方 Task 1〜6 + 8 + 9 だけで **serial/OpenMP の完全に使える機能**が
出来上がる。ここで一度 commit / 検証 / 文書化を締め、MPI を次の
マイルストーンにする方が、

- 早く実際に $S^{zz}(\mathbf Q_\mathrm{AF})/N$ の物理を見られる
- MPI 経路を壊した場合の切り分けが容易
- Stop Condition の「MPI ordering が serial と一致」を、
  **確立済みの serial 実装**に対して検証できる

という利点がある。設計 §3.1 の「serial/OMP/MPI/hybrid を初回実装の対象にする」は
最終到達点としては維持しつつ、**リリース単位を 2 つに割る**ことを推奨する。

### 5.2 Sum rule テストを default suite の中心に据える

§3.3 の通り sum rule は sample ごとに丸め誤差レベルで厳密成立する。
これは

- estimator のバグ
- displacement 集約のバグ（H-4 の modulo 含む）
- momentum plan の生成漏れ・重複
- MPI 受信欠落 (H-5)

を一度に捕まえる。計画は Step 8.3 で「real runs の検証」として扱っているが、
**決定論的な `G_up`/`G_down` に対する unit test としても Step 3.2 に入れ、
かつ DQMC 統合レベルでも default suite に入れる**べきである。

### 5.3 Stop Conditions への追加

計画 §13 に以下を足す。

- `szz_q` の入力が黙って切り捨てられうる状態のまま先に進まない (H-1)。
- 全 q sum rule が bin ごとに相対 1e-10 を超えて破れたら停止する。
- MPI の Szz が全 bin で厳密 0.0 になったら受信欠落を疑って停止する (H-5)。

## 6. 追加を推奨するテスト一覧

計画に無く、追加を推奨するもの。

| # | 種別 | 内容 | 対応指摘 |
| --- | --- | --- | --- |
| T-1 | unit | 負の変位 (`xi < xj`) が `disp = Lx-1` 等に正しく入る | H-4 |
| T-2 | unit | 非対称な dense G に対し $\sum_{ij}\sin(\cdot)C_{ij}=0$ | M-7 |
| T-3 | unit | $C^{zz}_{ij}-C^{zz}_{ji}=0$（機械精度） | M-7, §3.2 |
| T-4 | unit | 決定論的 G に対する sum rule（$\sum_q S^{zz}=\sum_iC^{zz}_{ii}$） | §5.2 |
| T-5 | unit | `szz_q` が 255 文字境界で切り捨てられたとき fail する | H-1 |
| T-6 | unit | `szz_q` に空白が含まれるとき fail する | H-1 |
| T-7 | integration | U=0 chain/square を $\frac1{2N}\sum_k f_k(1-f_{k+q})$ と照合 | M-8 |
| T-8 | integration | DQMC 実行の bin ごと sum rule（default suite） | §5.2 |
| T-9 | build | `test_dqmc_alternating` / `test_sign_regression_slow` のビルド | H-2 |
| T-10 | manual | `nbin` 走査による `dSzz` の plateau 確認 | M-9 |

## 7. 未確認事項

本レビューは静的解析のみで、以下は検証していない。

- `make test` / `test_omp` / `test_mpi` / `test_hybrid` の現状の合否。
  計画 Step 0.3 の実行前確認は必須。
- `src/green.c` の `green_build_ph_down()` の実装詳細（`Gu.tmp` を経由する
  呼び出しがあるため、`dqmc_map_ph_down()` が `Gd.g` に書く経路と
  workspace が衝突しないこと自体は既存動作として前提にした）。
- 大サイズでの実測メモリ・実行時間（L-7, L-9 の見積もりは flop 数からの概算）。
- kugui / Genkai 上の挙動。`AGENTS.md` の規定によりリモート実行は
  ユーザーの明示承認が必要であり、本レビューでは一切行っていない。

## 8. 結論

- 物理・アルゴリズム・後方互換の中核設計は **正しく、そのまま採用してよい**。
- 着手前に **H-1〜H-5 を設計書と計画に反映**すること。特に H-1（入力の
  無言切り捨て）と H-5（Gatherv 整合性）は、テストが緑でも誤った物理値を
  静かに出す種類の欠陥である。
- Medium は実装中に順次取り込めばよいが、M-4（出力精度）と M-5（beta 列）は
  出力契約なので **Task 6 の実装前に確定**させること。
- Task 7 (MPI/hybrid) を第 2 マイルストーンに分離することを推奨する。
