---
date: 2026-06-25
datetime: 2026-06-25 10:04 JST
model: Codex (GPT-5)
summary: |
  commit 3f3944a で再レビュー findings を反映した設計書・実装計画を三度目にレビューした。
  μ 二重カウント、E_gc/E_ph 誤差出力、down-spin テスト、非対称 hopping 拒否などは修正済み。
  残課題は主に文書間の古い出力列、入力 key 名の不一致、square の縮退サイズ入力検証である。
---

# 有限温度補助場 QMC 設計・実装計画 第3回レビュー

## 対象

- commit: `3f3944a docs: Codex 再レビュー反映（μ二重カウント・誤差出力ほか）`
- 設計書: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md`
- 実装計画: `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md`
- 前回再レビュー: `docs/reviews/2026-06-25-finite-T-aux-field-qmc-plan-rereview.md`

## 総評

前回再レビューの high findings は実質的に修正されている。grand-canonical ED の式は `H0` と `exp[-β(H0−μN)]` に統一され、`E_gc/E_ph` の誤差列も main 出力側に追加された。down-spin を含む `green_update` テスト、任意格子入力での非対称 hopping 拒否、`jackknife` の `N<2` ガード、設計書・計画書の frontmatter 追加も反映済みである。

残っている問題は、主に設計書・VALIDATION 案・main 出力の記述不一致と、入力パラメータ名や縮退サイズの扱いである。実装前に直すと、実装者と検証者の混乱を避けられる。

## Findings

### High: `VALIDATION.md` 案の出力列がまだ古い

該当箇所:

- 計画書 Task 13 `VALIDATION.md` 案

Task 11 の main 出力は次の新しい列に更新されている。

```text
T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign
```

しかし Task 13 の `VALIDATION.md` 案はまだ古い列を記載している。

```text
T  E_hub  dE  E_gc  E_ph  ntot  dN  doublon  dD  sign
```

さらに `E_gc=⟨H−μN⟩` と書かれており、後段で導入した `H0` 表記と揺れている。

修正案:

```text
QMC 出力列:
T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign

E_hub = <H0>
E_gc  = <H0 - μN>
E_ph  = <H0 - (U/2)N + (U/4)n_site>
```

### Medium: 設計書側に古い出力仕様が残っている

該当箇所:

- 設計書 §8 ビルド・実行

設計書 §5 では `E_hub/E_gc/E_ph` を出力する設計になっているが、§8 の出力例はまだ次のままである。

```text
T  E  dE  doublon  sign
```

これは Task 11 の main 出力と矛盾している。

修正案:

```text
T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign
```

に統一する。§7 の `E ± δE` も、必要なら `E_hub/E_gc/E_ph` の各誤差を出すと補足する。

### Medium: 入力 key 名が設計書と実装計画でずれている

該当箇所:

- 設計書 §8 入力例
- 計画書 Task 11 `params_read`

設計書は `bc=periodic` と `stabilize_interval` を例示している。一方、計画書の parser は `pbc` と `stab` を読む。

このままだと、設計書の入力例に従ったユーザーが `bc=periodic` や `stabilize_interval=8` を書いても、実装側では無視される可能性がある。

修正案:

- 設計書を `pbc=1`, `stab=8` に寄せる。
- あるいは parser で `bc` と `stabilize_interval` を alias として受ける。

実装者向けには alias を受ける方が堅牢である。

### Medium: `square` lattice で `Lx==1` または `Ly==1` かつ PBC の場合に self-bond が入る

該当箇所:

- 計画書 Task 3 `lattice_square`

`lattice_square` は PBC 境界で `IDX(0,y)` や `IDX(x,0)` へ bond を追加する。`Ly==1` の PBC では y 方向が自分自身へ戻るため、`add_bond(L,i,i,thop)` となり、対角成分が 2 回加算される。

これは chain として扱うべき入力が square に流れた場合に、意図しない onsite term を作る。

修正案:

- `lattice_square` は `Lx>=2 && Ly>=2` を要求する。
- `Ly==1` は `lattice_chain` を使うよう main 側で分岐する。
- あるいは self-bond を検出して追加しない。

### Low: Task 13 で作る `VALIDATION.md` に frontmatter がない

該当箇所:

- 計画書 Task 13 `VALIDATION.md` 案

設計書・計画書には frontmatter が追加されたが、Task 13 で新規作成する `VALIDATION.md` は `# 検証手順...` から始まっている。AGENTS.md の文書作成ルールに従うなら、`VALIDATION.md` にも `date/datetime/model/summary` を入れる必要がある。

修正案:

Task 13 の `VALIDATION.md` テンプレート冒頭に frontmatter を追加する。

## 前回 Findings の反映状況

- μ 二重カウント: 修正済み。`H0` と `exp[-β(H0−μN)]` に統一された。
- `E_gc/E_ph` 誤差出力: main 出力側は修正済み。`VALIDATION.md` 案に古い列が残る。
- 2D ED の現実性: 修正済み。小 OBC 系に寄せられた。
- 任意格子入力の対称性検証: 修正済み。
- down-spin 更新テスト: 修正済み。
- `jackknife N=1`: 修正済み。
- docs frontmatter: 設計書・計画書は修正済み。Task 13 の新規 `VALIDATION.md` テンプレートは未対応。

## 結論

実装を壊す可能性が高い物理・数値上の critical な問題は見当たらない。残りは文書と入力仕様の整合性が中心である。

次の修正では、`VALIDATION.md` 案の出力列、設計書 §8 の出力例、入力 key 名の統一、`square` の縮退サイズ入力検証を優先すればよい。
