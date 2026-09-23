---
date: 2026-07-02
datetime: 2026-07-02 19:19 JST
model: Claude Fable 5
status: review
topic: delayed update kugui 検証の解釈レビュー — 次の性能作業の優先順位
summary: |
  kugui i2cpu delayed-update 検証（2c7cb11 vs a31c0f3, PBS 843909）の解釈を
  profile CSV で定量レビュー。「delayed update は正しく効いている」は妥当
  （green_update 2.2×、出力 diff 空、MPI wall 1.30× は帯域競合緩和の証拠）。
  一方「次は delayed capacity tuning + flush layout が本筋」は不支持:
  green_update は sweep の 12% に低下済みで、当該施策の天井は ~1.03–1.05×。
  実測が指す本筋は (1) PH 対称性 2×、(2) 安定化系 63% の削減
  （交互スイープで stack_build 19% 消去、stab 再検討）、(3) rank/thread
  配分スキャン。capacity tuning は並行の微改善に格下げ。
---

# delayed update 検証の解釈レビュー — 次の性能作業はどこか

## 対象

- 検証データ: `data/profiling_runs/kugui_i2cpu_delayed_update_2c7cb11_20260702/`
  （README.md, profile_*.csv, diff_*.txt。PBS 843909, i2cpu cpu121, exit 0）
- 比較コミット: old `a31c0f3` vs new `2c7cb11`（delayed Green updates）
- レビューした主張（README「Interpretation」および報告者の要約）:
  1. delayed update は正しく効いている
  2. sweep 全体では green_stack_build / green_from_stack が残るため、
     次の性能作業は delayed capacity tuning と flush layout/cache 改善が本筋

## 結論

- 主張 1: **妥当**（下記の3つの根拠で支持）。
- 主張 2: **不支持**。capacity tuning / flush layout が削れるのは sweep の
  12%（green_update の残り）の内側であり、天井 1.14×・現実 ~1.03–1.05×。
  「本筋」は PH 対称性（2×）と安定化系（63%）の削減である。
  capacity tuning は安価な並行タスクとしては妥当（優先度の問題）。

## 主張 1 の根拠（delayed update は成功）

1. `green_update` total が serial/MPI とも **2.2×** 短縮
   （serial β=16: 1.615→0.728 s、MPI np16: 26.0→11.7 s）。
2. 出力 diff が全ケース（serial β=4/8/16、MPI np16 β=16）で空、
   old/new とも `make test` / `make test_mpi` PASS。
3. **MPI np16 の wall 1.30× > serial sweep 1.15×**。これは delayed update が
   rank 密集時のメモリ帯域競合を緩和した証拠で、production（nrep=120、
   ノードに rank を詰める運用）での実効価値は serial 計測値より大きい。
   delayed update 導入の正当性はむしろこの点で強い。

## 主張 2 の反証（数値）

新コード serial β=16（`profile_new_serial_b16.csv`、2 sweep = 5.996 s）を
非重複に分解:

| 部位 | 時間/2sweep | 割合 |
|---|---:|---:|
| 安定化系合計 = stack_build 1.116 + from_stack 1.958 + 左延長 udv_lmul 0.719 | 3.79 s | **63%** |
| green_wrap | 1.04 s | 17% |
| green_update（delayed 済み） | 0.73 s | **12%** |
| 残り（ブロック稠密積 gemm 等） | ~0.43 s | 7% |

- capacity tuning（16→32/64）と flush layout が触れるのは 12% の内側。
  **完全消去でも 1.14×、現実は k=16 の細長 GEMM が k=64 で効率化する分の
  数%（~1.03–1.05×）が天井**。
- なお la_gemm total の増加（3.635→4.060 s）は flush の rank-k GEMM が
  update 時間の一部を la_gemm region に移した結果で想定どおり（README の
  記載と整合）。

## 実測が指す優先順位

1. **半充填 PH 対称性で down スピン省略 — 2.0×（最大の単発レバー）**
   $G_\downarrow = 1 - P G_\uparrow^{\mathsf T} P$（P: 副格子パリティ）。
   上表の全行が半分になる。既にバックログ済み。検証ラダー
   （ED 照合・two-spin 実装との一致）通過を採用条件とする。
2. **安定化系 63% の削減**
   - 交互（上り/下り）スイープ化: sweep 冒頭の stack_build（19%）を
     丸ごと消去。単独で ~1.23×。MC 軌道が変わるため統計一致で検証。
   - stab 4→8 の再検討: from_stack / 左 lmul / inv_one_plus の回数が半減し
     ~15–20%。ただし stab=8 汚染の前歴（L4-U12）があるため、既存の
     stab クロスチェック手順で U ごとに検証してから。U=4 の 16×16 は
     通る見込みが高い。
   - `udv_inv_one_plus`（1.12 s, 19%）の内部削減: la_inverse×2 +
     la_logdet×2 のうち、det U の符号を QR の Householder 符号から
     相乗り取得する等の余地。
3. **MPI × threaded MKL の rank/thread 配分スキャン**（README 3項目めは妥当）
   残り時間の ~85% が n=256 の BLAS。production は throughput 指向なので
   「128 rank×1 thread vs 64×2 vs 32×4 で replicas/hour を比較」する
   帯域競合の観点で設計するのが正しい（今回の np16 wall 1.30× がその兆候）。
4. delayed capacity tuning / flush layout: 上記と並行の微改善として実施可
   （単体の期待値 ~1.03–1.05×）。

## 補足

- 本レビューの分解では green_stack_build に udv_rmul（0.697 s）、
  green_from_stack に udv_combine（0.834 s）+ udv_inv_one_plus（1.124 s）が
  ネストして含まれる（profiler region は入れ子計上）。
- 16×16 の実運用見通し: PH 対称性 + 交互スイープが入れば β=16 serial で
  ~1.2 s/sweep 程度（現行 3.0 s）まで見込める。

## 追記: Codex確認による修正点（2026-07-02 19:24 JST）

上記の主結論（delayed update は成功、capacity tuning は主戦場ではない）は
妥当。ただし、次の3点は本文の表現を弱めるか、次計画で前提条件として明記する。

1. **MPI wall 1.30×は「証拠」ではなく「示唆」に留める。**

   `time -p real` は MPI np16 β=16 で 11.22 s → 8.62 s（1.30×）だが、
   profiler の `beta_total` は 8.55 s → 7.63 s（1.12×）、
   aggregate `dqmc_sweep` は 3.453 s → 3.006 s（1.15×）。
   したがって 1.30×には MPI launch / synchronization / profiler 外の揺れも含む。
   「rank 密集時の帯域競合緩和の証拠」と断定せず、
   「帯域競合緩和を示唆する。production rank 密度で再測定する価値がある」
   とするのが安全。

2. **交互スイープで stack_build を丸ごと消せる、という記述は未証明。**

   現実装の stack は sweep 冒頭の auxiliary field から構築される。
   sweep 中に field flip が入るため、前 sweep の stack をそのまま次 sweep の
   逆向き安定化に再利用できるとは限らない。逆向き用 boundary stack を
   sweep 中に保存・更新する設計、または reverse-direction 専用の整合性証明が必要。
   `stack_build` 19% 消去・~1.23× は「上限見積もり / 要設計検証」として扱う。

3. **PH 対称性の 2×は上限表現にする。**

   半充填 PH 対称性で spin-dependent Green work はほぼ半減できる見込みだが、
   setup、measurement、I/O、jackknife、PH 適用条件の guard は残る。
   「上表の全行が半分」と断定せず、
   「主要な spin-dependent kernel は最大 2×に近く短縮」と書く方が正確。
