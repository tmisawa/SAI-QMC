---
date: 2026-07-14
datetime: 2026-07-14 16:30 JST
model: Claude Fable 5 (claude-fable-5)
summary: |
  Phase 2.1（単一 factor scale 診断）の実装レビューと追加測定。実装は承認 —
  前レビューの明確化 C1-C3 はすべて反映され、テストも全行 24 tab / sentinel
  検査まで強化された。レビュアーの追加測定（beta=30, 8 sweeps）で、
  (1) 壁の内側 run の完走、(2) suffix stack build も平衡化後は prefix と同じ
  スケール（~600, per-unit-beta ≈20.4）に達すること、(3) 平衡 spread ≈ 1190 が
  beta=30 で既に 745 を超えることを確認。Phase 6 は有界 multi-factor 分割を
  第一候補とすることを推奨する。
---

# udv_scale_file Phase 2.1 実装レビューと追加測定

対象:

- `src/green.h` / `src/green.c`（`GreenScaleObserver`, stack build 記録）
- `src/dqmc.c`（`left_udv_forward` / `left_udv_backward`, 単一 factor emit,
  observer 設定/解除, `dqmc_stack_build_failed()`）
- `tests/test_dqmc.c`（TSV 構造検査の拡張）
- `docs/2026-07-14-udv-green-rebuild-roadmap.html`, `LOG.md` 更新

## 総合判定: 承認

前レビュー（Phase 2.1 計画レビュー）の明確化 3 点はすべて実装に反映されている:

| 指摘 | 反映 |
|---|---|
| C1: 単一 factor 行の right 側を NULL sentinel でなく明示 emit | `dqmc_record_single_udv_scale_diag()` が zeroed stats（counts 0/0/0, min/max/spread/cross = nan）で `dqmc_write_udv_scale_diag()` を直接呼ぶ。`udv_scale_stats(NULL)` は不使用 |
| C2: tau の意味 | `left_udv_forward` は `tau=Ltr, boundary=M`、`left_udv_backward` は `tau=0, boundary=0` |
| C3: label 前方一致衝突 | `left_udv_forward` / `left_udv_backward` に反転（`forward`/`backward` と startswith 衝突なし） |

## コードレビュー結果（すべて妥当）

- **observer 設計**: forward 宣言 `typedef struct Green Green` で callback 型を
  struct 前に定義する構成は clean。`green_alloc()`/`green_free()` で null
  初期化・解除、`dqmc_enable_udv_scale_diag()` の空 path 分岐でも明示解除
  （計画の「observer 残留」リスク対応）。observer 失敗は
  `G->work.failed` latch → 既存 UDV 失敗と同経路で `D.status=1` に届く。
- **記録サイト**: suffix/prefix build とも `udv_copy` 直後・rmul/lmul 直前
  （= dense 形成への入力）で記録し、失敗時は break。`left_udv_forward` は
  最終 lmul 後・`green_from_left_udv()` 直前（= full-beta product）、u/d
  （`!use_ph`）両対応。`left_udv_backward` は `len>0` guard の外にあり常に記録。
- **`dqmc_stack_build_failed()`**: stack build 中の UDV 失敗を従来より早く
  `status=1` に変換する。従来は境界 rebuild まで遅延して同じ結果に到達して
  いたので、失敗 run の最終状態は不変（挙動改善のみ）。
- **テスト**: 全行 24 tab assertion、フィールド分解による sentinel 検査、
  alternating 2 sweep + 明示 prefix build で全 label を発火。期待値を机上で
  独立検算した — suffix_pre 2（j=2,1）、prefix_pre 3（j=1..3）、forward pair 2
  （l+1=2,4）、backward pair 2（l=4,2）、left_udv 各 1、`tau=6/boundary=3` と
  `tau=0/boundary=0` — すべて実装と整合。

## レビュアー側で独立に再現した検証

- `make test` → `ALL TESTS PASSED`。
- 再現 seed（beta=33.325, nwarm=1）: `stack_suffix_pre_rmul` 166 行
  （M−1 = 166 ✓）+ `forward` pair 165 行を出力後、**従来と同一の失敗点**
  （boundary 165 の `udv_combine stage=C -inf`）で停止。診断追加が trajectory を
  乱していないことを確認。`left_udv_forward` は未到達 — 計画のリスク欄の
  記載どおりで問題なし。

## 追加測定（beta=30, 8 sweeps）と Phase 6 への含意

壁の内側 beta=30（Ltr=1200）、同 seed、nwarm=6, nmeas=2 を実行した。
**rc=0 で完走し observables は有限** — 改訂 roadmap Phase 5 の
「壁の内側完走」gate が達成可能であることを裏付ける。

sweep ごとの最大 `max_logD`（natural log）:

| sweep | prefix (pair 行) | suffix build (pre_rmul) | full product (left_udv) |
|---|---|---|---|
| 0 | 629.8 | 325.2 | 634.7 |
| 1 | 593.0 | 625.0 | 596.5 |
| 2–7 | 591–612 | 586–607 | 593–616 |

### 知見 1: suffix stack build も平衡化後は同じ壁に到達する

初期 field の suffix build は 325 に留まるが、**1 sweep の平衡化で 625 に急伸**
し、以後 prefix と同水準（~600）で推移する。Phase 2 review の推測
（「初期 field の成長率が低いだけで、平衡配置では prefix と同率になる」）が
実測で確定した。**Phase 6 の単一 factor 対策は `green_stack_build_suffix()` の
rmul 系も必ずカバーする必要がある。**

### 知見 2: 平衡成長率 ≈ 20.4 /unit-beta、目標 beta は揺らぎ支配の限界域

- 平衡時の full product max ≈ 600 ± 15–25（beta=30）→ per-unit-beta ≈ 20.0–20.5。
- beta=33.325 への外挿平均 ≈ 680。壁 709.78 までの余裕は ~30 で、観測された
  sweep 間揺らぎ（±25）および初期 field の inflate（sweep 0 は平衡比 +25 —
  再現 seed が第 1 sweep で 708 に達した理由）と同程度。
- したがって beta=33.325 の長 run（10^4 sweeps）では壁越えがほぼ確実に発生する。
  Phase 2 review の「linear-D の限界 beta ≈ 33」を「**平均では ≈ 34.6 だが、
  揺らぎを含む実効的な長 run 限界は beta ≈ 31–32**」に精緻化する。
  roadmap の Phase 5 gate（beta=30–32 完走）は妥当な位置にある。

### 知見 3（Phase 6 設計選択に直結): 平衡 spread は beta=30 で既に 1190

壁の内側でも単一 factor の `spread_logD` は平衡時 ~1086–1191 に達し、
dense 表現可能幅 745 を大きく超えている。つまり **log-D 保持方式は、
overflow しない beta でも dense QR 入力を作る時点で下位モードを表現できない**
（offset 方式の underflow / rank 問題が beta=30 で既に発生する）。
一方、有界 multi-factor 分割（各 factor の被覆を beta ≈ 15–17 以下に制限すれば
spread ≈ 700 < 745）は dense 表現の範囲内に収まる。

**推奨: Phase 6 は「有界 multi-factor prefix/suffix 保持 + two-sided（多因子）
再構成」を第一候補、log-D は truncation policy を明示できる場合の代替とする。**
（two-sided solver（Phase 3）はこの多因子化の基本部品になるため、投資は無駄に
ならない。）

## 軽微な指摘

- 診断行数は再現条件で約 2 倍（pair 165 + suffix_pre 166 /sweep）になった。
  短い診断 run 用の位置づけは維持されており現時点で対応不要（`udv_scale_every`
  間引きの優先度は据え置きで可）。
- テストの `green_stack_build_prefix()` 明示呼び出しは、backward sweep が
  carried prefix を再利用するため pre_lmul 行が自然には出ないことへの対処で、
  やや人工的だが site の発火確認としては妥当。将来 carried_prefix_valid=0 の
  経路をテストする場合は sweep 順序で自然に発火させられる。
- commit 分割（fail-fast / Phase 1 / Phase 2 / Phase 2.1 が working tree に
  混在）— 既出指摘の再掲。diff が大きくなり続けているため、Phase 3 着手前の
  分割 commit を強く推奨。

## Phase 3 への申し送り（更新）

- two-sided solver のテストケース「片側大 scale」は、今回の実測に合わせて
  `D_l max ≈ e^600, D_r max ≈ e^{O(1)}`（後期 boundary 型）と
  `D_l ≈ e^300, D_r ≈ e^300`（中間 boundary 型）の双方を含めること。
- 将来の多因子化（Phase 6 第一候補）を見据え、`udv_inv_one_plus_two_sided_work()`
  の内部構造（`H` の組み立て）を「因子を 1 つずつ吸収する」形に整理しておくと
  Phase 6 での拡張が容易になる。
