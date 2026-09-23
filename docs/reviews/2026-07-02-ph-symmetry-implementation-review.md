---
date: 2026-07-02
datetime: 2026-07-02 20:31 JST
model: Claude Fable 5
status: review
topic: PH 対称性実装（down スピン省略）の検証レビュー
summary: |
  半充填・二部格子で down スピンの更新/wrap/安定化を省く PH 対称性実装
  （未コミット working tree）の徹底検証。数理（恒等式・Metropolis 比・
  sign≡+1 の正当性）、delayed update との整合、全テスト
  （serial/slow/omp/mpi/hybrid）、leaks、低温ストレス（β=20, U=12, Ltr=800）、
  旧バイナリ A/B、性能実測を
  すべて実施し**ブロッカーなし**。16×16 で β=4: 1.90×, β=8: 1.96× と
  ほぼ理想の 2× を達成。sweep はスタック導入前から累計 ~25×。
  軽微メモ（dN 列が厳密 0 化、periodic 写像は防御的、Gd のフル確保、
  バージョン間は統計比較）と、解消済みメモ（square sweep テスト追加済み）。
---

# PH 対称性実装 検証レビュー

## 対象

未コミット working tree（基点 HEAD b77afcb、delayed update 2c7cb11 込み）:

- `src/model.{h,c}` — `half_filling` / `ph_symmetric` / `bipart` の保持
  （`ph_symmetric = half && L->is_bipartite`）
- `src/green.{h,c}` — `green_build_ph_down`（G↓ = 1 − P G↑ᵀ P）、
  `green_ph_down_ratio_N`、`green_delay_diag` の共通化
- `src/dqmc.{h,c}` — `use_ph` 経路: down スピンの更新・wrap・stack/from_stack
  再構成をスキップし、安定化点で `dqmc_map_ph_down` により G↓ を写像で再構成
- 新規テスト `tests/test_ph_symmetry.c`、
  拡張 `tests/test_dqmc_stack.c`（PH vs two-spin 同一軌道）、
  `tests/test_dqmc.c` / `tests/test_model.c`

## 結論

**ブロッカーなし。コミット可能。**
数理・delayed update との整合・fail-fast 意味論・数値精度・物理出力・性能の
すべてが確認できた。性能はほぼ理想の 2×。軽微メモは任意フォローアップ。

## 1. 数理の正しさ（精読）

### 恒等式と Metropolis 比

- `green_build_ph_down`: $[G_\downarrow]_{ij} = \delta_{ij} - \varepsilon_i
  \varepsilon_j [G_\uparrow]_{ji}$ — 規約どおり
  （`docs/2026-07-02-ph-symmetry-spin-correlations-note.md` §根拠と同形）。
- `green_ph_down_ratio_N`: $R_\downarrow = 1 + g^\uparrow_{ii} N_\downarrow$ は
  $g^\downarrow_{ii} = 1 - g^\uparrow_{ii}$（$\varepsilon_i^2 = 1$）と厳密整合。
  対角は delay 補正済み（`green_delay_diag`）を使用しており、two-spin 側の
  `green_delay_ratio_N` と同じ精度規約。
- 解析的に $R_\downarrow = e^{2\lambda s} R_\uparrow$、したがって
  $R = R_\uparrow R_\downarrow = e^{2\lambda s} R_\uparrow^2 \ge 0$。

### sign ≡ +1 の正当性

$G_\downarrow = 1 - P G_\uparrow^{\mathsf T} P$ は
$M_\downarrow^{-1} = P\,(M_\uparrow^{\mathsf T} - 1)(M_\uparrow^{\mathsf T})^{-1}\,P$
と同値で、$M_\uparrow - 1 = B_\uparrow(\beta,0)$ だから

$$\det M_\downarrow = \frac{\det M_\uparrow}{\det B_\uparrow(\beta,0)},
\qquad
\det B_\uparrow(\beta,0) = \prod_l \det B_\uparrow(l) > 0
\ \Rightarrow\
\operatorname{sgn}\det M_\downarrow = \operatorname{sgn}\det M_\uparrow .$$

- PH 時 `D.sign = 1.0` 固定は厳密に正しい
  （$\operatorname{sgn}(\det M_\uparrow \det M_\downarrow) =
  \operatorname{sgn}(\det M_\uparrow)^2 = +1$）。
- `Gd.det_sign = Gu.det_sign`（`dqmc_map_ph_down`）も同式から正当。
  検証テストが two-spin reference の det_sign と直接比較しており、実測でも一致。

### delayed update との整合（本実装の急所）

- `dqmc_map_ph_down` の呼び出しは `green_from_stack` / `green_from_left_udv` /
  `green_from_scratch` の**直後のみ**。これらはすべて `delay_count = 0` に
  リセットするため、写像は常に**厳密な** G↑ から行われる（stale な
  delay 未適用 g からの写像は起こらない）。
- 提案比は `green_delay_diag` による delay 補正済み対角を使用 ✓。
- down 側の `N_\downarrow` は `field_N(f, -1.0, s[l·n+i])` — ループ変数 l と
  `Gu.cur_l` は sweep 中一致しており、two-spin 経路の
  `green_flipN(&Gd, i)` と同値 ✓。

### ガードと解放

- `ph_symmetric = half && is_bipartite`。奇数周期鎖・奇数周期正方格子は
  `is_bipartite = 0`、`lattice=file` は BFS 2彩色で判定（対角ゼロ・対称性が
  入力検証で強制されるため PH の前提は自動的に満たされる）。
- なお**非二部格子入力は main の入力検証が旧コードから一律拒否**
  （v1 スコープ外）なので、production バイナリでは PH 経路が常時有効。
  two-spin 経路はテスト（`m.ph_symmetric = 0` 上書き）専用のリファレンス。
- `dqmc_init` 冒頭の `memset(D, 0, …)` により、PH 時に未確保の
  stack_d / left_d / combined_d も `dqmc_free` で安全
  （`free(NULL)` / `green_stack_free` の NULL ガード）。

## 2. テスト実行結果

| 項目 | 結果 |
|---|---|
| `make clean && make dqmc` | 警告なしでビルド成功 |
| `make test`（test_ph_symmetry / 拡張 test_dqmc_stack 含む） | ALL TESTS PASSED |
| `make test_slow` | ALL SLOW TESTS PASSED |
| `make test_omp` | ALL OMP TESTS PASSED |
| `make test_mpi` | ALL MPI TESTS PASSED |
| `make test_hybrid` | ALL HYBRID TESTS PASSED |
| `leaks --atExit`（PH 本体・test_dqmc_stack・test_ph_symmetry） | すべて 0 leaks |

追加テストの設計評価: `test_dqmc_stack` の「同一 seed で PH sweep vs
from-scratch two-spin reference の場軌道・sign・det_sign・G の一致」は
強い不変条件で適切（場は完全一致、G は 1e-9 許容）。初回レビュー時は chain
のみだったが、2026-07-02 21:23 JST の追補で 2D square (2×4) ケースも追加済み。

## 3. 低温ストレス検証（既存テストのカバレッジ外を追加実施）

既存テストは Ltr ≤ 20。production 条件で使い捨て比較プログラムを実行:

写像一致（`green_from_scratch` の G↓ 直接計算 vs `green_build_ph_down`）:

| 条件 | max\|ΔG↓\| | det_sign 不一致 |
|---|---:|---:|
| 1D L4, β=20, U=4/12 (Ltr=200) | ≤ 4.3e-13 | 0 |
| 1D L8, β=16, U=8 (Ltr=160) | 3.9e-10 | 0 |
| 1D L8, β=20, dtau=0.025 (Ltr=800) | 2.3e-10 | 0 |

フルスイープ軌道（PH vs two-spin、同一 seed、dqmc_sweep 使用）:

| 条件 | 場の不一致 | max\|ΔG↓\| | max\|ΔE\| |
|---|---:|---:|---:|
| β=8–20, stab=2/4/8, U=4–12（4ケース） | **0** | ≤ 4.5e-12 | ≤ 3.5e-12 |

## 4. 旧バイナリ A/B（HEAD b77afcb を別ビルド、同一入力・同一 seed）

- `1d_L4_U4.txt` 全6β: 完全一致。
- 16×16 β=4: 完全一致。β=8: E の第8有効数字のみ差（誤差 ±16.6 に対し無視可）。
- **dN 列が ~1e-9 → 厳密 0 に変化**: PH では配置ごとに
  $\langle n_\uparrow + n_\downarrow \rangle_s = 1$ が厳密成立するため。
  正しい挙動（半充填の厳密化）だが、旧出力との機械比較時は注意。
- 非二部格子入力（奇数鎖 PBC）は旧新とも同一の入力エラーで拒否 — 挙動一致。

## 5. 性能実測（Mac/Apple Silicon, Accelerate, serial, 16×16, U=4, stab=4）

| | 旧 (b77afcb) | 新 (PH) | speedup |
|---|---:|---:|---:|
| β=4, user time | 0.78 s | 0.41 s | **1.90×** |
| β=8, user time | 1.53 s | 0.78 s | **1.96×** |

- ほぼ理想の 2×（残差は測定・setup 等のスピン非依存部分）。
- β=8 の sweep 平均 0.235 s。**UDV スタック導入前（~5.8 s/sweep）から累計 ~25×**。
- 新内訳（β=8）: 安定化系（stack_build + from_stack + 左 lmul）65% /
  green_update 19% / wrap 11% — 次のレバーは引き続き安定化系
  （`docs/reviews/2026-07-02-delayed-update-next-steps-review.md` の優先順位
  §2 が次の本筋）。

## 6. 軽微メモ（ブロッカーではない、任意フォローアップ)

1. **dN 列の厳密 0 化**（§4）: 正しい挙動だが、旧データとの diff 比較スクリプトが
   ある場合は列の扱いに注意。
2. periodic 安定化ごとの `dqmc_map_ph_down` は、測定用途には sweep 末尾の
   1回で足りる（防御的実装。コスト n² で無害なので現状維持で可）。
3. PH 時も `Gd` が Green としてフル確保される（delay バッファ・LinalgWork・
   B/Binv/tmp ≈ 14n²·8B ≈ 7 MB @n=256/replica）。direct rebuild 検証コードとの
   互換のための意図的な設計だが、メモリが問題になれば将来スリム化可。
4. **解消済み**: sweep レベルの PH vs two-spin テストは chain のみだったが、
   `tests/test_dqmc_stack.c` に square (2×4) ケースを追加した。
5. 旧バイナリと同一 seed でも大きい系では軌道が最終的に分岐する
   （β=8 で第8桁差を観測）。**バージョン間の結果比較は統計一致で行う**こと。

## 7. 残タスク

- kugui i2cpu での old/new 比較（これまでの検証パターン踏襲。
  期待値: sweep ~2×、MPI wall はそれ以上の可能性 — down スピン分の
  メモリ帯域も半減するため）。
- スピン相関測定を将来追加する際は
  `docs/2026-07-02-ph-symmetry-spin-correlations-note.md` の (a) 方式
  （写像済み `Gd.g` を測定に渡す — 本実装がまさにこの形）が既に成立している。

## 追記: Codex確認による補足（2026-07-02 21:23 JST）

上記の主結論（ブロッカーなし、コミット可能）は妥当。後で検証根拠として参照する
文書にするため、次の3点を補足しておく。

1. **MPI / hybrid テストも確認済み。**

   Codex 側では同じ未コミット PH 実装に対して `make test_mpi` と
   `make test_hybrid` も実行し、どちらも PASS を確認した。本文 §2 の表にも
   追記済み。コミット判断の根拠としては serial / slow / OMP / MPI / hybrid の
   全系列が緑、という扱いでよい。

2. **leaks・低温ストレス・旧バイナリ A/B は再現情報を残すとさらに強い。**

   本文 §2–§4 の `leaks --atExit`、低温ストレス、旧バイナリ A/B はレビュー根拠
   として十分有用。ただし、本文だけでは再実行コマンド、使い捨て比較プログラム、
   ログ保存先が分からない。将来この文書を監査・再検証に使う場合は、実行コマンド
   または保存したログ/スクリプトのパスを追記するのが望ましい。Codex 側で
   永続的に再現できる基本検証は `make test`, `make test_slow`, `make test_omp`,
   `make test_mpi`, `make test_hybrid` と `input/bench_2d_L16.txt` 系の profile
   実行である。一方、本文 §3–§4 の使い捨て低温ストレス / 旧バイナリ A/B の
   生ログはこのレビュー文書内には未収載。

3. **「production では PH 経路が常時有効」は CLI 前提として読む。**

   `main` は非二部格子を v1 スコープ外として拒否し、`dqmc_run_replica` は
   `model_init(..., half=1, ...)` を使うため、現在の CLI production 入力では
   半充填・二部格子に限定され、PH 経路が有効になる。一方、ライブラリ/API としては
   `model_init(..., half=0, ...)` や `m.ph_symmetric = 0` による two-spin 経路が
   残っている。したがって本文 §1 の表現は「現在の CLI production 入力では
   PH 経路が常時有効」と読むのが正確。
