---
date: 2026-06-25
datetime: 2026-06-25 10:48 JST
model: Codex (GPT-5)
status: review
topic: 有限温度補助場 QMC 設計・実装計画の第5回レビュー確認
summary: |
  commit 149d424 の第5回レビュー文書を確認し、第4回 findings の反映確認は妥当と判断した。
  一方で入力安全性について、lattice key の typo が chain に silent fallback する点と、
  file hopping の対角成分を拒否しない点が残っていた。
  どちらも実装前に計画へ追記すべき軽微な残課題として整理する。
---

# 有限温度補助場 QMC 設計・実装計画 第6回レビュー

## 対象

- commit: `149d424 docs: 収束確認レビュー（第5回）を追加`
- 第5回レビュー: `docs/reviews/2026-06-25-finite-T-aux-field-qmc-plan-fifth-review.md`
- 設計書: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md`
- 実装計画: `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md`

## 総評

第5回レビューの主結論、すなわち第4回レビュー 5 findings の反映確認は妥当である。
`H0/E_gc` 表記、縮退サイズの二部性、non-bipartite 実行エラー、`strncpy` 終端、
`bc` alias の typo 検出はいずれも設計書・計画書へ反映されている。

ただし、第5回レビューの「入力安全性まで一貫」という結論は少し強い。実装前に潰しておくべき
入力検証の残件が 2 点ある。どちらも物理式や数値アルゴリズムを揺らすものではないが、
typo や想定外入力を静かに別モデルとして走らせる類の不具合なので、計画へ追記する。

## Findings

### Medium: `lattice` key の typo が silent chain fallback になる

該当箇所:

- 計画書 Task 11 `params_read`
- 計画書 Task 11 `main`

現在の Task 11 では `lattice` 文字列を保存するだけで、許可値を検証していない。
`main` 側も `square` 以外をすべて `chain` として扱うため、例えば `lattice=sqaure` は
エラーにならず 1D chain として実行される。第4回で `bc` alias の typo をエラーにした方針と
同じく、`lattice` も許可値を明示して検証するべきである。

修正方針:

- Task 11 では `chain|square` 以外をエラーにする。
- Task 14 で `lattice=file` を追加する際に、許可値検証も `chain|square|file` へ拡張する。
- `main` 側も防御的に `chain`/`square`/`file` を明示分岐し、未知値はエラーにする。

### Medium: `lattice_from_file` が diagonal hopping を拒否しない

該当箇所:

- 設計書 §2 `w_i=0`
- 計画書 Task 14 `lattice_from_file`

設計書では onsite disorder `w_i` は当面 0 としており、組み込み lattice も self-bond を避けて
`t_ii=0` を前提にしている。一方、Task 14 の dense file 入力は対称性だけを検証し、
`t_ii != 0` を許してしまう。これは onsite potential/disorder を hopping 行列経由で入れる経路になり、
半充填二部格子の sign-free 前提や ED 比較規約を静かに崩し得る。

修正方針:

- v1 の file hopping は対称かつ対角ゼロに限定する。
- `lattice_from_file` で `fabs(t_ii) > 1e-12` をエラーにする。
- `tests/test_lattice_file.c` に diagonal 非ゼロファイルを拒否するテストを追加する。

## 結論

第5回レビューの反映確認は妥当だが、入力安全性の残件として上記 2 点を計画に追記する。
この修正後は、実装フェーズで typo や想定外 file 入力が静かに別モデルへ化けるリスクが下がる。
