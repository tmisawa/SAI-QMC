---
date: 2026-07-02
datetime: 2026-07-02 16:26 JST
model: Claude Fable 5
status: review
topic: DQMC UDV スタック実装の検証レビュー
summary: |
  UDV 部分積スタック実装（plan: 2026-07-02-dqmc-udv-stack-performance.md）の
  徹底検証。コード精読・全テスト（serial/slow/omp）・leaks・低温ストレス
  （β=20, U=12, D レンジ 1e-45..1e+58）・旧バイナリとの物理回帰・性能実測を
  全て実施し、**ブロッカーなし**。16×16 で β=4: 4.7×, β=8: 7.5× を実測、
  スケーリングの β²→β線形化を確認。軽微指摘 4件（S[0] 未使用、低温ストレスの
  CI 未収載、aliasing 制約未文書化、sweep 統合の自動テスト未追加）。
---

# DQMC UDV スタック実装 検証レビュー

## 対象

- 実装計画: `docs/superpowers/plans/2026-07-02-dqmc-udv-stack-performance.md`
  （2026-07-02 15:35 JST レビュー反映版）
- 検証したコード（working tree, 対照は git HEAD = 25ebdd2）:
  - `src/linalg.{h,c}` — `LinalgWork`, `udv_rmul`, `udv_combine`,
    `udv_lmul_work`, `udv_inv_one_plus_work`, `la_inverse_work`, `la_logdet_work`
  - `src/green.{h,c}` — `GreenStack`, `green_build_Bblock`, `green_stack_build`,
    `green_from_stack`, `green_from_left_udv`, expv 2値キャッシュ, u/v 事前確保
  - `src/dqmc.{h,c}` — `dqmc_sweep` のスタック統合
  - `src/field.{h,c}` — `field_N` の N_cache
  - `src/profiler.{h,c}` — region 4種追加
  - 新規テスト: `tests/test_udv_stack.c`, `tests/test_green_stack.c`
  - ベンチ入力: `input/bench_2d_L8.txt`, `input/bench_2d_L16.txt`

## 結論

**ブロッカーなし。コミット可能。**
アルゴリズム・境界の意味論・sign/fail-fast の不変条件・数値安定性・物理結果・
性能のいずれも計画どおり、または計画より良い結果を確認した。
軽微指摘 4件は任意のフォローアップ。

## 1. コード精読による正しさの確認

### 境界の意味論（plan レビュー High #1 の反映確認）

- `green_stack_alloc`: `b[j] = min(j·stab, L)`, `M = ceil(L/stab)` — 正しい。
- periodic 安定化は `((l+1) % stab == 0) && (l+1) < L` で発火（`src/dqmc.c`）。
  旧コードは `l+1 == L` でも periodic + 末尾の 2 回再構成していたが、新コードは
  periodic をスキップして末尾 1 回に集約 — 意味論同値で再構成 1 回分の節約。
- sweep 末尾は `len = L - tau_left` の残り suffix（tail block / all block /
  最後の full block を包含）を常に左 UDV へ延長してから `green_from_left_udv`
  — 旧「末尾無条件 `green_from_scratch(0)`」と同一の保証。
  `L % stab != 0` / `L < stab` / `L == stab` / `stab = 1` の全ケースで成立
  （テストおよび実走で確認、§3）。

### 積の順序と等価性

- 左 UDV は更新済み slice `0..τ-1` の積 B(τ,0)、スタック `S[j]` は未更新
  slice `τ..L-1` の積 B(β,τ)。`udv_combine(left, S[j])` = B(τ,0)·B(β,τ) は
  `green_from_scratch(τ)` の循環積 B_{τ-1}···B_0·B_{L-1}···B_τ と同値。
- `cur_l` の設定（periodic: `b[j] % L` = τ、末尾: 0）は旧経路と一致。

### sign 安定化（2026-07-01 plan）の不変条件

- det_sign は結合後の単一 UDV に対する既存 `udv_inv_one_plus`（無変更の
  符号ロジック）から取得。失敗時 `det_sign = 0` + 非0 return + `D->status = 1`
  の fail-fast 連鎖、periodic・末尾双方での sign リセット位置 — すべて維持。

### メモリ・バッファ設計（plan レビュー High #2 の反映確認）

- `UDV` は factor-only（U/D/T のみ）を維持。作業領域は `LinalgWork` に分離され
  `Green` が 1 個保持（スタック段数に非依存）。
- `udv_inv_one_plus_work` 経路（`la_inverse_work`/`la_logdet_work` 含む）に
  hot allocation なし。`udv_combine` と `udv_inv_one_plus_work` の
  ワークバッファ割当（w->A..F, M, R, Ttmp, v1/v2, tau, work, ipiv）に衝突なし。
- `green_build_Bblock` の out/Btmp/tmp に `Green` の B/Binv/tmp を使うのは安全
  （`green_wrap` が毎回 Binv/tmp を作り直すため stale 使用なし）。

### exp 2値キャッシュ

- `Green.expv/expvinv`（`green_build_B`/`green_wrap`）と `Field.N_cache`
  （`field_N`）は元の式と一致。σ は `green_alloc` で ±1.0 の exact 値が入るため
  `field_N` のキャッシュ分岐は常にヒットし、一般 σ へのフォールバックも残る。

## 2. テスト実行結果

| 項目 | 結果 |
|---|---|
| `make clean && make dqmc` | 警告なしでビルド成功 |
| `make test`（新規 test_udv_stack / test_green_stack 含む） | ALL TESTS PASSED |
| `make test_slow`（sign 回帰） | ALL SLOW TESTS PASSED |
| `make test_omp` | ALL OMP TESTS PASSED |
| `leaks --atExit`（Ltr=5/13, stab=3 の端数条件で実走） | 0 leaks |

## 3. 低温ストレス検証（既存テストのカバレッジ外を追加実施）

既存 `test_green_stack` は Ltr ≤ 10（β ≤ 1）で、production（β=8〜20）の
D ダイナミックレンジをカバーしない。`udv_rmul` は行スケール行列の QR であり
理論上は低温での精度劣化が懸念されるため、使い捨て比較プログラムで
stacked 再構成（stack_build + from_stack / from_left_udv）と `green_from_scratch`
を全安定化点で突き合わせた:

| 条件 | D レンジ | max\|ΔG\| | det_sign 不一致 |
|---|---|---|---|
| 1D L4, β=8–16, U=4, stab=4 | 1e-25..1e+21 | ≤ 5.2e-14 | 0 |
| 1D L4, β=20, U=8/12, stab=2/4/8 | 1e-57..1e+58 | ≤ 3.7e-12 | 0 |
| 1D L8, β=16–20, U=8/12, stab=4 | 1e-47..1e+45 | ≤ 1.6e-11 | 0 |
| 1D L4/L8, β=20, dtau=0.025 (Ltr=800) | 1e-28..1e+31 | ≤ 8.1e-13 | 0 |

**結論: 行スケール QR への理論的懸念は実用条件で顕在化しない。**
production の全パラメータ域（2d4/2d6 系列の β=20, dtau=0.025 相当を含む）で
from_scratch と一致する。

## 4. 物理回帰（旧バイナリとの比較）

git HEAD (25ebdd2) のバイナリを別ディレクトリにビルドし、同一入力・同一 seed で比較:

- `input/1d_L4_U4.txt`（全6β）: 全出力列が印字精度（%.8g）で完全一致。
- `bench_2d_L8`（8×8, β=4）: E/doublon/sign 完全一致。
- `bench_2d_L16`（16×16, β=4, β=8）: β=4 完全一致、β=8 は E の第8有効数字のみ
  相違（−207.31493 vs −207.31492、誤差 ±16.6 に対し無視できる丸め差）。
- 端数境界の実走（1D, β=0.3/0.5, stab=8 → L=3,5 < stab、および stab=1）:
  旧・新・stab=1 の3通りが完全一致。

## 5. 性能実測（Mac/Apple Silicon, Accelerate, serial）

16×16, U=4, dtau=0.1, stab=4:

| | 旧 (HEAD) | 新 (stack) | speedup |
|---|---|---|---|
| β=4 (L=40), 1 sweep | ~1.9 s | ~0.41 s | **4.7×** |
| β=8 (L=80), 1 sweep | ~5.8 s | ~0.80 s | **7.5×** |
| β=4→8 の時間比 | 3.4×（≒β²則） | **2.0×（β線形）** | — |

**plan の Goal「β²則の β 線形化」を実測で達成。** speedup は L に比例して
拡大するため、β=16–20 の production では 15× 級が見込まれる。

新プロファイル内訳（16×16, β=4, measurement）: green_update 52% /
安定化系（stack_build + udv_lmul + from_stack）~39% / wrap 6%。
次の律速は plan の予測どおり green_update — 後続の delayed update 計画が有効。

## 6. 軽微指摘（ブロッカーではない、任意フォローアップ）

1. **S[0] が未消費**: `green_stack_build` は S[0] まで構築するが、periodic
   安定化は j ≥ 1 しか使わない。j=0 の構築をスキップすれば sweep あたり
   ブロック積 + udv_rmul 各1回 × 2スピンの節約（効果 ~1/M、任意）。
2. **低温ストレスの CI 未収載**: §3 の検証は使い捨てプログラムで実施した。
   L=200 級の stacked vs from_scratch 比較を `tests/test_*_slow.c` として
   追加することを推奨（`make test_slow` 運用に適合）。
3. **aliasing 制約の未文書化**: `udv_combine(l, r, out, w)` は out が l/r と
   別インスタンスであることが前提（out->U 書き込み時に l->U を読むため）。
   `green_from_stack` の combined ≠ left も同様。現在の呼び出しは全て安全だが、
   `linalg.h` / `green.h` に一言明記が望ましい。
4. **plan Task 5 の sweep 統合自動テスト未追加**: 「同一場配置での旧経路との
   一致」を dqmc_sweep レベルで検証する自動テストは未実装（本レビューでは
   旧バイナリ比較で手動確認済み）。指摘 2 と合わせて 1 本の slow テストに
   まとめられる。

## 7. 残タスク（plan Task 7 の未了分）

- kugui/MKL での before/after 計測と β スケーリング確認（リモート作業承認が必要）。
- `VALIDATION.md` への stacked 安定化の検証結果追記。
- ベースライン・効果測定の LOG.md 記録は 2026-07-02 16:07 JST エントリで実施済み。
