---
date: 2026-06-25
datetime: 2026-06-25 14:00 JST
model: Codex (GPT-5)
summary: |
  有限温度 DQMC コード開発の振り返り。
  実装前レビュー 7 回、実装後レビュー 1 回、実装本体約 20 分という進め方を整理した。
  詳細な博士論文付録を source of truth として使えたことが、物理規約・符号・測定式の検証に大きく効いた。
---

# DQMC コード開発振り返り

## 概要

今回の有限温度補助場 QMC、特に DQMC/BSS 実装では、AI を単なるコード生成器としてではなく、設計・実装計画・実装済みコードの厳しいレビュー担当として使ったことが大きかった。

もう一つ大きかった点は、手法の詳細が書かれた博士論文付録があったこと。Hubbard-Stratonovich 変換、Green 関数の定義、determinant ratio、rank-1 update、wrapping、UDV 安定化、測定式を具体的な式に照らして確認できたため、一般論ではなく「この式に対してこの実装が合っているか」をレビューできた。

## 数字

- 設計・実装計画レビュー: 7 回
- 実装後コードレビュー: 1 回
- レビュー文書合計: 8 本
- 実装前レビューと修正にかかった時間: 約 1 時間 50 分
  - 初回レビュー文書: 2026-06-25 09:24 JST
  - 実装前最終修正 commit: 2026-06-25 11:16 JST
- 実装本体にかかった時間: 約 20 分
  - scaffold 開始: 2026-06-25 11:21:13 JST
  - DQMC 本体、テスト、U=4 sign 修正まで: 2026-06-25 11:41:44 JST
- 実装後レビューと軽微修正: 約 24 分
  - 実装完了: 2026-06-25 11:41:44 JST
  - 実装レビュー後 follow-up fix: 2026-06-25 12:05:29 JST
- ソース行数: `src/*.[ch]` で 1,420 行
- テスト行数: `tests/*.c` で 820 行
- テスト数: 15 本
- `src + tests + Makefile + input + VALIDATION.md`: 2,391 行

## 効いた進め方

1. 実装前に design/plan を徹底レビューする。
2. findings を文書化し、妥当性を確認してから計画へ反映する。
3. 修正後に再レビューし、同じ種類の不具合が残っていないか確認する。
4. 実装は Task 0 から Task 15 まで小さい TDD 単位で進める。
5. 実装後に、計画適合性・潜在バグ・物理式との対応を別タスクとして再レビューする。
6. U=0 解析解、半充填の `sign=1`、`ntot=L`、ED 比較、`dtau^2` 外挿を validation ladder にする。
7. 計算結果と図は `data/` に整理し、LOG とともに commit する。

## 博士論文付録が効いた点

博士論文付録があったことで、AI レビューの質がかなり上がった。特に QMC では、見た目には正しく見えるが物理的には壊れるバグが多い。

- 補助場 flip の `N` の符号
- Green 関数の規約が `<c_i c_j†>` なのか `<c_i† c_j>` なのか
- `E_hub=<H0>`、`E_gc=<H0-mu N>`、particle-hole symmetric 形式の定数シフト
- grand-canonical ED と canonical ED の比較対象
- `dtau` 有限値での比較と `dtau→0` 外挿後の比較
- column-major 実装での右掛け対角行列、すなわち列スケール

これらは一般的な QMC 知識だけでは見落としやすい。博士論文の式番号と照合できたことで、レビューを「もっともらしい実装か」ではなく「式と一致しているか」にできた。

## 共有用の英語メモ

> The biggest tip is that I used Claude/Codex much more as a reviewer than as a code generator. Before writing code, I made it review the design and implementation plan 7 times, fixing every finding and committing each round. That took about 1h50m. After that, the actual DQMC implementation was surprisingly fast: about 20 minutes for around 1,420 lines of C source/header code plus 820 lines of tests. Then I did one post-implementation review and follow-up fix, about another 24 minutes.
>
> Another big factor was having a detailed reference document: a PhD thesis appendix describing the finite-temperature auxiliary-field QMC method. That made it possible to check the implementation against concrete equations, not just generic knowledge. In particular, we could tie the code to specific formulas for the Hubbard-Stratonovich field, Green function definition, determinant ratio, rank-1 update, wrapping, stabilization, and measurements.
>
> For QMC, the important part was not typing code quickly, but forcing clear conventions: Green function definition, energy definition, chemical potential shift, grand-canonical ED comparison, sign checks, and dtau extrapolation. Most dangerous QMC bugs are convention, sign, or normalization bugs.

