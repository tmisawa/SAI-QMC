---
date: 2026-07-14
datetime: 2026-07-14 13:24 JST
model: Claude Fable 5 (claude-fable-5)
summary: |
  green_rebuild Phase 1 実装（option 追加・parser・stdout header・未実装 two_sided の
  明示エラー・IO tests）と、改訂版実装計画・roadmap HTML のレビュー。
  実装は承認。`make test` 全通過・header 出力・two_sided エラー停止をレビュアー側で
  独立に再現確認した。改訂版計画の two-sided 恒等式・det sign 式も検算済みで正しい。
  指摘は軽微 4 点（runtime guard の自動テスト欠如、roadmap の文言、commit 分割、
  Phase 4 での guard 撤去漏れ防止）。
---

# green_rebuild Phase 1 実装レビュー

対象:

- `docs/2026-07-14-udv-green-rebuild-roadmap.html`（roadmap、新規）
- `docs/2026-07-14-udv-log-scale-implementation-plan.md`（改訂版）
- `src/io.h`, `src/io.c`, `src/main.c`, `tests/test_io.c`（Phase 1 実装）
- `LOG.md` 2026-07-14 12:02 JST エントリ

前レビュー: `docs/2026-07-14-udv-log-scale-plan-review.md`（条件付き承認、M1-M3）

## 総合判定: 承認

Phase 1 実装はコード・テストとも妥当。前レビューの重大指摘 M1-M3 は改訂版計画に
すべて反映されており、方針転換（two-sided Db/Ds を第一候補、log-scale は
Phase 6 に格下げ、実装前に spread 診断）は前レビューの推奨アクション順序と一致する。

## レビュアー側で独立に再現した検証

- `make test` → `ALL TESTS PASSED`（`tests/test_io` 含む全 serial テスト）。
- `./dqmc input/1d_L4_U4.txt` の header:
  `# lattice=chain n=4 U=4 mu=2 dtau=0.1 bipartite=1 green_rebuild=combine
  parallel=serial nrep=1 bins=30` — `green_rebuild=` の出力を確認。
- `green_rebuild=two_sided` の入力で
  `ERROR: green_rebuild=two_sided is planned but not implemented yet`、
  exit code 1 を確認（silent fallback なし）。
- `git diff --check` クリーン。
- LOG.md の検証主張（`make test` / header / two_sided エラー）と再現結果が一致。

## コードレビュー結果

### `src/io.h` / `src/io.c` — 問題なし

- `green_rebuild[16]` は `"two_sided"`（9 文字 + NUL）に十分。
- default `combine` の設定位置（`defaults()`）、パーサ分岐、末尾の許容値検証は
  既存 `sweep_order` と完全に同型で、エラーメッセージ形式も既存規約に一致。
- unknown value は `ERROR: green_rebuild must be combine or two_sided (got %s)`
  で fail。typo の silent 受理なし。

### `src/main.c` — 問題なし

- two_sided guard は `params_read` 直後・lattice 確保前に置かれており、
  エラー経路でのリーク無し。root-only stderr + `mpi_finalize_if_enabled` +
  `return 1` は隣接エラー経路（`lattice=file` 検証等）と同型。
- header への `green_rebuild=%s` 挿入は key=value トークン形式。`scripts/` /
  `tests/` に header 行を位置依存でパースするものが無いことを確認済み
  （トークン挿入は下流互換）。
- roadmap の decision gate「Phase 1 では `replicas.csv` に列を足さない」は
  遵守されている（CSV header 変更なし）。

### `tests/test_io.c` — 問題なし

- default が `combine`、`combine`/`two_sided` 受理、不正値
  （`green_rebuild=log`）拒否の 4 点をカバー。前レビューが要求した
  Phase 1 テストマトリクスと一致。

### 改訂版実装計画 — 数式検算済み、正しい

two-sided の核となる恒等式を独立に再導出して確認した:

- `U_l D_lb (D_lb^{-1} U_l^T T_r^{-1} D_rb^{-1} + D_ls M D_rs) D_rb T_r
  = I + U_l D_l M D_r T_r` ✓（`D_lb D_ls = D_l`、対角行列は可換）
- `G = T_r^{-1} D_rb^{-1} H^{-1} D_lb^{-1} U_l^T` ✓
- det sign: `det(I+A) = det(U_l)·det(D_lb)·det(H)·det(D_rb)·det(T_r)`,
  `det(T_r)=1` ✓
- 構成要素 `|D_lb^{-1}|, |D_rb^{-1}|, |D_ls|, |D_rs| ≤ 1` で cross product
  `D_l[i]·D_r[j]` を作らない、という主張 ✓

前レビュー指摘への対応状況:

| 指摘 | 対応 | 判定 |
|---|---|---|
| M1 (global offset の headroom) | 実装前に Phase 2 で spread 実測診断、受け入れ基準を「再現 run 完走 + 有限 observables」に強化（計画 Phase 5/6 検証項目） | 解消 |
| M2 (lmul/rmul の log 入力未設計) | log-scale 全体を Phase 6 に deferral、parser も `log` を受けない decision gate 明記 | 解消（先送りとして妥当） |
| M3 (two-sided 未検討) | two-sided を第一候補に採用、`green_from_boundary_factors()` 1 箇所に閉じる設計 | 解消 |
| S1 (mode の所在) | `Green.rebuild_mode` 案を明記、`dqmc_record_stab_drift()` 経路も理由に含む | 解消 |
| S4 (メタデータ) | stdout header 必須・`replicas.csv` 変更回避・`run_info.txt` はスクリプト側、と具体化 | 解消 |

### roadmap HTML — 内容正確、文言 1 点

- 実装状態・文書状態の記載は実際のファイル・working tree と一致することを確認。
- frontmatter 相当（datetime/model/summary）は HTML コメントと可視 meta box の
  両方にあり、文書作成ルールを満たす。

## 指摘事項（すべて軽微）

1. **two_sided runtime guard に自動テストがない。** 現状は手動確認のみ
   （LOG 記載）。guard は Phase 4 で撤去される一時措置なので必須ではないが、
   Phase 4 で「guard 撤去 + two_sided 実行」への差し替えを忘れると
   two_sided が永久に起動不能になる。roadmap の Phase 4 exit criteria に
   「main.c の guard 撤去」を明示しておくことを推奨。
2. **roadmap Phase 4 exit criteria の「combine stays bit-for-bit close」は
   自己矛盾気味。** two-sided は別関数として追加され combine 経路は無変更の
   はずなので、正しくは「combine は bit-identical（経路無変更）、two_sided は
   統計誤差内で一致」。改訂版計画本文の表現と揃えるとよい。
3. **working tree に 2 つの論理変更が混在。** 現在の diff（18 files,
   +1242 行）には前日までの fail-fast 実装（`measure/replica/jackknife`
   finite check 群）と Phase 1 が混ざっている。commit 時は
   「fail-fast 診断」「green_rebuild option (Phase 1)」を別 commit に
   分割することを推奨（TDD バイトサイズ刻みの方針とも整合）。
4. **`jackknife_and_print()` の void → int 変更**（fail-fast 側の変更）は
   serial 経路で return 1 時に cleanup してから exit しており正しいが、
   MPI 経路の `root_post_failed` → `mpi_any_failed` → `goto mpi_beta_cleanup`
   の合流も確認した。問題なし — ただしこれは Phase 1 ではなく fail-fast
   commit に属する変更であり、commit メッセージで区別すること（指摘 3 と同件）。

## 次フェーズへの申し送り

- Phase 2（spread 診断）: 診断出力は production default の stdout を変えない
  こと（roadmap の exit criteria 通り）。stderr または opt-in ファイルを推奨。
- Phase 3（`udv_inv_one_plus_two_sided_work()`）: unit test の dense reference
  比較は factor 単位でなく `g` / det sign 単位で行うこと（QR 符号不定性、
  前レビュー S3）。moderate case では現行
  `udv_combine + udv_inv_one_plus_work` との一致テストも入れると
  回帰検出が強くなる。
- Phase 4: guard 撤去（指摘 1）と `Green.rebuild_mode` の伝播先一覧
  （`green_from_stack` / `green_from_boundary_factors` / alternating backward
  経路）の確認。
