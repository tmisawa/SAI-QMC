---
date: 2026-07-14
datetime: 2026-07-14 14:29 JST
model: Claude Fable 5 (claude-fable-5)
summary: |
  Phase 2（udv_scale_file 診断）の実装レビューと、診断データの独立再現・分析。
  実装は承認（テスト・guard・TSV 出力をレビュアー側で再現、LOG の数値と bit 一致）。
  重要な新事実: 診断データは前レビュー M1（global offset 不成立）を実測で確認し、
  さらに単一 factor D の壁（ln DBL_MAX ≈ 709.8）が beta=33.325 で既に有効である
  ことを示した。two_sided 単独では目標 run は完走できない公算が大きく、
  roadmap Phase 5 の exit criteria 改訂と Phase 6 の必須化を推奨する。
---

# udv_scale_file 診断 (Phase 2) 実装レビューと診断データ分析

対象:

- `src/io.h`, `src/io.c`（`udv_scale_file` / alias `udv_scale_diagnostics_file`）
- `src/dqmc.h`, `src/dqmc.c`（`DqmcUdvScaleDiag`, `dqmc_enable_udv_scale_diag()`,
  `dqmc_record_udv_scale_diag()`）
- `src/main.c`（TSV header 生成、serial-only guard）
- `src/replica_run.c`（enable 呼び出し）
- `tests/test_io.c`, `tests/test_dqmc.c`
- `LOG.md` 2026-07-14 14:08 JST エントリ
- `docs/2026-07-14-udv-green-rebuild-roadmap.html`（Phase 2 更新）

## 総合判定: 実装は承認。診断結果は roadmap の改訂を要求する

実装自体は正しく、opt-in 設計・production stdout 無変更・fail-fast 整合の
すべてを満たす。加えて、レビューの一環として再現 seed の診断 run を独立に実行し
データを分析した結果、**単一 factor D の overflow 壁が目標条件で既に有効**という
roadmap の前提を変える事実が確認された（後述）。

## レビュアー側で独立に再現した検証

- `make test` → `ALL TESTS PASSED`（`test_dqmc` の新規診断テスト含む）。
- `dqmc_omp` ビルドで `parallel=omp` + `udv_scale_file` →
  `ERROR: udv_scale_file currently supports parallel=serial only`, rc=1。
  （serial バイナリでは `parallel=omp` 自体が先に拒否されるため guard には
  OpenMP ビルドで到達することを確認。）
- 再現 seed `14012418791647386686`, L=4(4x4), U=16, dtau=0.025, beta=33.325,
  `nwarm=1` の診断 run を実行: TSV 165 データ行（boundary 1–165）を出力後、
  既存どおり `udv_combine stage=C row=0 col=0 value=-inf` で停止。
  最終行 `tau=1320, boundary=165, left_max_logD=708.14631542658333,
  cross_max_logD=710.68162208088245` — **LOG.md 記載値と bit 一致**。
- production stdout: 通常 header / observable 形式のまま（変更なし）を確認。

## コードレビュー結果（すべて妥当）

- **parser**: `udv_scale_file` / alias とも `stab_drift_file` の既存 2 キー
  パターンと同型。default 空文字。テストは本キー・alias・default の 3 点をカバー。
- **serial-only guard**: `stab_drift_file` の前例と同一の配置・文言・
  クリーンアップ（`lattice_free` + `mpi_finalize`）。
- **記録サイト**: forward は `green_from_stack()` 直前（u/d 両スピン、
  `!use_ph` 分岐正しい）、backward（alternating）は
  `green_from_boundary_factors()` 直前。left/right の対応は combine の
  prefix/suffix（l/r）と一致していることを call site で確認。
- **TSV 整合**: header 25 列とデータ行 25 フィールドの名前・順序・型
  （`%d/%llu/%.17g/%s`）が一致。NaN は `nan` として出力され TSV 解析互換。
- **統計計算** (`udv_scale_stats`): min/max 初期化（`finite_count==0` 時に両方
  設定）、zero/nonfinite の分離カウント、finite 0 個時の NaN 化、いずれも正しい。
- **失敗経路**: 記録失敗 → `D->status=1` + `dqmc_invalidate_carried()` は
  `stab_drift` の前例と同じ fail-fast 協調。起動時に `init_udv_scale_file()` が
  "w" で開くため、パス不正は run 開始前に検出される。
- **テスト設計**: `test_dqmc` の期待値 2 行は L=6, stab=2 で境界が
  l+1=2,4（l+1<L のため 6 は除外）となることと一致。end-of-sweep が記録されない
  現仕様を暗黙に文書化してもいる。

## 診断データ分析（レビュアーによる追加分析）

再現 run の TSV から（tau, left min/max/spread, right min/max/spread, cross）:

| tau | boundary | left_max | left_spread | right_max | right_spread | cross_max |
|---|---|---|---|---|---|---|
| 8 | 1 | 5.3 | 7.8 | 362.2 | 714.7 | 367.5 |
| 640 | 80 | 348.4 | 668.8 | 186.9 | 375.2 | 535.3 |
| 1280 | 160 | 685.4 | 1313.1 | 13.6 | 25.3 | 699.0 |
| 1320 | 165 | **708.15** | **1355.8** | 2.5 | 6.3 | **710.68** |

参照値: `ln(DBL_MAX) = 709.78`、denormal 下限 `ln ≈ -744.4`。

### 事実 1: cross_max は overflow の正確な予測子として機能した

最終行 `cross_max=710.68 > 709.78` の直後に当該 combine が `stage=C -inf` で
停止。診断は設計どおり「入る直前」の状態を捉えており、`log|TLUR|`（O(1)）を
除けば overflow 条件と定量的に整合する。**Phase 2 の目的は達成されている。**

### 事実 2: 前レビュー M1（global offset 不成立）が実測で確定した

`left_spread = 1355.8` は underflow 幅 745 を 610 decade 超過する。global offset
版 log-combine では left factor の下位モードの行が丸ごと exactly 0 になり、
rank 欠損 fail-fast に置き換わるだけ — 前レビューの M1 が推定でなく実測で
確定した。**log-scale combine を Phase 6 に先送りした判断は正しかった**
（診断先行アプローチの成果）。

### 事実 3（新規・重要）: 単一 factor D の壁が目標 beta で既に有効

- `left_max` の成長はほぼ線形: 0.54–0.57 / slice（≈ 21.5 / 単位 beta）。
- tau=1320 で 708.15。外挿すると次の境界 tau=1328 で ≈ 712.7、full beta
  (tau=1333) で ≈ 715.5 — **いずれも 709.78 を超える**。
- つまり two_sided で boundary 165 の combine を直しても、同一 sweep 内の
  次の `udv_lmul_work()`（tau=1328 への prefix 延長、`stage=B_U_D` で
  `M[:,j] *= D[j]`）が overflow する公算が大きい。トラジェクトリは combine
  成功後に変わるため確定ではないが、成長率から見て beta=33.325 は
  linear-D 単一 factor の限界 beta ≈ 709.78/21.5 ≈ **33.0 のわずかに外側**にある。
- suffix 側も同じ壁を持つ: 平衡化後は sweep 開始時の
  `green_stack_build_suffix()`（rmul, `S[1]` は ~full beta を被覆）が
  同じ成長率に達し、`stage=D_T_B` で overflow し得る。
  （今回の run では初期 field の成長率が低く suffix_max=362 に留まったが、
  これは field 未平衡のためで、平衡配置では prefix と同率になると考えるべき。）

### 事実 4（副次）: fail-fast 導入前の「10397 sweeps 生存」は無害でなかった

今回の測定で第 1 sweep から combine overflow が起きることが確定した。
fail-fast 前の実装では inf → QR → NaN の Green が「次の境界 rebuild で
自己回復」しつつ、NaN 区間では全 flip が棄却される（`rng < fabs(NaN)` は偽）
ため、**silent なサンプリング歪みを伴って走り続けていた**ことになる。
L=4, U=16 の T < 0.04（clean window 外）の既存データは結果を採用しない、
という既定方針の正しさを裏付ける。

## roadmap / 計画への提案（重要度順）

1. **Phase 5 exit criteria の改訂**: 現行の「beta=33.325 再現 seed が two_sided
   で完走」は上記の外挿から達成不能の公算が大きい。推奨ゲート:
   (a) 失敗箇所が `udv_combine stage=C` から単一 factor サイト
   （`udv_lmul_work stage=B_U_D` / `udv_rmul stage=D_T_B`）へ移ること、
   (b) 壁の内側（例 beta=30–32）で two_sided が完走し combine 経路と統計一致、
   の 2 点に分けるべき。
2. **Phase 6 の位置づけを conditional から必須へ**: roadmap の gate
   「single-factor D が double を超える証拠が出たら」は今回の測定で
   ほぼ満たされた（708.15/709.78、外挿 715）。beta=33.325 を目標とする限り、
   単一 factor 対策（log-D 保持、または prefix/suffix を複数 factor に分割保持し
   two-sided を多因子化する方式）が必要になる。方式選定には次項の追加計測が
   有効。
3. **記録サイトの追加（Phase 2.1 として小さく）**: 現状は boundary rebuild のみ
   記録している。次に失敗が現れる場所である
   (a) end-of-sweep `green_from_left_udv()` 直前の単一 factor（full-beta prefix、
   right 列は NaN で埋める）、
   (b) `green_stack_build_suffix()` / `_prefix()` の各 rmul/lmul 直前、
   も記録すると、単一 factor の壁と stack build の成長率が直接測れる。

## 軽微な指摘

- **長 run でのファイルサイズ**: 記録は boundary 毎 × sweep 毎（今回 165 行/sweep）。
  フル再現条件（nwarm=2000, nmeas=9000）では約 180 万行 ≈ 数百 MB、
  fopen/fclose も同回数になる。Phase 2 の想定用途（短い診断 run）では問題ないが、
  長 run に使う場合に備えて sweep 間引き（例: `udv_scale_every=N`）を
  将来オプションとして検討。現時点で対応不要。
- **`sweep_count` 列の意味**: sweep 完了時にインクリメントされるため、
  行の値は「完了済み sweep 数」（第 1 sweep 中は 0）。解析側の誤読を防ぐため
  roadmap か README に一行注記を推奨。
- **commit 分割**（前回レビュー指摘の再掲）: working tree には fail-fast 実装・
  Phase 1・Phase 2 の 3 つの論理変更が混在したまま。コミット時に分割すること。

## Phase 3 への申し送り

- `udv_inv_one_plus_two_sided_work()` の unit test は `g` / det sign 単位で
  dense reference と比較（QR 符号不定性のため factor 単位比較は不可）。
- moderate case では現行 `udv_combine + udv_inv_one_plus_work` との一致テストを
  併設して回帰検出を強化。
- 今回の測定データ（`left_max=708`, `right_max=2.5` のような極端な非対称も
  boundary によって現れる）を踏まえ、テストケースに「片側だけ大 scale」
  （例: `D_l ~ e^{±350}`, `D_r ~ O(1)`）と「両側大 scale」の両方を含めること。
- two_sided の `H` は `T_r^{-1}` を要するため、`dtrtri` 失敗時の fail-fast を
  現行 `udv_inv_one_plus_work()` と同型で入れること。
