---
date: 2026-07-01
status: review
topic: DQMC sign stabilization implementation plan review
target: docs/superpowers/plans/2026-07-01-dqmc-sign-stabilization.md
reviewer: Codex
---

# DQMC Sign Stabilization Plan Review

## Review Scope

対象:

- `docs/superpowers/plans/2026-07-01-dqmc-sign-stabilization.md`
- 関連実装:
  - `src/dqmc.c`
  - `src/green.c`
  - `src/green.h`
  - `src/linalg.c`
  - `src/replica_run.c`
  - `src/replica.c`
  - `tests/test_dqmc.c`
  - `tests/test_integration.c`
  - `Makefile`

観点:

- 計画の前提が現コードの制御フローと一致しているか
- sign reset の実装漏れが起きないか
- 数値失敗時に stale sign を採用しないか
- 回帰テストが修正前に失敗し、修正後に安定して通る設計になっているか

ビルド/テストは実行していない。静的レビューのみ。

## Summary

修正方針そのものは妥当。`green_from_scratch` で各スピンの determinant sign を再取得し、
`Dqmc.sign` を `Gu.det_sign * Gd.det_sign` にリセットする案は、wrap/update drift による
偽符号反転の蓄積を止める方向として正しい。

ただし、計画は現コードに既に存在する sweep 末尾の無条件 `green_from_scratch(..., 0)` を十分に
織り込めていない。また `green_from_scratch` 失敗時に `det_sign` を前回値のまま使う方針は危険。
この 2 点は実装前に計画へ反映する必要がある。

## Findings

### High: sweep 末尾の既存 `green_from_scratch(0)` 後の sign reset が計画上曖昧

計画の Task 5 は、`L % stab != 0` の場合に sweep 末尾へ追加の `green_from_scratch` を入れる
任意タスクとして書かれている。

しかし現コードでは、`dqmc_sweep` の最後で既に毎 sweep 必ず:

- `green_from_scratch(&D->Gu, 0)`
- `green_from_scratch(&D->Gd, 0)`

を呼んでいる。

該当箇所:

- `src/dqmc.c:61-62`
- 計画書 Task 5: `docs/superpowers/plans/2026-07-01-dqmc-sign-stabilization.md:153-160`

このため Task 5 の前提は現コードと矛盾している。追加すべきなのは新しい末尾再構成ではなく、
既存の末尾 `green_from_scratch(..., 0)` の直後に:

```c
D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);
```

を必ず入れること。

計画の File Map / Task 2 は「安定化ブロックで両スピン再構成後」と読めるため、実装者が
`if (((l + 1) % D->stab_interval) == 0)` ブロック内だけに sign reset を追加し、sweep 末尾の
reset を漏らすリスクがある。

影響:

- `Ltr < stab` で sweep 中の periodic stabilization が一度も走らない場合、sign が測定まで
  リセットされない。
- `Ltr % stab != 0` の場合も、最後の stabilization から測定までの drift sign が残る。
- 既存の `dqmc_sweep` 末尾再安定化が G だけを修正し、`D->sign` は古いままになる。

推奨修正:

- Task 2 に「periodic stabilization block の直後」と「sweep 末尾の既存
  `green_from_scratch(..., 0)` の直後」の両方で sign reset する、と明記する。
- Task 5 は削除するか、「既存末尾再安定化後の sign reset 漏れを防ぐ確認」に書き換える。

### High: `green_from_scratch` 失敗時に stale `det_sign` を使う方針が危険

計画では、`udv_inv_one_plus` が非0を返した場合は `det_sign` を前回値のまま据え置き、既存の
エラー扱いに従うとしている。

該当箇所:

- `docs/superpowers/plans/2026-07-01-dqmc-sign-stabilization.md:112-116`
- 現コード: `src/green.c:61`

しかし現コードの `green_from_scratch` は `void` で、`udv_inv_one_plus` の戻り値を捨てている。
呼び出し側へ失敗を伝える既存のエラー扱いは実質ない。

このまま計画通りに実装すると、以下の危険がある。

- `G->g` の再構成に失敗しても、呼び出し側は成功として処理を継続する。
- `det_sign` は前回値のまま残り、壊れた/未更新の Green 関数と stale sign で測定する。
- sign collapse を隠すだけで、数値破綻を別の物理量破綻として残す可能性がある。

推奨修正:

- `green_from_scratch` を `int` 戻り値にして失敗を伝播する。
- 変更範囲を抑えるなら、最低限 `det_sign = 0` など不正状態を明示し、`dqmc_init` /
  `dqmc_sweep` 側で検出して fail-fast する。
- `la_logdet` 失敗時も同様に stale sign を使わない。

### Medium: `la_logdet(G)` の堅牢性説明が強すぎる

計画では `G_σ` は固有値 0..1 で良条件、`la_logdet` は underflow しても符号を堅牢に得られる、
としている。

該当箇所:

- `docs/superpowers/plans/2026-07-01-dqmc-sign-stabilization.md:76`

`G=(I+P)^{-1}` から sign を取る実装は単純で妥当な第一段階だが、低温・強結合では `P` の
特異値レンジが極端に広がり、`G` も非常に小さい特異値を持ち得る。LU pivot の符号が常に
安定とは言い切れない。

推奨修正:

- 計画の表現を「まずは低リスクな単純実装」程度に弱める。
- `la_logdet` 失敗時の fail-fast を必須にする。
- 高 U / 低温 / 大 beta の stress test を validation ladder に追加する。
- 将来的には UDV 分解の `U/D/T` と `udv_inv_one_plus` 内の LU 情報から sign を取得する
  `O(n)`/相乗り方式を検討対象に残す。

### Medium: 回帰テストが修正前に確実に赤くなる保証が弱い

Task 3 は `L=4, U=12, dtau=0.1, beta=4, stab=8, nwarm/nmeas 小` の直列 run で
`replica_bin_values` 成功、sign 1、`E_hub` の `stab=1` 整合を確認するとしている。

該当箇所:

- `docs/superpowers/plans/2026-07-01-dqmc-sign-stabilization.md:137-142`

しかし `nwarm/nmeas 小` では、修正前に `sum_sign == 0` が再現しない可能性が高い。
また `E_hub` の `stab=1` 比較は、短い測定では統計揺らぎで flaky になりやすい。

推奨修正:

- job 840456 と同じ seed、bin 数、per-bin 測定数、または診断で確認済みの最小再現条件を
  明記する。
- unit/regression test と slow diagnostic test を分ける。
- CI 向けには deterministic に drift sign を作る小さなテストを用意し、実機再現は
  validation として扱う。
- `E_hub` 比較は smoke test では緩い sanity check に留め、厳密な統計整合は長時間検証へ
  分離する。

### Low: コスト見積もりが sweep 末尾再安定化を数えていない

計画の Cost Analysis は、from_scratch 回数を `L/stab` 回/sweep としている。

該当箇所:

- `docs/superpowers/plans/2026-07-01-dqmc-sign-stabilization.md:68-75`

現コードでは periodic stabilization に加えて sweep 末尾の `green_from_scratch(..., 0)` が
常にあるため、追加 `la_logdet` は概ね:

```text
2 spin * (floor(L/stab) + 1) per sweep
```

になる。

結論の「ほぼ無コスト」は大きく変わらないが、実装者が実測 call count と計画の表を照合した時に
混乱しないよう、表を更新した方がよい。

## Recommended Plan Edits

実装前に、少なくとも以下を計画へ反映する。

1. Task 2 に「periodic stabilization 後」と「sweep 末尾の既存 `green_from_scratch(0)` 後」の
   両方で `D->sign` をリセットする手順を明記する。
2. Task 5 は削除または再定義する。現コードには既に sweep 末尾再安定化が存在する。
3. `green_from_scratch` / `la_logdet` 失敗時に stale `det_sign` を使わない方針へ変更する。
4. 回帰テストの再現条件を固定し、短時間テストと実機診断を分ける。
5. Cost Analysis に sweep 末尾再安定化分を反映する。

## Conclusion

設計方向は実装してよい。ただし、現コードの `dqmc_sweep` 末尾再安定化を前提に組み直さないと、
肝心の測定直前 sign reset を漏らす可能性がある。さらに、数値失敗時に前回 sign を流用する
計画は危険なので、fail-fast またはエラー伝播を先に決めてから実装するべき。
