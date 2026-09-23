---
date: 2026-08-22
datetime: 2026-08-22 11:30 JST
model: Claude Opus 5 (1M context)
status: review
topic: Sperp(q) transverse structure factor implementation plan review
target: docs/superpowers/plans/2026-08-22-sperp-structure-factor-implementation.md
reviewer: Claude Opus 5
summary: |
  横スピン構造因子 S_perp(q) の実装計画レビュー。estimator、規格化、sum rule、
  対称性は再導出と数値実験で正しいことを確認した。一方で、検証設計の重み付けに
  問題がある。U=0 の Sperp=2*Szz は spin pairing の誤りを一切検出できないことを
  数値で示す。さらに有限 dtau では spin-channel HS が SU(2) を破るため
  Sperp/2 と Szz は系統的にずれる。High 3 件、Medium 8 件、Low 6 件を指摘する。
---

# Sperp(q) 実装計画レビュー

- 日時: 2026-08-22 11:30 JST
- 使用AI: Claude Opus 5 (1M context)
- 要約: 物理式と規約は正しい。指摘の中心は「何を検証の柱に置くか」と
  「有限 Δτ で $S_\perp=2S^{zz}$ が成り立たないことをどう扱うか」。
  数値実験でテストの検出力を測定し、根拠付きで組み替えを提案する。

## 1. レビュー範囲と方法

対象:

- `docs/superpowers/plans/2026-08-22-sperp-structure-factor-implementation.md`

照合した実装・文書（merge 済み Szz 実装 `4ba47c5` 時点）:

- `src/structure_factor.{c,h}`, `src/replica.{c,h}`, `src/replica_run.{c,h}`,
  `src/replica_mpi.{c,h}`, `src/main.c`, `src/io.{c,h}`, `src/field.c`, `Makefile`
- `docs/2026-08-21-szz-structure-factor-{usage,validation}.md`
- `docs/2026-07-02-ph-symmetry-spin-correlations-note.md`
- `docs/reviews/2026-08-21-szz-structure-factor-{design-plan,implementation}-review.md`

**静的読解に加えて、estimator を独立に実装して数値実験を行った。**
具体的には、正しい $C^\perp_{ij}$ と、実装時に起こりうる 3 種類の誤り

- A: spin pairing の取り違え（同一 spin どうしを組む）
- D: 係数 $1/2$ の欠落（raw ladder sum を出す）
- E: 片方の項で $\delta_{ij}$ を落とす

を 4x4 格子上で並べ、計画が挙げる各検証状態がどれを検出できるかを測定した（§3.2）。
実装コードは無いので、これは**計画の検証設計に対する検出力測定**である。

## 2. 総評

**採用可。物理は正しい。** ただし着手前に High 3 件を計画へ反映すること。

estimator、$1/N$ 規格化、$C^\perp_{ij}=C^\perp_{ji}$、sum rule の係数、
product state の値は、いずれも独立に再導出して計画と一致した（§3.1）。
Task 分割、generalization を独立 commit にする方針、baseline gate、risk 表は
前回 2 本のレビュー指摘を踏まえており、計画の作りは良い。

問題は物理式ではなく**検証設計の重み付け**と**文書化**に集中している。

- §7 の acceptance criterion が「U=0 で $S_\perp=2S^{zz}$」に重みを置いているが、
  この条件は **spin pairing の誤りを原理的に検出できない**（§3.2 で実測）。
- TSV header に `# su2_relation=Sperp(q)=2*Szz(q)` を無条件で書く設計だが、
  この関係は**有限 Δτ では成り立たない**。原因は Hirsch spin-channel HS が
  補助場を $S^z$ に結合させることで、統計誤差ではなく系統誤差である（§4）。
- 両方 enable 時のメモリが倍増することが計画にない。usage 文書には既に
  Szz 単独の見積りが載っているので、書かないと不整合になる。

## 3. 検証できた計画の主張

### 3.1 再導出で一致した項目

**estimator。** $S^+_i=c^\dagger_{i\uparrow}c_{i\downarrow}$ の下で

$$
\langle c^\dagger_{i\uparrow}c_{i\downarrow}c^\dagger_{j\downarrow}c_{j\uparrow}\rangle_s
=\langle c^\dagger_{i\uparrow}c_{j\uparrow}\rangle_s
 \langle c_{i\downarrow}c^\dagger_{j\downarrow}\rangle_s
=(\delta_{ij}-g^\uparrow_{ji})g^\downarrow_{ij}
$$

となる。$c_{j\uparrow}$ を左へ移す並べ替えは 2 回の transposition なので
**符号は $+1$** であり、計画の式に符号の取りこぼしは無い。
`docs/2026-07-02-ph-symmetry-spin-correlations-note.md` の横成分の式とも一致する。

**対称性。** $i\ne j$ で
$C^\perp_{ij}=-\tfrac12(g^\uparrow_{ji}g^\downarrow_{ij}+g^\downarrow_{ji}g^\uparrow_{ij})$
となり $i\leftrightarrow j$ で不変。$i=j$ は自明。よって cosine table のみで
計算できるという主張は正しい（この 1 行が計画に無い → L-3）。

**sum rule。** $a=n^\uparrow_i$, $b=n^\downarrow_i$ とすると

$$
C^\perp_{ii}=\tfrac12\left[a(1-b)+b(1-a)\right]=\tfrac12(a+b-2ab),
\qquad
C^{zz}_{ii}=\tfrac14(a+b-2ab)
$$

なので $C^\perp_{ii}=2C^{zz}_{ii}$ が**厳密に**成り立ち、

$$
\sum_{\mathbf q}S_\perp(\mathbf q)=\tfrac12(N_e-2ND)
$$

は既存 Szz sum rule のちょうど 2 倍。しかも `src/measure.c` の
`ntot`/`doublon` と同一の量から構成されるため、**sample ごと・bin ごとに厳密**に
成立する。1 サイト 1 電子なら $\langle(S^x)^2+(S^y)^2\rangle=S(S+1)-S_z^2=1/2$ とも整合する。

**product state の値。** 再計算して計画と一致。

| 状態 | $S_\perp(\mathbf q)$ | $2S^{zz}(\mathbf q)$ |
| --- | --- | --- |
| $G_\uparrow=G_\downarrow=\tfrac12I$ | 全 q で $1/4$ | 全 q で $1/4$ |
| fully polarized up | 全 q で $1/2$ | $q=0$ で $N/2$、他 0 |
| Néel | 全 q で $1/2$ | $Q_\mathrm{AF}$ で $N/2$、$q=0$ で 0 |

より一般に、**1 サイト 1 電子の任意の対角 product state で
$S_\perp(\mathbf q)=1/2$（全 q）**になる。$S_\perp$ は古典スピン配列に完全に鈍感で、
$S^{zz}$ だけが配列を分解する。これは物理的に正しい（古典 Néel 状態は
横方向の**モーメント**は持つが横方向の**秩序**は持たない）。

**$S^{+-}$ と $S^{-+}$ の扱い。** 固定場では両者は異なるが、weight
$\det M_\uparrow\det M_\downarrow$ は $s\to-s$（up↔down 交換）で不変なので、
MC 平均では一致する。したがって $(S^{+-}+S^{-+})/2$ を測るのは規約の都合だけでなく、
**厳密な対称性による分散低減**でもある。計画の決定を支持する根拠なので §1 に書くとよい（L-5）。

### 3.2 各検証状態の検出力（数値実測）

正しい estimator と誤り 3 種を 4x4 格子で並べ、
$\max_q|S_\perp^\text{correct}(q)-S_\perp^\text{variant}(q)|$ を測った。

| 検証状態 | A: spin pairing 誤り | D: $1/2$ 欠落 | E: $\delta_{ij}$ 落ち |
| --- | ---: | ---: | ---: |
| **U=0 相当（$G_\uparrow=G_\downarrow$）** | **0.000e+00** | 2.520e-01 | 2.497e-01 |
| **$G=\tfrac12 I$** | **0.000e+00** | 2.500e-01 | 2.500e-01 |
| fully polarized | 5.000e-01 | 5.000e-01 | 5.000e-01 |
| Néel | 5.000e-01 | 5.000e-01 | 2.500e-01 |
| dense asymmetric $G_\uparrow\ne G_\downarrow$ | 1.102e-01 | 2.779e-01 | 1.881e-01 |

**$G_\uparrow=G_\downarrow$ になる 2 つの状態（U=0 と $\tfrac12I$）は、
spin pairing の誤りを一切検出しない。** 差が丸め誤差ですらなく厳密に 0 になる。
理由は単純で、$G_\uparrow=G_\downarrow$ のとき
$C^\perp_{ij}$ と「同一 spin どうしを組んだ式」が恒等的に一致するからである。

もう一つ確認した事実として、**$C^\perp_{ij}$ の 2 項のどちらか（あるいは両方）を
$i\leftrightarrow j$ 転置しても $S_\perp(q)$ は変わらない**（差 $\le 1.1\times10^{-16}$）。
$\sum_{ij}\cos[\mathbf q\cdot(\mathbf r_i-\mathbf r_j)]f(i,j)$ は
$f\to f^T$ で不変なので、**index 転置系の誤りは momentum 和の後では原理的に観測できない**。
これは「$g_{ij}$ / $g_{ji}$ の取り違え」を §8 の risk に挙げていることへの重要な補足で、
その risk は実空間 $C^\perp_{ij}$ の要素比較でしか捕まえられない。

## 4. 有限 Δτ における SU(2) の破れ

これは計画で最も扱いが弱い論点なので独立の節にする。

`src/field.c` は

```c
f->lambda = acosh(exp(dtau * U / 2.0));
f->N_cache[isigma][is] = exp(-2.0 * f->lambda * sigma * s) - 1.0;
```

すなわち **Hirsch の spin-channel（離散）HS 変換**を使っている。補助場は
$\sigma=\pm1$ すなわち $S^z$ に結合するため、**有限 Δτ の ensemble は SU(2) 不変ではない**。
結果として

$$
\left\langle S^{xx}\right\rangle=\left\langle S^{yy}\right\rangle\ne\left\langle S^{zz}\right\rangle
\quad\text{at finite }\Delta\tau
$$

となり、$S_\perp(\mathbf q)/2-S^{zz}(\mathbf q)$ は **統計誤差ではなく系統誤差**として残る。
本プロジェクトは既に Trotter 誤差が $O(\Delta\tau^2)$ であることを
`VALIDATION.md` §「E(dtau) = E(0) + c dtau^2」と `qmc_L6_U4_trotter_summary.dat` で
確立しているので、この差も同じ次数で消えると期待される（指数は実測で確認すること）。

一方、**on-site の恒等式 $C^\perp_{ii}=2C^{zz}_{ii}$ は Δτ に依らず厳密**なので、
$\sum_q S_\perp=2\sum_q S^{zz}$ は常に成り立つ。破れるのは q 分解された分布だけで、
q について足し戻すと相殺する。

計画への影響は 3 つある。

1. TSV header の `# su2_relation=Sperp(q)=2*Szz(q)` は**無条件の関係として誤読される**。
   条件付きであることを header 自体に書く（H-2）。
2. Task 6 item 6 の「short U=12 run で $S_\perp/2$ を Szz の横に出して physics inspection」は
   **定性的すぎる**。Δτ scan にして差が $\Delta\tau^2$ で 0 に向かうことを定量確認すべき。
3. これは欠陥ではなく**この機能が新たに提供する価値**である。
   $S_\perp/2-S^{zz}$ は **q 分解された Trotter 誤差 probe** であり、
   scalar のエネルギー比較では得られない情報を与える。計画の Goal に書く価値がある。

## 5. 指摘事項

### 5.1 High

#### H-1: §7 の acceptance criterion が U=0 の $S_\perp=2S^{zz}$ に寄りすぎている

§7 は

```text
- U=0 verifies `Sperp == 2*Szz` at every q。
```

を最終受け入れ条件に挙げ、依頼メッセージでも「U=0では全qについて
Sperp = 2*Szz を厳密検証」が主要決定として挙がっている。しかし §3.2 の実測どおり、
この条件は **spin pairing を取り違えた estimator でも厳密に満たされる**。
$G=\tfrac12I$ の check も同じ盲点を持つ。

幸い、計画には検出力のある check が既に入っている。

- §3.1「fully polarized product state: 全 q で $S_\perp=1/2$」（差 0.500）
- Task 2 test #4「deterministic dense non-symmetric matrices match the direct oracle」（差 0.110）

したがって**カバレッジの穴ではなく、優先順位付けの誤り**である。修正案:

- §7 の並びを組み替え、先頭を「dense asymmetric $G_\uparrow\ne G_\downarrow$ が
  direct oracle と一致」と「fully polarized / Néel の解析値と一致」にする。
- U=0 の項には「DQMC Green を通した end-to-end の配線確認であり、
  spin pairing の正しさは保証しない」と注記する。
- Task 2 test #4 のテストコードに、この理由をコメントとして残す。

#### H-2: TSV header の `su2_relation` が有限 Δτ で成り立たない

§4 の通り。`# su2_relation=Sperp(q)=2*Szz(q)` を出力ファイルに無条件で書くと、
低温 AF run で 2 つの TSV を突き合わせた利用者が bug と誤認する。
とくに Néel 的な配置では $S_\perp$ は全 q で平坦なのに $2S^{zz}(Q_\mathrm{AF})$ は
$N/2$（4x4 なら 8）まで立つので、**桁で違って見える**。

header を条件付きに書き換えること。例:

```text
# su2_relation=Sperp(q)=2*Szz(q) for the SU(2)-symmetric model
# su2_caveat=spin-channel HS breaks SU(2) at finite dtau; the measured
#            difference is a systematic O(dtau^2) Trotter effect, not noise
# exact_relation=sum_q Sperp(q) = 2*sum_q Szz(q) holds at any dtau
```

3 行目が重要である。**q ごとには破れるが総和は厳密に一致する**という、
利用者が実際に使える切り分け基準を与えられる。

usage 文書にも同じ説明を入れ、Task 6 item 6 を Δτ scan の定量検証に格上げすること。

#### H-3: Szz と Sperp を同時に有効にしたときのメモリ倍増が計画にない

`docs/2026-08-21-szz-structure-factor-usage.md` §5 は既に

```text
- phase table は rank あたり `8*N*nq` bytes（szz_q=all で 48x48: 約42 MB/rank）
- MPI/hybrid root はさらに約 `16*nrep*nbin*nq` bytes（32x32,nrep=120,nbin=100 で約197 MB）
```

と明記している。計画 §5.1 の「Share Momentum Infrastructure」は**型の一般化**であって
instance の共有ではなく、§5.3 は「二つの plan を受ける」としているので、
`szz_q=all sperp_q=all` では phase table も root vector も**そのまま 2 倍**になる。

| 格子 | phase table / rank（Szz のみ → 両方） |
| --- | --- |
| 32x32 | 8 MB → **16 MB** |
| 48x48 | 42 MB → **85 MB** |
| 64x64 | 134 MB → **268 MB** |

root は 32x32 / `nrep=120` / `nbin=100` で 197 MB → **394 MB**。

対応は次のいずれか。

- **(推奨) selector 文字列が一致するときは plan instance を共有する。**
  plan は read-only なので OpenMP でも安全に共有でき、`main.c` で
  「同一文字列なら 1 つ作って両方に同じポインタを渡し、free は 1 回」とするだけで済む。
  workspace の `corr_disp` は observable ごとに必要なので分ける。
- 少なくとも usage 文書に倍増を明記し、大規模格子では
  「片方は `all`、もう片方は selected q」を推奨する。

### 5.2 Medium

#### M-1: Task 4 の「test target discovery only if required」は必須にする

`Makefile` には既に

```make
test_szz: dqmc
test_szz_parallel: dqmc dqmc_omp dqmc_mpi dqmc_hybrid
test: $(TESTBIN) test_szz
test_hybrid: $(TESTBIN_HYBRID) test_szz_parallel
```

がある（前回レビュー M-1 への対応）。`tests/test_sperp_output.sh` と
`tests/test_sperp_parallel.sh` を**同じ形で `test:` / `test_hybrid:` に連結する**ことを
Task 4 / Task 5 の必須項目にすること。`.PHONY` への追加と
`.gitignore` の `!tests/test_*.sh` は既に整っている。
「only if required」のままだと、前回指摘した「shell test が自動実行されない」穴が再発する。

#### M-2: Néel product state の check を追加する

§3.1 の解析 check に Néel が無い。bug 検出力は fully polarized と重複するが
（どちらも variant A を 0.5 の差で捕まえる）、**回帰テストとしての価値が別にある**。

- $S_\perp=1/2$（全 q）に対し $2S^{zz}(Q_\mathrm{AF})=N/2$ という桁違いの差を固定する。
  将来 $S_\perp$ を「$2S^{zz}$ に合うように」直そうとする誤った修正を止められる。
- fully polarized は $\downarrow$ セクターが空という退化した状態だが、
  Néel は両 spin セクターが埋まった状態で同じ値を出すことを確認できる。

より一般形として「1 サイト 1 電子の任意の対角 product state で $S_\perp(q)=1/2$」を
テストにするのが最も強い。

#### M-3: 「Sperp を有効にしても Szz 出力が変わらない」が明示的 gate になっていない

Task 4 step 6 は「compare disabled stdout and Szz TSV with Task 0 baseline」だが、
本当に必要なのは同一 seed での 3 通りの比較である。

1. 両方無効 → stdout が baseline と byte 一致、TSV が作られない。
2. Szz のみ有効 → stdout scalar 行と `szz.tsv` が baseline と byte 一致。
3. **両方有効 → `szz.tsv` が (2) と byte 一致し、stdout scalar 行も一致。**

(3) が新規機能に固有の gate であり、「追加の RNG 呼び出しをしない」という
§7 の主張を実証する唯一の手段である。明記すること。

#### M-4: stdout metadata comment の扱いが未定義

`src/main.c:652` は Szz 有効時に ` szz_file=%s szz_nq=%d` を metadata 行へ追記する。
Sperp で同じことをするのかしないのかが計画に無い。後方互換 gate は
この行の byte 比較に依存するので、output contract に明記すること。

#### M-5: spin swap invariance と $q\leftrightarrow-q$ は tautology

§3.1 の「spin label swap $G_\uparrow\leftrightarrow G_\downarrow$ で不変」は、
$C^\perp$ の式が 2 項の入れ替えで閉じるため**誤った実装でも成立する**（数値実測: 差 0）。
$q\leftrightarrow-q$ も cos 実装では恒真で、これは Szz レビューでも同じ指摘をした。

削る必要はないが、テストコードに「これは実装の構造から恒真であり、
physical property の documentation として置いている」とコメントし、
§7 の acceptance の根拠には数えないこと。

#### M-6: Task 1 の gate に cross-mode script が入っていない

Task 1 の gate は

```sh
make tests/test_structure_factor tests/test_integration
./tests/test_structure_factor
./tests/test_integration
make test
```

だが、型の一般化は `src/main.c`・`src/replica_run.c`・`src/replica_mpi.c` の経路にも及ぶ。
`make test` は `test_szz`（serial output script）しか含まないので、
**`make test_hybrid`（= `test_szz_parallel`）を Task 1 の gate に加える**こと。
MPI ordering の regression は serial script では検出できない。

#### M-7: 両方有効時に $O(N^2)$ の $(i,j)$ ループが 2 回走る

`corr_disp` は **selector に依存せず lattice と Green 関数だけで決まる**。
したがって 1 パスで `corr_disp_zz` と `corr_disp_perp` を同時に埋め、
その後それぞれの phase table と縮約できる。異なる selector を指定していても成立する。

前回レビュー L-1 で指摘したとおり現行 kernel は 1 対あたり 7 回の `isfinite` と
冗長な対角要素再読み込みを行っており、それが 2 倍になる。
計算時間は sweep の $O(N^3L_\tau)$ に対して依然無視できるので**優先度は低い**が、
型を一般化する Task 1 の時点で kernel も 1 パス化しておくのが自然である。

#### M-8: 差の誤差棒が v1 出力では正しく付けられない

§2.2 は「Szz/Sperp covariance を使う paired-error 列」を除外している。判断は妥当だが、
**この機能の目玉診断量である $S_\perp/2-S^{zz}$ に正しい誤差が付けられない**という
具体的な代償が生じる。両者は同一 configuration 上で測るので正の相関を持ち、

$$
\mathrm{Var}(X-Y)=\mathrm{Var}X+\mathrm{Var}Y-2\,\mathrm{Cov}(X,Y)
<\mathrm{Var}X+\mathrm{Var}Y
$$

である。したがって 2 ファイルの誤差を単純に二乗和すると**過大評価**になる。
向きとしては安全側（有意差を見落とす側）なので v1 で問題ないが、
usage 文書に「単純な誤差伝播は保守的であり、真の差の有意性は過小評価される。
厳密な誤差が必要なら bin レベルの出力を伴う別機能とする」と明記すること。

### 5.3 Low

- **L-1: 型名。** `SpinStructurePlan` / `SpinStructureWorkspace` は selector・momentum index・
  phase table を持つだけで spin 依存が無い。将来 charge structure factor で
  再度改名しないよう、`MomentumPlan` / `StructureFactorPlan` を検討する。
  改名は一度で済ませたい。

- **L-2: file 衝突検査が文字列比較のみ。** `szz_file=./a.tsv` と `sperp_file=a.tsv` は
  素通りする。既定値どうし（`szz.tsv` / `sperp.tsv`）は衝突しないので実害は小さいが、
  契約として「文字列一致のみを検査する」と書いておくこと。

- **L-3: $C^\perp_{ij}=C^\perp_{ji}$ の根拠を 1 行入れる。** §3 は結論だけ書いている。
  $i\ne j$ で $-\tfrac12(g^\uparrow_{ji}g^\downarrow_{ij}+g^\downarrow_{ji}g^\uparrow_{ij})$ に
  なることを示せば、cos-only 実装の正当性が自明になる。

- **L-4: Task 6 の文書更新先が曖昧。** 「README.md or the existing Szz usage guide as
  appropriate」ではなく、既存 `docs/2026-08-21-szz-structure-factor-usage.md` を
  spin structure factor 全体の usage 文書へ拡張するのか、sibling を作るのかを
  計画時点で決めること。両者に SU(2)/Δτ の注意（H-2）が必要になるので、
  1 本にまとめるほうが齟齬が出にくい。

- **L-5: $S^{+-}$ と $S^{-+}$ の関係を §1 に書く。** §3.1 で述べたとおり、
  weight が $s\to-s$ で不変なので両者は MC 平均で一致する。
  $(S^{+-}+S^{-+})/2$ は厳密対称性による**分散低減**でもあり、
  「raw を出さない」という決定の積極的な根拠になる。

- **L-6: `ReplicaQObservable` の enabled 表現。** `nq==0` を disabled として使うなら、
  `sum_sign_values != NULL && nq > 0` という invariant を 1 つの helper に集約し、
  各所で個別に判定しないこと。現行 Szz と同じ流儀なので一貫性はある。

- **L-7: Task 2 test #8（PH mapping）の位置づけ。** 既存 `tests/test_ph_symmetry.c` は
  direct な $G_\downarrow$ と PH-mapped $G_\downarrow$ が 1e-9 で一致することを既に
  確認している。両者を estimator に入れて比較するテストは、その一致の
  下流確認であって独立な物理検証ではない。意図をコメントに書くこと。

## 6. 推奨する検証の優先順位

§3.2 の実測に基づく並べ替え。

| 優先 | 検証 | 検出できるもの |
| ---: | --- | --- |
| 1 | dense asymmetric $G_\uparrow\ne G_\downarrow$ vs direct oracle | spin pairing、係数、$\delta$ 配置のすべて |
| 2 | 実空間 $C^\perp_{ij}$ の要素比較 | **index 転置**（momentum 和の後では原理的に見えない） |
| 3 | fully polarized / Néel / 1 電子 product state = $1/2$ | spin pairing、係数 |
| 4 | all-q sum rule $=\tfrac12(N_e-2ND)$ | 係数、q 生成漏れ、MPI 受信欠落 |
| 5 | $G=\tfrac12I=1/4$ | 係数のみ |
| 6 | U=0 で $S_\perp=2S^{zz}$ | 係数のみ（+ end-to-end 配線） |
| — | spin swap 不変、$q\leftrightarrow-q$ | **なし（恒真）** |

優先度 2 は計画に無い。§8 の risk 表に「Green index reversal」を挙げている以上、
実空間要素の比較を Task 2 のテストに入れないとその risk は閉じない。

## 7. 未確認事項

- 実装は存在しないので、コードレベルの検証は行っていない。
  §3.2 の数値は、計画の式から自分で書き起こした参照実装によるもの。
- $S_\perp/2-S^{zz}$ の Δτ 依存性の**大きさ**は測定していない。
  $O(\Delta\tau^2)$ という次数はプロジェクトが既に確立した Trotter 次数
  （`VALIDATION.md`、`qmc_L6_U4_trotter_summary.dat`）からの推定であり、
  この量について指数を実測することを Task 6 に入れるべきである。
- メモリ見積り（H-3）は配列サイズからの算術で、実測ではない。

## 8. 結論

- **物理式・規約・sum rule は正しい。計画は採用してよい。**
- 着手前に **H-1（acceptance の重み付け）と H-2（有限 Δτ の SU(2) 破れ）** を
  計画へ反映すること。とくに H-2 は、実装が正しくても利用者が bug と誤認する種類の
  問題であり、TSV header と usage 文書の両方で扱う必要がある。
- **H-3（メモリ倍増）** は既存 usage 文書との整合の問題なので、
  実装方針（plan instance 共有か文書化か）を先に決めること。
- Medium のうち **M-1（make target）と M-3（両方有効時の Szz byte 一致）** は
  gate として計画に書き込むこと。残りは実装中に取り込めばよい。
