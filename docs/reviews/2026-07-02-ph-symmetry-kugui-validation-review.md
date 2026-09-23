---
date: 2026-07-02
datetime: 2026-07-02 21:42 JST
model: Claude Fable 5
status: review
topic: PH 対称性 kugui i2cpu 検証（PBS 844018）の独立照合レビュー
summary: |
  PH 対称性 kugui 検証（211d4d6 vs b77afcb、PBS 844018、README 21:40 JST）を
  生データ（profile CSV / time / diff / make logs / base_commits）から独立に
  再計算・照合。全数値が README と一致し、検証結果・解釈とも問題なし。
  決め手の一つは green_update 呼び出し数の厳密半減（121814→60907）で、
  kugui でも PH 経路が期待どおり片スピン化している強い整合性証拠。
  ただし軌道一致そのものの直接確認は固定場 sweep/G 行列比較テストにある。
  MPI wall 2.20× の内訳を精密化（profiled beta block は 2.00×、外部 real
  との差分は未計測領域の短縮 2.95s→0.99s — メモリ半減の示唆）。
  kugui 16×16 β=16 の sweep は 63.81s→1.50s で最適化 3 段の累計 42.5×。
---

# PH 対称性 kugui 検証の独立照合レビュー

## 対象

- 検証データ: `data/profiling_runs/kugui_i2cpu_ph_symmetry_211d4d6_20260702/`
  （README.md 2026-07-02 21:40 JST, GPT-5 Codex 作成）
- 結果 HTML: `docs/2026-07-02-kugui-i2cpu-ph-symmetry-validation.html`
- 比較コミット: old `b77afcb` vs new `211d4d6 feat: exploit PH symmetry at
  half filling`（base_commits.txt で確認）
- 実行環境: PBS 844018.kugui-pbs, i2cpu cpu121, icc + MKL sequential,
  `FI_PROVIDER=psm3`（AGENTS.md の kugui 確認済み構成に準拠）
- レビュー方法: summary TSV を経由せず、生の profile CSV / time ファイル /
  diff / make ログから数値を独立に再計算して README の主張と照合。

## 結論

**検証結果・解釈とも問題なし。** 全数値が生データから再現でき、
出力差分は事前予測（実装レビュー
`docs/reviews/2026-07-02-ph-symmetry-implementation-review.md` §4・§6-1）の
範囲内。README の「次の本筋は片スピン側の安定化系」という解釈も
優先順位レビューと整合し妥当。

## 1. 性能数値の再計算（すべて README と一致）

profile CSV の `measurement/dqmc_sweep` と time ファイルから独立に算出:

| mode | β | old sweep | new sweep | sweep 比 | wall 比 |
|---|---:|---:|---:|---:|---:|
| serial | 4 | 0.719 s | 0.362 s | 1.989× | 2.089× |
| serial | 8 | 1.475 s | 0.740 s | 1.993× | 1.954× |
| serial | 16 | 2.964 s | 1.501 s | 1.975× | 1.951× |
| MPI np16 | 16 | 3.008 s | 1.506 s | 1.997× | 2.199× |

Mac 検証（1.90×/1.96×）と整合し、kugui の方がより理想 2× に近い。

## 2. 正しさの決め手: 呼び出し数の厳密半減

serial β=16 の region 呼び出し数（old → new）:

| region | calls |
|---|---|
| green_update | 121814 → **60907（厳密に半分）** |
| green_from_stack | 160 → 80 |
| green_wrap | 640 → 320 |
| green_stack_build | 4 → 2 |
| green_from_scratch | 2 → 1 |

`green_update` の厳密半減は、同一 seed・同一入力で PH 経路が期待どおり
片スピン分だけを計上している強い整合性証拠である。ただし、これ単独を
「完全に同一のマルコフ軌道」の直接証拠とはしない。軌道一致そのものは
Mac での固定場 sweep ストレス（実装レビュー §3）と `Gu/Gd` 行列比較で
直接確認済みであり、今回の kugui profile はその不変条件と矛盾しない
実機スケールの補強証拠と位置づける。

## 3. 出力差分の照合

diff_*.txt を直接確認:

- **sign=1 を全ケース維持**（serial β=4/8/16、MPI np16 β=16）。
- **dN（ntot の jackknife 誤差）が ~1e-8〜5.1e-7 → 厳密 0**。
  PH では配置ごとに n↑+n↓ = 1 が厳密成立するため（予測どおりの改善）。
- E/D の差は印字最終桁のみ: β=4 は E/D とも完全一致、β=8/16 は E が
  1e-5〜2e-5（jackknife 誤差 ±7.8〜7.9 に対し無視できる）、D が 1e-8。
- old/new とも `make test` / `make test_mpi` PASS（ログ末尾で確認）。

## 4. MPI wall 2.20× の内訳（精密化）

- profiler の `beta_total`: 7.67 s → 3.84 s = **2.00×**。
  これは純計算部だけではなく、root 側で計測された beta block 全体
  （setup/gather/jackknife を含む）の wall interval である。
- 外部 `real` との差分 = profiler 外の未計測領域: 2.95 s → 0.99 s。
  MPI init/finalize、入力処理、出力 close などが混在し、launch/setup
  だけには分解できない。
  stack_d / Gd 系の確保が消えたメモリフットプリント半減の効果と推定されるが、
  MPI 起動の揺らぎと切り分けていないため**断定せず「示唆」に留める**
  （delayed-update レビューの Codex 追記 2026-07-02 19:24 の規律を踏襲）。
- README の表現（"consistent with reduced per-rank memory traffic, but
  production rank/thread layout should still be measured separately"）は適切。

## 5. 解釈の妥当性

README「次の性能ターゲットはスピン重複ではなく、残った片スピンの安定化系
（green_stack_build / green_from_stack / udv_inv_one_plus / BLAS）」— 妥当。
`docs/reviews/2026-07-02-delayed-update-next-steps-review.md` §2 の優先順位
（交互スイープで stack_build 消去、stab 再検討 + クロスチェック、
inv_one_plus 内部削減、rank/thread 配分スキャン）がそのまま次の作業リスト。

## 6. ハイライト: 最適化 3 段の累計（kugui i2cpu, 16×16, U=4, stab=4, β=16）

| 段階 | commit | sweep 平均 | 累計 |
|---|---|---:|---:|
| ベースライン（安定化全再計算） | 25ebdd2 | 63.81 s | 1× |
| UDV 部分積スタック | ff9b741 | 3.43 s | 18.6× |
| delayed update | 2c7cb11 | 3.00 s | 21.3× |
| PH 対称性 | 211d4d6 | 1.50 s | **42.5×** |

3300 sweep/replica ≈ 82 分（kugui 1コア）なので、nrep=120 の MPI で
**16×16 低温計算は実用域に到達した見込み**。ただし本検証は np16 smoke
であり、production の nrep=120 では rank/thread 配分スキャンを別途行う。
当初の性能分析（`docs/2026-07-02-performance-analysis.md`）の目標を
達成した水準という判断は妥当。

## 7. 特記事項

- 本レビューは既存データの照合であり、コード変更・再実行は行っていない。
- 再現手順: 本文 §1–§3 の数値は
  `data/profiling_runs/kugui_i2cpu_ph_symmetry_211d4d6_20260702/` の
  profile_*.csv（`awk` で measurement/dqmc_sweep 行を集計）、time_*.txt、
  diff_*_old_vs_new.txt、*_make_test*.log から直接得られる。
- Codex 追記（2026-07-02）: 本文の結論は維持するが、証拠の強さを
  区別する。`green_update` 半減は実機 profile 上の強い整合性証拠であり、
  軌道一致の直接確認は固定場 sweep/G 行列比較テストに置く。また
  `beta_total` は純計算部ではなく profiled beta block 全体、外部 `real`
  との差分は MPI init/finalize や I/O を含む未計測領域として扱う。
