---
date: 2026-06-25
datetime: 2026-06-25 10:40 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: 有限温度補助場 QMC 設計・実装計画の収束確認レビュー（第5回）
summary: |
  commit eef0abd で第4回レビュー findings を反映した設計書・実装計画を確認レビューした。
  第4回の 5 findings はすべて設計書・計画の両方に正しく反映済み。file 入力と
  non-bipartite 実行エラーの相互作用は detect_bipartite_from_hopping (2彩色) で解決済みと確認。
  実装着手を妨げる critical/high はなく、計画は実装可能な状態に収束したと判断する。
---

# 有限温度補助場 QMC 設計・実装計画 収束確認レビュー（第5回）

## 対象

- commit: `eef0abd docs: address fourth DQMC review findings`
- 設計書: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md`
- 実装計画: `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md`
- 第4回レビュー: `docs/reviews/2026-06-25-finite-T-aux-field-qmc-plan-fourth-review.md`

## 総評

4 巡のレビュー（初回 Critical 1 + High 3、再 9、第3回 5、第4回 5）で指摘された
物理・数値・文書整合・入力安全性の問題はすべて反映されている。本レビューでは第4回 findings の
反映を1件ずつ突き合わせ、加えて反映の副作用として生じうる新規不整合を独自に探索した。
**実装着手を妨げる critical/high finding はない。** 計画は実装可能な状態に収束したと判断する。

## 第4回 findings の反映確認

| # | 第4回 finding | 反映箇所 | 判定 |
|---|---|---|---|
| 1 | §5 の `H`/`H0` 表記の曖昧さ（μ 二重カウント再誘発） | 設計書 §5 を `H0=K_hop+UΣn↑n↓`、重み `exp[−β(H0−μN)]`、`E_hub=⟨H0⟩`/`E_gc=⟨H0−μN⟩`/`E_ph=⟨H0−(U/2)N+(U/4)n_site⟩` に統一 | ✅ |
| 2 | 縮退サイズの二部性誤判定 | Task 3 で `is_bipartite=(pbc&&Lx>1&&Lx%2)?0:1`、square も `Lx>1`/`Ly>1` 条件付き。`chain(1,pbc)`・`square(1,4,pbc)` のテスト追加 | ✅ |
| 3 | non-bipartite の `sign` は相対符号にすぎない | Task 11 main で non-bipartite を**実行エラー**化、Task 10 注に「将来対応時は初期配置 `det(I+P↑)det(I+P↓)` の符号計算が必要」と明記 | ✅ |
| 4 | `strncpy` の null 終端保証 | Task 11 `lattice` と Task 14 `latfile` の両方でコピー後に `[size-1]='\0'` を明示 | ✅ |
| 5 | `bc` alias の typo を silent OBC | `periodic/open/1/0/pbc/obc` 以外はエラー（`fclose`+`return 1`） | ✅ |

設計書・計画書の frontmatter、反映履歴（「第4回レビュー対応」項）も整備されている。

## 独自に確認した点（反映の副作用）

### 解決済み: `lattice=file` と non-bipartite 実行エラーの相互作用

第4回反映で main は `!is_bipartite` を実行エラーにした。一方 `lattice_from_file` は
任意行列を読むため二部性が自明でない。両者が単純に組み合わさると **bipartite な file 格子まで
弾かれて Task 14（ファイル入力）が死にコード化**する懸念があった。

確認の結果、`lattice_from_file` は `detect_bipartite_from_hopping`（ホッピンググラフの BFS 2彩色）で
二部性を実判定しており、この懸念は解消済みである。実装も検証した:

- 非連結成分ごとに彩色を開始（`for s … if(bipart[s]!=0) continue`）。
- 孤立点は `+1` のまま（self-bond なしで矛盾なし）。
- 奇閉路で `is_bipartite=0`。
- 閾値 `1e-12` で hopping の有無を判定（dump→read の往復でも安定）。
- `test_lattice_file` が `F.is_bipartite==1`（4-chain）を確認。

真に二部な file 格子は v1 を通過し、非二部は他の格子と同様に弾かれる。整合は取れている。

## 残る軽微な注記（修正不要）

- 計画書「レビュー反映履歴」#5 に「非二部半充填を**警告**」とあるが、これは第1回時点の記録で、
  第4回 #26（**実行エラー**化）が上書きしている。changelog としては正確であり、現行仕様の単一情報源は
  設計書 §1/§9 と Task 11 main（実行エラー）である。
- `non-bipartite` を将来サポートする場合の初期 determinant sign 計算（Task 10 注）は v1 非スコープ。
  実装時に着手するのではなく、必要になった時点で別タスク化する。

## 検証時に注視すべき箇所（実装フェーズ向けメモ）

レビュー上の欠陥ではないが、実装で最初に壊れやすい順に:

1. **補助場 N の符号**（A.93）: `green_flipN` が反転前の `s` を読むこと。Task 7 の両スピンテストが回帰検出器。
2. **UDV 安定化の公式**（`g=T^{-1}M^{-1}diag(1/Db)U^T`）: `diag(1/Db)` が `M^{-1}` の列に作用すること。
3. **column-major と右掛け対角行列**（`B=expK·diag(d)` の列スケール）: Task 15 の B_l テストが検出器。
4. **low-T 安定化間隔**: 大 β で rank-1 更新と from_scratch の残差が許容外なら `stab` を小さくする。

## 結論

4 巡のレビューを経て、物理（補助場更新の符号、グリーン関数規約、半充填 μ）、数値（UDV 安定化、
Trotter 外挿、ジャックナイフ）、検証設計（grand-canonical ED 原則、規約変換、誤差出力）、
入力安全性（key alias、null 終端、二部性判定）まで一貫している。
**計画は実装着手可能な状態に収束した。** 次フェーズは Task 0 からの TDD 実装に進んでよい。
