---
date: 2026-06-25
datetime: 2026-06-25 09:50 JST
model: Codex (GPT-5)
summary: |
  commit 7f1ecd1 でレビュー findings を反映した設計書・実装計画を再レビューした。
  補助場 flip の N 符号は API レベルで妥当に修正されている。
  一方で grand-canonical ED の μ 二重カウント、E_gc/E_ph 誤差出力不足、
  任意格子入力の対称性検証不足など、実装前に直すべき点が残る。
---

# 有限温度補助場 QMC 設計・実装計画 再レビュー

## 対象

- commit: `7f1ecd1 docs: Codex レビュー反映（補助場N符号バグ修正ほか）`
- 設計書: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md`
- 実装計画: `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md`
- 前回レビュー: `docs/reviews/2026-06-25-finite-T-aux-field-qmc-plan-review.md`

## 総評

前回レビューの主要 findings は概ね反映されている。特に critical だった補助場 flip の `N_{imσ}` 符号は、`green_flipN` で反転前の場から一度だけ計算し、`green_ratio_N` と `green_update(..., N)` に同じ値を渡す API に変更されており、pre/post の取り違えを構造的に避ける設計になった。この修正は妥当である。

また、`ntot`、`E_hub`、`E_gc`、`E_ph` の導入、`beta/dtau` の整数性チェック、二部性判定、2x2 PBC の定量比較除外、任意格子入力タスク追加も方向性はよい。

ただし、修正で追加された grand-canonical ED の説明と出力列まわりに、検証時の誤比較につながる不整合が残っている。実装前に以下を直すべきである。

## Findings

### High: grand-canonical ED の式で μ を二重に引いている

該当箇所:

- 計画書 Task 13 `VALIDATION.md` 案の grand-canonical ED 説明

現在の文では、ED 側について次のように書かれている。

```text
H = K_hop + U n↑n↓ − μN
grand-canonical trace Tr[O e^{−β(H−μN)}]/Tr[e^{−β(H−μN)}]
```

この定義では、`H` がすでに `−μN` を含んでいるにもかかわらず、指数でさらに `H−μN` としているため、実質的に `−2μN` になる。

修正案はどちらかに統一すること。

```text
H0 = K_hop + U n↑n↓
Z = Tr exp[-β(H0 − μN)]
E_gc = <H0 − μN>
```

または

```text
H_gc = K_hop + U n↑n↓ − μN
Z = Tr exp[-β H_gc]
E_gc = <H_gc>
```

QMC 出力の `E_gc` は `E_hub − μ*ntot = <H0 − μN>` なので、検証文書でもこの定義に合わせるのが安全である。

### High: `E_gc` / `E_ph` の誤差を計算しているが出力していない

該当箇所:

- 計画書 Task 11 の main 出力

main の計画では `jackknife(Egc, ..., &Eg, &Ege)` と `jackknife(Eph, ..., &Ep, &Epe)` を計算しているが、出力列は次の形になっている。

```text
T  E_hub dE  E_gc  E_ph  ntot dN  doublon dD  sign
```

比較対象として `E_gc` や `E_ph` を使う設計にした以上、それぞれの誤差も出力しないと、ED/TPQ 照合時の許容差評価に使えない。

修正案:

```text
T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign
```

`printf` も `Eg/Ege`、`Ep/Epe` の両方を出すように更新する。

### Medium: 2D 検証対象を 4x4 にしたことで ED 検証の現実性が落ちている

該当箇所:

- 設計書 §6 検証戦略
- 計画書 Task 13 2D の注意

2x2 PBC を定量比較から外す判断は妥当である。一方で、代替として `4x4` を小クラスタ ED/TPQ 照合の例に置くと、grand-canonical ED では Hilbert space が非常に大きくなり、2日ハッカソンの検証対象として現実性が落ちる。

修正案:

- ED 照合用 2D は `2x2 OBC`、`2x3`、`2x4` などを明示する。
- `4x4` は TPQ または別手法向けの検証候補として分けて記述する。
- 2D 検証では必ず DQMC が dump した hopping matrix を ED 側へ渡す。

### Medium: 任意格子ファイル入力が非対称 hopping を拒否しない

該当箇所:

- 計画書 Task 14 `lattice_from_file`
- 計画書 Task 4 `model_init`

Task 14 の `lattice_from_file` は dense 行列をそのまま読み込む。一方、Task 4 の `model_init` は `la_expm_sym` を使っており、これは対称行列を前提に `dsyev` で行列指数を作る。

非対称 `t_ij` が入力された場合、`dsyev` は片三角だけを見て計算するため、実際に意図した hopping matrix とは異なる行列指数を静かに作る可能性がある。

修正案:

- `lattice_from_file` で `fabs(t_ij - t_ji) < tol` を検証する。
- 非対称行列はエラーにする。
- 将来、非対称 hopping を扱うなら `la_expm_sym` ではなく一般行列指数の実装が必要、と明記する。

### Medium: down-spin の `green_update` テストがコード例に入っていない

該当箇所:

- 計画書 Task 7 `tests/test_green_update.c`

計画書には「上向き・下向き両スピンで同一テストを行うこと」と注記されているが、実際のコード例は `green_alloc(..., +1.0)` の up-spin のみである。

`N = exp(-2λσs)-1` の符号修正は σ に依存するため、down-spin を実コード例に入れておく方が安全である。

修正案:

- `sigma = +1.0, -1.0` のループで同じ ratio/update/from_scratch テストを実行する。
- または up/down それぞれの `Green` を作って、両方を明示的に検証する。

### Low: `jackknife()` は `N=1` でゼロ割りする

該当箇所:

- 計画書 Task 9 `jackknife`
- 計画書 Task 11 input handling

`jackknife()` は `(sum-x[i])/(N-1)` を使うため、`N=1` でゼロ割りする。現状の入力パーサでは `nbin=1` や `nmeas<nbin` を拒否していない。

修正案:

- `params_read` 後に `nbin >= 2` を検証する。
- `nmeas >= nbin` を要求する。
- `jackknife()` 側でも `N < 2` の場合は `err=0` などにするか、エラー扱いを明示する。

### Low: AGENTS.md の docs frontmatter ルールにはまだ未対応

該当箇所:

- 設計書
- 実装計画

`AGENTS.md` は `docs/` 配下の文書作成・追記時に、冒頭へ日時、モデル名、summary を書くことを要求している。今回更新された設計書と計画書は既存文書だが、運用ルールを厳密に守るなら frontmatter を追加するべきである。

修正案:

- 設計書と計画書の冒頭に frontmatter を追加する。
- 既存タイトルは frontmatter の後に置く。

## 前回 Findings の反映状況

- `N` 符号: 反映済み。API 変更によりかなり堅牢になっている。
- ED/TPQ ensemble: 方針は反映済み。ただし μ 二重カウントの記述修正が必要。
- エネルギー定義と出力: 方針は反映済み。ただし `E_gc` / `E_ph` の誤差出力が不足。
- Trotter 誤差: 反映済み。
- 二部性チェック: 反映済み。
- 2x2 PBC: 定量比較除外として反映済み。ただし代替 2D ED 対象の現実性を調整したい。
- 任意格子入力: Task 14 として反映済み。ただし対称性検証が必要。
- `beta/dtau`: 反映済み。
- git path / main include: 反映済み。

## 結論

commit `7f1ecd1` による修正で、前回レビューの critical/high の大部分は正しい方向に改善された。特に `N` 符号の修正は実装前計画として十分に妥当である。

次に直すべき最優先は、grand-canonical ED の μ 二重カウント記述と、`E_gc` / `E_ph` の誤差出力不足である。この2点を直せば、検証文書と実行出力の整合性がかなり改善する。
