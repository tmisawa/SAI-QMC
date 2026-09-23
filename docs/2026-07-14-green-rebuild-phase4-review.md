---
date: 2026-07-14
datetime: 2026-07-14 18:14 JST
model: Claude Fable 5 (claude-fable-5)
summary: |
  Phase 4（green_rebuild=two_sided の Green 統合）の実装レビュー。判定は承認。
  mode 伝搬・guard 撤去・テストを検証し、さらにレビュアー側の実行検証で
  (1) default combine 経路の bit 不変（%.17g TSV が diff クリーン）、
  (2) 再現 seed の失敗が udv_combine stage=C から udv_lmul_work stage=B_U_D
  （単一 factor site）へ移動 — Phase 5 gate (a) を既に満たす —、
  (3) beta=30 two_sided 完走、を確認した。残る Phase 5 作業は beta=30-32 の
  統計的一致検証のみ。
---

# green_rebuild=two_sided Green 統合 (Phase 4) 実装レビュー

対象:

- `src/green.h` / `src/green.c`: `GreenRebuildMode`,
  `green_set_rebuild_mode()`, `green_from_boundary_factors()` の mode 分岐
- `src/dqmc.h` / `src/dqmc.c`: `Dqmc.green_rebuild_mode`,
  `dqmc_set_green_rebuild_mode()`, stab_drift 参照への伝搬
- `src/replica_run.c`: `green_rebuild_mode_from_params()` と init 直後の設定
- `src/main.c`: 一時 runtime guard の撤去
- `tests/test_green_stack.c`: 両 mode の boundary reconstruction 照合
- roadmap（Phase 4 done / Phase 5 next）、`LOG.md`

## 総合判定: 承認

統合は設計（改訂版計画）どおりで、前回申し送りの注意点がすべて反映されている。
レビュアー側の実行検証により、**改訂 roadmap の Phase 5 gate (a)（失敗の
単一 factor site への移動）は既に満たされている**ことを確認した。

## コードレビュー結果（すべて妥当）

- **mode 分岐** (`green_from_boundary_factors()`): two_sided は
  `udv_inv_one_plus_two_sided_work(prefix, suffix, ...)` を直接呼び
  `combined` は `(void)` で未使用化（API 互換維持 — 計画どおり）。combine は
  従来と同一の演算列。未知 mode は `work.failed=1` の defensive 分岐。
  det_sign / cur_l / delay_count の後処理は両 mode 共通で従来と同一。
- **mode の所在と伝搬**: `Green.rebuild_mode`（default `COMBINE` を
  `green_alloc()`/`green_free()` で設定）。`dqmc_set_green_rebuild_mode()` は
  Gu/Gd に加え **stab_drift 参照 Green にも伝搬**し、
  `dqmc_enable_stab_drift()` 側でも `D->green_rebuild_mode` から再同期 —
  設定順序（enable が先でも後でも）双方をカバー。前回申し送りの
  「stab drift 参照だけ別 mode になる事故」は構造的に防がれている。
- **設定タイミング**: `replica_run.c` は `dqmc_init_mode()` 成功直後に設定。
  init 中の Green 再構成は `green_from_scratch()`（単一 factor 経路、
  mode 非依存）のみなので、init 後設定で正しい。
- **guard 撤去**: `main.c` に `two_sided` の文字列は残っていない（grep で確認）。
  roadmap decision gate を満たす。
- **parser 整合**: `green_rebuild_mode_from_params()` の unknown → COMBINE は
  parser 検証（combine|two_sided 以外は error）により到達不能の defensive。
- **テスト**: `check_boundary_reconstruction()` が全 boundary で
  from-scratch 参照・det_sign・brute-force det sign と照合し、これを
  `COMBINE` / `TWO_SIDED` の両 mode で実行。7 構成（L=5–10, stab=1–8,
  U=4/12）に適用され、green レベルの回帰網は強い。

## レビュアー側で独立に再現した検証

1. `make test` → `ALL TESTS PASSED`。
2. **default combine の bit 不変**: 再現 seed（beta=33.325, combine,
   診断付き）を Phase 4 後に再実行し、`%.17g` 精度の診断 TSV 331 行が
   Phase 2.1 時点の出力と **bit 一致**、失敗も同一
   （`udv_combine stage=C`, boundary 165）。default 経路の数値経路は不変。
3. **再現 seed + two_sided**: boundary 165 の combine（cross=710.68）を
   **通過**し、boundary=166 (tau=1328) の pair 行が出現。その後
   end-of-sweep の延長 lmul で
   `udv_lmul_work stage=B_U_D value=inf` により fail-fast。
   **失敗が単一 factor site へ移動 — Phase 2 review の定量予測
   （left_max=708.15 + ブロック成長で 709.78 超え）どおりで、
   改訂 roadmap Phase 5 gate (a) を満たす。**
4. **beta=30 + two_sided**: rc=0 で完走、header に
   `green_rebuild=two_sided` を確認。観測値は combine run と印字精度
   （8 桁）で一致 — 丸め差による accept/reject flip の期待回数が
   この run 長では ~1e-8 のため、bit 級一致は想定どおり
   （two_sided が実際に分岐していることは 3. の挙動変化が証明）。

## 軽微な指摘

- **DQMC レベルの two_sided sweep テストがない**: green レベルの両 mode 照合は
  強いが、`dqmc_sweep()` を two_sided で回す回帰テスト（例:
  `test_dqmc_stack.c` の new/ref 比較を two_sided 化、または stab_drift を
  two_sided で 1 ケース）を Phase 5 で 1 本追加すると、mode 伝搬の退行を
  検出できる。
- **alternating + two_sided** は `dqmc_backward_boundary()` が同じ
  `green_from_boundary_factors()` を通るため理論上カバーされるが、
  実行レベルのテストは combine のみ。上と同様 Phase 5 で 1 ケース推奨。
- 性能: two_sided は QR（combine）の代わりに LU 逆行列 1 回 + gemm 数回で
  同オーダー。必要になったら profile.csv で比較（現時点で不要）。
- **commit 分割**（再掲・最終確認）: fail-fast / Phase 1 / 2 / 2.1 / 3 / 4 の
  6 変更が working tree に混在したまま。Phase 5 の長 run 検証前に分割 commit
  することを強く推奨 — 検証で問題が出た場合の bisect が現状では不可能。

## Phase 5 に残る作業（gate 対応状況）

| gate | 状態 |
|---|---|
| (a) 再現 seed の失敗が単一 factor site へ移動 | **本レビューで確認済み**（`stage=B_U_D`） |
| (b) beta=30–32 の two_sided 完走 | beta=30 短 run は完走確認済み。**統計的一致の検証（十分な nmeas での combine との比較）が残作業** |
| combine default の非退行 | bit 不変を確認済み。`make test_mpi` / `make test_slow` の実行は未確認（要実施） |

統計比較の設計メモ: 同一 seed でも trajectory は最終的に分岐するため、
「bin 平均 ± jackknife 誤差での一致（例: 2σ）」を判定基準にし、
nmeas は誤差が物理量の ~1% 程度になる長さを取ること。beta=32 では
単一 factor 壁（平均 ≈ 656 + 揺らぎ）にまだ余裕があるが、
`udv_scale_file` を併用して max left_max を記録しておくと、
壁との距離を run ごとに定量できる。
