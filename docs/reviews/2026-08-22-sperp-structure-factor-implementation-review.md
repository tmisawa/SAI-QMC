---
date: 2026-08-22
datetime: 2026-08-22 16:42 JST
model: Claude Opus 5 (1M context)
status: review
topic: Sperp(q) transverse structure factor implementation review
target: |
  branch feat/sperp-structure-factor (uncommitted worktree, base 4ba47c5)
  src/structure_factor.{c,h}, src/{main,io,replica,replica_run,replica_mpi,profiler}.c
  tests/test_structure_factor.c, tests/test_sperp_{output,parallel}.sh
  docs/2026-08-22-sperp-structure-factor-{usage,validation}.md
reviewer: Claude Opus 5
summary: |
  S_perp(q) 実装のレビュー。correctness bug は検出されなかった。
  まず前回レビューの誤り（有限 dtau で SU(2) が破れるという主張）を訂正する。
  全 HS 配置の厳密列挙で、ensemble の Sperp=2*Szz が任意の dtau で成立することを
  確認した。実装の header 記述が正しい。24 seed の独立測定で dSperp が
  20-30% 過小評価であることを定量化した。Medium 3 件、Low 5 件。
---

# Sperp Structure Factor 実装レビュー

- 日時: 2026-08-22 16:42 JST
- 使用AI: Claude Opus 5 (1M context)
- 要約: 実装は正しい。前回レビュー（計画レビュー）の H-2 は物理的に誤りだったので
  §2 で訂正し、厳密列挙で決着させる。新規の実測所見は「`dSperp` が bin 幅を
  いくら広げても真の誤差より 20-30% 小さい」という統計上の注意点。

## 1. レビュー方法

対象は `feat/sperp-structure-factor`（base `4ba47c5`、全変更未コミット）。

**静的読解に加えて、次を実際に実行した。**

- 4 variant のビルド（`-Wall -Wextra` 警告ゼロ）と全 test matrix
- `git worktree` で `4ba47c5` の baseline binary を作り byte 比較
- 全 HS 配置の**厳密列挙**による SU(2) 恒等式の検証（§2）
- U=0 の独立 free-fermion 公式との照合（§3.2）
- **24 個の独立 seed** を使った誤差棒の較正測定（§4.1）
- 3 種類の bin 幅での誤差の bin 幅依存性（§4.1）
- ASan/UBSan、`leaks --atExit`、および自作の異常系入力

## 2. 前回レビューの訂正: 有限 dtau で SU(2) は破れない

`docs/reviews/2026-08-22-sperp-structure-factor-plan-review.md` の **H-2 は誤りだった。**
あそこでは「Hirsch spin-channel HS が補助場を $S^z$ に結合させるので、有限 $\Delta\tau$ では
$S_\perp/2-S^{zz}$ が $O(\Delta\tau^2)$ の系統誤差として残る」と書いたが、これは成立しない。

正しい論証は次のとおり。

1. 離散 HS 変換は**厳密な恒等式**である。各 slice で
   $\sum_{s}\tfrac12 e^{\lambda s(n_\uparrow-n_\downarrow)}\cdot\mathrm{const}
   =e^{-\Delta\tau U(n_\uparrow-\frac12)(n_\downarrow-\frac12)}$。
2. したがって全補助場について和を取ると
   $\sum_{\{s\}}\prod_l B(s_l)=\prod_l\left(e^{-\Delta\tau K}e^{-\Delta\tau V}\right)\equiv P$。
3. $K$ も $V=U(n_\uparrow-\frac12)(n_\downarrow-\frac12)$ も SU(2) 不変
   （$n_\uparrow n_\downarrow=(n_i^2-n_i)/2$）なので、**$P$ は SU(2) 不変**。
4. 等時刻推定量は $\langle O\rangle=\mathrm{Tr}[OP]/\mathrm{Tr}[P]$ に厳密に等しい
   （$\sum_s w(s)\langle O\rangle_s=\mathrm{Tr}[O\sum_s\prod B(s)]$）。
5. よって $\langle S^{zz}\rangle=\langle S^{xx}\rangle=\langle S^{yy}\rangle$ が
   **任意の $\Delta\tau$ で厳密**に成立する。

SU(2) が破れるのは**個々の補助場配置に対してだけ**で、ensemble 平均では回復する。
実装の TSV header

```text
# su2_relation=ensemble Sperp(q)=2*Szz(q) at any dtau for the current SU(2)-invariant model
# hs_caveat=individual spin-channel-HS samples select the z axis; equality is restored by ensemble averaging
```

は**正しい**。前回の指摘に従わなかったのは適切な判断である。

### 2.1 厳密列挙による確認

コードと同一の規約（`K=t_{ij}-\mu\delta_{ij}`、$\mu=U/2$、`expK=exp(-dtau*K)`、
`expv[s]=exp(lambda*sigma*s - dtau*U/2)`、`B=expK·diag(expv)`、$G=(I+B_{L-1}\cdots B_0)^{-1}$）
で、全 HS 配置を列挙して $\sum_s w(s)S(q;s)/\sum_s w(s)$ を厳密に計算した。

| 系 | 配置数 | U | dtau | $\max_q\lvert S_\perp-2S^{zz}\rvert$ |
| --- | ---: | ---: | ---: | ---: |
| 2-site chain, $L_\tau=3$ | 64 | 4 | 0.4 | 2.2e-16 |
| 2-site chain, $L_\tau=3$ | 64 | 4 | 0.05 | 2.2e-16 |
| 2-site chain, $L_\tau=3$ | 64 | 8 | 0.4 | 5.6e-17 |
| 2-site chain, $L_\tau=3$ | 64 | 8 | 0.05 | 5.6e-17 |
| 4-site chain PBC, $L_\tau=2$ | 256 | 8 | 0.4 | 6.1e-16 |
| 4-site chain PBC, $L_\tau=2$ | 256 | 8 | 0.1 | 3.9e-16 |

$\Delta\tau=0.4$、$U=8$ という粗い・強結合条件でも機械精度で一致する。
統計を一切含まない決着である。

### 2.2 MC による確認

`chain L=4, U=8, beta=2, dtau=0.2, nrep=8, nbin=20, nmeas=20000` を
**24 個の独立 seed** で回し、`DeltaSU2` の seed 平均をその標準誤差と比べた。

| q | $\overline{\Delta_\mathrm{SU2}}$ | s.e.m. | 0 からの隔たり |
| ---: | ---: | ---: | ---: |
| 0 | -4.3e-05 | 1.1e-03 | 0.04 σ |
| 1 | +9.0e-04 | 9.6e-04 | 0.94 σ |
| 2 | -1.8e-03 | 1.4e-03 | 1.27 σ |

$\Delta\tau=0.05$ でも同様で、$\Delta\tau$ 依存の傾向は見られない。
**`DeltaSU2` は Trotter 誤差の probe ではなく、実装バグと統計の健全性の probe である。**
利用文書がそう書いているのは正しい。

## 3. 検証結果

### 3.1 ビルドと test matrix

| command | 結果 |
| --- | --- |
| `make dqmc dqmc_omp dqmc_mpi dqmc_hybrid` | 成功、警告ゼロ |
| `make test`（`test_szz`, `test_sperp` を含む） | ALL TESTS PASSED |
| `make test_omp` | ALL OMP TESTS PASSED |
| `OMPI_CC=cc make test_mpi` | ALL MPI TESTS PASSED |
| `OMPI_CC=cc make test_hybrid`（`test_szz_parallel`, `test_sperp_parallel` を含む） | ALL HYBRID TESTS PASSED |
| `make test_slow` | ALL SLOW TESTS PASSED |

前回指摘 M-1（shell test が make target に入っていない）は解消されている。
`test_sperp` は `test:` に、`test_sperp_parallel` は `test_hybrid:` に連結済みで、
`AGENTS.md` が案内する 4 コマンドだけで全部走る。

なお `mpicc` は configure 時の `gcc-15` を探すため、`src/*.c` を touch した後は
`OMPI_CC` の指定が必須だった。文書の記述どおりである。

### 3.2 物理の独立検証

**U=0**: `input/1d_L4_U0_spin_all.txt` の `sperp.tsv` を、自分で書いた自由フェルミオン公式
$S^{zz}_{U=0}(q)=\frac{1}{2N}\sum_k f_k(1-f_{k+q})$ の 2 倍と比較した。

| m | Sperp (DQMC) | $2\times$ 独立公式 |
| ---: | --- | --- |
| 0 | 0.13383135310664554 | 0.13383135310664557 |
| 1 | 0.25 | 0.25 |
| 2 | 0.36616864689335449 | 0.36616864689335443 |
| 3 | 0.24999999999999994 | 0.25 |

**estimator の式**: `spin_pair_correlations()` の

```c
*cperp = 0.5 * ((delta - gu_ji) * gd_ij + (delta - gd_ji) * gu_ij);
```

は、自分の Wick 再導出
$\langle S^+_iS^-_j\rangle_s=(\delta_{ij}-g^\uparrow_{ji})g^\downarrow_{ij}$
（fermion 並べ替えは 2 transposition なので符号 $+1$）と完全に一致する。

**sum rule**: $C^\perp_{ii}=2C^{zz}_{ii}$ が厳密なので
$\sum_qS_\perp=\tfrac12(N_e-2ND)$ が sample ごとに成立する。実測でも
`sperp_sum_rule_check()` の prefactor 0.5 が正しく効いている。

### 3.3 後方互換

`4ba47c5` を `git worktree` に展開して baseline binary を作り、同一入力で比較した。

| 入力 | stdout | szz.tsv |
| --- | --- | --- |
| `input/1d_L4_U0.txt` | byte 一致 | — |
| `input/1d_L4_U4.txt` | byte 一致 | — |
| `input/1d_L4_U0_szz_all.txt` | byte 一致 | **byte 一致** |

`tests/test_sperp_output.sh` はさらに「Szz のみ」と「両方有効」で `szz.tsv` が
byte 一致し、stdout の scalar 行も一致することを検査している。前回指摘 M-3 の
3 通り比較がそのまま入っている。

### 3.4 メモリ安全性と異常系

自分で実行した確認:

- ASan+UBSan（`-fsanitize=address,undefined`）で
  (a) 両方有効・plan 共有、(b) Sperp のみ、(c) Szz/Sperp で異なる q 幅
  の 3 経路とも runtime error ゼロ。
- `leaks --atExit` で `0 leaks`。plan 共有時の片側 free（`free_structure_plans()` の
  `plans_shared` 分岐）でも double free / leak なし。
- 異常系はすべて fail-fast:
  `lattice=file`+`sperp_q=af`、`sperp_q=0:0, 1:0`（内部空白）、`sperp_file=none`、
  `szz_file==sperp_file`、片側だけ enabled で consistency 有効、
  consistency で q 順序不一致、consistency file が szz_file と同名。
- **shell test が扱っていない組み合わせ**として、`szz_q=all` と
  `sperp_q=0:0,1:0,2:0,3:0`（文字列は違うが q 集合は同一）+ consistency を試した。
  plan は共有されないが `plans_have_same_q()` が真になり、consistency が正しく 4 行出た。

### 3.5 前回レビューの指摘への対応

| 前回の指摘 | 状態 |
| --- | --- |
| H-1: U=0 の `Sperp=2*Szz` は spin pairing 誤りを検出できない | 対応済。`tests/test_structure_factor.c:195` に `/* This SU(2) wiring check cannot detect an up/down pairing swap. */` があり、dense asymmetric oracle が主検証になっている |
| H-2: 有限 dtau の SU(2) 破れ | **こちらの誤り**。§2 で訂正 |
| H-3: plan/メモリ倍増 | 対応済。同一 selector で plan 共有、利用文書に見積り記載 |
| M-1: make target 連結 | 対応済 |
| M-2: Néel check | 対応済（`tests/test_structure_factor.c:213`） |
| M-3: 両方有効時の Szz byte 一致 gate | 対応済（`test_sperp_output.sh`） |
| M-5: tautology の明示 | 対応済（コメント） |
| M-7: one-pass 走査 | 対応済（`measure_szz_sperp_sample()`） |
| 優先度 2: 実空間 $C^\perp_{ij}$ の要素比較 | 対応済（`spin_pair_correlations` vs `direct_cperp`、1e-15） |
| L-1: 型名 | 対応済（`StructureFactorPlan`） |

## 4. 指摘事項

correctness bug は検出されなかった。以下は統計・運用・文書の指摘である。

### 4.1 Medium

#### M-1: `dSperp` は bin 幅を広げても真の誤差より 20-30% 小さい

これが今回の主要な実測所見である。

`chain L=4, U=8, beta=2, dtau=0.2, nrep=8, nmeas=20000` を **24 個の独立 seed**で
回し、seed 間の標準偏差（＝真の統計誤差）と、各 run が報告する jackknife 誤差を比べた。
3 種類の bin 幅で繰り返した（各比の相対不確かさは約 15%）。

| bin size | observable | 報告 / 真値 (q=0) | (q=1) | (q=2) |
| ---: | --- | ---: | ---: | ---: |
| 200 | Szz | 0.92 | 1.00 | 0.98 |
| 200 | **Sperp** | **0.84** | **0.75** | **0.85** |
| 1000 | Szz | 1.02 | 1.05 | 1.07 |
| 1000 | **Sperp** | **0.85** | **0.75** | **0.86** |
| 5000 | Szz | 1.05 | 1.02 | 1.08 |
| 5000 | **Sperp** | **0.88** | **0.76** | **0.87** |

- **`dSzz` は正しい。** 報告値と真値が bin 幅によらず一致する。
- **`dSperp` は一貫して 15-25% 小さい。** つまり真の誤差は報告値の約 1.2-1.3 倍。
- 決定的なのは、**bin 幅を 200 から 5000 まで 25 倍にしてもこの差が閉じない**点である。
  bin size 5000 は 1 replica の測定列全体の 1/4 にあたる。
- 単一 run 内の bin 幅走査では
  `dSperp` が bin size 200→5000 で 6.80e-03→7.12e-03（+5%）しか増えず、
  **真値 8.08e-03 に届かないまま plateau に見える**。

つまり、既存 Szz 利用文書が勧める「bin 幅 plateau を確認せよ」という診断は
**Sperp では偽の安心を与える**。`nwarm` を 10 倍（2000→20000）にしても改善しなかったので、
warmup 不足では説明できない。pooled bin jackknife が replica 間の寄与を
取りこぼしている可能性が高く、これは計画が明示的に scope 外とした
「replica-blocked error estimator の改修」そのものである。

対応（実装変更は不要）:

- 利用文書に「`dSperp` は bin 幅 plateau だけでは検証できない。少なくとも
  `chain L=4,U=8,beta=2` の測定では真の誤差の 0.75-0.88 倍だった」と明記する。
- 論文品質の誤差が必要なときは、**独立 seed の run を複数回し、その散らばりから
  誤差を取る**ことを推奨する（scalar observables でも同じ手が使える）。
- `DeltaSU2` の誤差は**較正が取れている**（真値/報告値 = 1.06, 1.08, 0.85、平均 1.00）
  ので、SU(2) consistency 判定そのものは信頼してよい。これは書いておく価値がある。

#### M-2: 両方有効時に profiler の `measure_szz` が消える

`dqmc_run_replica()` は両方有効なら one-pass の
`measure_szz_sperp_sample()` を呼び、その時間を `PROF_MEASURE_SPERP` だけに記録する。
実測（`profile=1`）:

```text
# both enabled
measurement,measure_sperp,10,4.00e-06,...      <- measure_szz の行は無い
# szz only
measurement,measure_szz,10,1.00e-06,...
```

profile を条件間で比較する利用者から見ると、Sperp を有効にした途端に
`measure_szz` が消え、`measure_sperp` が両方の費用を抱える。誤読しやすい。

対応案（いずれか）:

- joint path 用に `measure_spin` という第 3 の region 名を用意する（最も明快）。
- あるいは joint path を `PROF_MEASURE_SZZ` に記録し、`measure_sperp` は
  Sperp 単独 path 専用にする。
- 最低限、利用文書と validation 文書に 1 行書く。validation 文書 §7 は
  「one-pass joint measurement を記録する `measure_sperp`」と書いてはいるが、
  `measure_szz` が消えることには触れていない。

#### M-3: validation 文書に相互作用系の `DeltaSU2` 実測が無い

`DeltaSU2` は今回の目玉機能だが、validation 文書に載っているのは
`tests/test_sperp_output.sh` の U=0 ケース（厳密に 0）だけである。
U>0 で統計誤差の範囲に収まることを示した記録が無い。

§2.2 の測定（24 seed、$U=8$、$\beta=2$、$\Delta\tau=0.2$ と $0.05$、0.04-1.27 σ）を
そのまま validation 文書に転記すれば埋まる。あわせて §2.1 の厳密列挙も
「なぜ任意の dtau で 0 になるはずなのか」の根拠として載せる価値がある。

### 4.2 Low

- **L-1: `.gitignore` に spin observable の既定出力が入っていない。**
  `/replicas.csv` と `hopping_used.txt` は無視されるのに、`szz.tsv` / `sperp.tsv` /
  `spin_consistency.tsv` は入っていない。実際に repo root に未追跡の `szz.tsv` が
  残っている（`git status` に出る）。`/szz.tsv`, `/sperp.tsv`,
  `/spin_consistency.tsv` を追加し、残っているファイルを削除すること。

- **L-2: 新規 2 文書が `AGENTS.md` の文書ルールを満たしていない。**
  `AGENTS.md` §文書作成ルールは「日時（日付＋**時刻**、JST）」と
  「簡潔なまとめ（2〜5 行）」を必須にしている。
  `docs/2026-08-22-sperp-structure-factor-{usage,validation}.md` は
  `datetime: 2026-08-22 JST` で時刻が無く、`summary:` フィールドも無い。
  Szz の同種文書（`2026-08-21-...`）は両方持っているので、揃えること。

- **L-3: 互換 typedef と wrapper が残っている。**
  `structure_factor.h` の `typedef StructureFactorPlan SzzPlan;` 他 3 本と、
  `szz_plan_init` / `szz_plan_free` / `szz_workspace_init` / `szz_workspace_free` /
  `sperp_bin_ratio`（`szz_bin_ratio` の別名）は、一般化後は薄い転送層である。
  意図的に残すなら header に 1 行そう書き、そうでなければ呼び出し側を
  `structure_factor_*` に寄せて削ること。中途半端に両方あるのが一番読みにくい。

- **L-4: 全変更が未コミット。** 24 ファイルの変更と 6 ファイルの新規追加が
  working tree にある。計画は 6 つの commit boundary を定義していたので、
  merge 前にその粒度で commit しておくと `git bisect` が効く。前回と同じ指摘である。

- **L-5: `spin_consistency_file` の衝突検査が `szz_file`/`sperp_file` に限られる。**
  `replica_log` や `profile_file` と同名でも通る。既定値どうしは衝突しないので
  実害は小さいが、利用文書の「v1 は canonical-path 同一性までは判定しない」の
  すぐ隣に書いておくとよい。

## 5. 未確認事項

- §4.1 の測定は `chain L=4, U=8, beta=2` の 1 条件のみである。
  過小評価の倍率が格子サイズ・温度・U でどう変わるかは測っていない。
- 厳密列挙（§2.1）は最大 4 site / $L_\tau=2$ までで、大きな系では確認していない。
  ただし論証（§2 の 1-5）は系のサイズに依らない。
- kugui/Genkai などの remote 実行は行っていない（`AGENTS.md` の承認が必要）。

## 6. 結論

- **実装は正しい。** estimator は独立導出と一致し、U=0 は独立公式と機械精度で一致、
  後方互換は baseline binary と byte 一致、全 test matrix・sanitizer・leak 検査が clean。
  前回レビューの指摘はこちらの誤り（H-2）を除きすべて反映されている。
- **こちらの H-2 は誤りだった。** 有限 dtau でも ensemble の $S_\perp=2S^{zz}$ は
  厳密に成立する。§2.1 の厳密列挙で決着した。実装の header 記述が正しい。
- merge 前に直すべきものは無い。**M-1 は文書に書けば足りる**が、
  `dSperp` を論文の誤差として使う前に必ず読む必要があるので優先度は高い。
- M-2（profiler の region 帰属）は小さな改善、M-3 と Low は文書と housekeeping。
