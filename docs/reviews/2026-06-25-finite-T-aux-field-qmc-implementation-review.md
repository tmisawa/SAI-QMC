---
date: 2026-06-25
datetime: 2026-06-25 11:57 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: 有限温度補助場 QMC (DQMC/BSS) 実装の完成後コードレビュー（計画適合性・潜在バグ・物理検証）
summary: |
  commit f259aff の実装を、計画書 Task 0〜15 との適合性と潜在バグの両面から徹底レビューした。
  Critical/High バグは無し。コア物理式 (A.95/A.107/A.108/A.99/A.56) を手計算で再導出し実装と一致を確認、
  全テスト緑、U=0 解析解一致、U=4 を独立実装の厳密対角化(ED)と dtau→0 外挿で 1σ 以内一致、
  低温(β=20)安定、ASan/UBSan クリーンを確認。指摘は軽微4件（未使用 expKinv 等）のみ。
---

# 有限温度補助場 QMC (DQMC/BSS) 実装レビュー

## 対象

- 実装レビュー対象 commit: `f259aff fix(dqmc): restabilize Green functions at sweep end`
  （レビュー文書追加前の実装 HEAD）
- 実装計画: `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md`（Task 0〜15）
- 設計書: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md`
- リファレンス: 大塚雄一 博士論文 付録A
- レビュー対象ソース: `src/` 全 12 モジュール（linalg / lattice / model / field / rng / green / dqmc / measure / io / main）+
  `tests/test_*.c` 15 本（補助ヘッダ `tests/test_util.h` を除く）

## 総評

**計画書 Task 0〜15 はすべて実装されており、相互作用ケース (U>0) まで含めて物理的に正しいことを独立検証で確認した。Critical/High に該当するバグは検出されなかった。** 指摘は軽微な最適化余地（デッドコード・冗長計算）4 件のみで、正しさには影響しない。

計画外の差分は 1 点のみ（`dqmc_sweep` 末尾の再安定化）で、これは `stab_interval > Ltr` の高温点で `sign=-1` が出る不具合への意図的修正（commit `f259aff`、回帰テスト付き、LOG 記録済み）であり妥当。

LOG.md の handoff にある「全体コードレビュー」は本レビューで完了とみなせる。

## 検証方法と結果

| 検証 | 方法 | 結果 |
|---|---|---|
| 全テストスイート | `make test`（15 本の `test_*.c`） | ✅ ALL TESTS PASSED |
| U=0 自由電子 | 解析解 `E=2Σ_i ε_i f(ε_i)` と比較 | ✅ 誤差 ~1e-6（T=2: −1.8485, T=0.125: −4.0）|
| **U=4 相互作用** | **独立実装の grand-canonical ED と照合** | ✅ **dtau→0 外挿が厳密値と 1σ 以内で一致** |
| 低温安定性 | β=8,12,16,20 (T=0.05) を実行 | ✅ ntot=4, sign=1, NaN なし |
| 非二部格子の拒否 | 3-site chain PBC | ✅ 実行エラー終了 |
| 入力パーサ異常系 | 未知 key / 不正数値 / nmeas%nbin≠0 等 | ✅ 全て検出しエラー |
| メモリ安全性 | AddressSanitizer + UBSan でテスト・実行 | ✅ メモリエラー・未定義動作なし |
| コンパイル健全性 | `-std=c11 -O2 -Wall -Wextra` | ✅ 警告ゼロ |

### U=4 の ED 照合（相互作用部の最重要検証）

4-site Hubbard ring (PBC, t=−1, U=4) を grand-canonical (μ=U/2=2) で独立に厳密対角化し、QMC の `E_hub=⟨H0⟩` を dtau=0.1,0.05,0.025 から `dtau²` 線形外挿して比較した。

| T | QMC (dtau→0 外挿) | ED 厳密 | 差 |
|---|---|---|---|
| 1.0 | −0.6258 ± 0.0082 | −0.62187 | 0.004（1σ 内）|
| 0.25 | −1.9053 ± 0.0151 | −1.90837 | 0.003（1σ 内）|

- エネルギーは dtau=0.1→0.05→0.025 で単調に `dtau²` 収束し、外挿値が厳密値と統計誤差内で一致。
- doublon も ED（T=1: 0.108, T=0.25: 0.078）と一致。
- これにより **U=0 では検証不能な相互作用項・HS 変換・補助場符号・rank-1 更新の正しさが定量的に確定した。**

## コア物理の式ごとの確認（付録A と照合）

実装の核心部はすべて導出を手計算で追い、コードと一致することを確認した。

- **B 行列** (A.22/A.109) `B_lσ = expK·diag(exp(λσ s_il − ΔτU/2))`：expK の列 j を係数倍する column-major 実装が正しい。`src/green.c` `green_build_B`。`tests/test_green_buildb.c` で 2-site 手計算固定。
- **Green 初期化** (A.95) 巡回積 `g_{l0}=(I + B_{l0-1}…B_0 B_{L-1}…B_{l0})^{-1}`：UDV の lmul 順序が正しい。`src/green.c` `green_from_scratch`。`tests/test_green_init.c` が U>0 非可換系で l0=0/1 を独立検証。
- **rank-1 更新** (A.107)：Sherman–Morrison/Woodbury を独立に再導出。右掛け `A_l → A_l(I+Δ)`（補助場は B_l の右端）に対し
  `g' = g − (N/R)·(δ_ij − g_ji)·g_ik` が得られ、コードと**完全一致**。`src/green.c` `green_update`。
- **受理比** (A.98/A.99) `R_σ = 1+(1−g_ii)N`、`R=R↑R↓`、受理 `min(1,|R|)`：正しい。`src/dqmc.c` `dqmc_sweep`。`N` は反転前の場から 1 回だけ計算し ratio と update で共有する設計（pre/post 取り違えを構造的に排除）も計画どおり。
- **wrapping** (A.108) `g_{l+1}=B_l g_l B_l^{-1}`：`A_{l+1}=B_l A_l B_l^{-1}` から導出して正しい。`src/green.c` `green_wrap`。`tests/test_green_wrap.c` で from_scratch と一致確認。
- **UDV 安定化** `(I+UDT)^{-1}=T^{-1}M^{-1}D_b^{-1}U^T`, `M=D_b^{-1}U^T T^{-1}+D_s`：式を検算（×(I+UDT)=I）し、QR 蓄積 `U'=Q, D'=diag(R), T'=diag(1/D')R·T` ともに実装と一致。`src/linalg.c` `udv_lmul`/`udv_inv_one_plus`。`tests/test_udv.c` の強スケール分離 residual test も緑。
- **測定** (A.56) `ekin=Σ_ij t_ij(δ_ij−g_ji)`（μ 項を分離するため `L.t` の零対角を使用）、`⟨n↑n↓⟩=(1−g_ii↑)(1−g_ii↓)`、規約変換 `E_hub/E_gc/E_ph`：正しい。`src/measure.c`、`src/main.c`。
- **スライス管理**：sweep 全体で不変条件 `cur_l==l` が成立（init で 0、wrap 毎に +1、L 回で 0 復帰、周期安定化後も nl=(l+1)%L で整合）。`src/dqmc.c`。

## 計画との対応

- Task 0〜15 すべて実装済み。git 履歴が Task 単位のコミットと一致（`0c01c0c` scaffold 〜 `f259aff` 末尾再安定化）。
- 計画書記載のテストはすべて存在し緑。加えて `tests/test_dqmc.c`（計画外、sign 回帰）も追加されている。
- **計画外の差分（意図的・妥当）**: `dqmc_sweep` 末尾に無条件の `green_from_scratch(0)` を追加（`src/dqmc.c` 58–59 行, commit `f259aff`）。`stab_interval > Ltr` で sweep 内安定化が一度も走らず warmup 中に誤差蓄積→`sign=-1` となる経路の修正。安定化間隔を stab_interval で確実に上限化する。LOG.md 記録・回帰テストあり。

## Findings（軽微・バグではない／任意対応）

### Low: `expKinv` が未使用（デッドコード）

`src/model.c` で `expKinv=exp(+ΔτK)` を計算・保持・free しているが、どこからも参照されない（`green_wrap` は `la_inverse(B)` で B^{-1} を計算）。`grep expKinv src/` は model.c/model.h のみ。
→ 削除するか、`green_wrap` で `B^{-1}=diag(1/d)·expKinv` を用いれば `la_inverse` を 1 回省ける。

### Low: 測定時の二重 from_scratch（冗長計算）

`dqmc_sweep` 末尾（`src/dqmc.c` 58–59）で slice 0 を純化済みなのに、`src/main.c` 104–105 が測定前に再度 `green_from_scratch(0)` を呼ぶ。結果は同一で冗長。
→ main 側の 2 行を削れば測定ごとの安定化コストが半減（正しさには影響なし）。

### Low: 2×2 PBC square が実行可能

2×2 PBC は同一ペアの二重結合（t=−2）が生じるが `is_bipartite=1` となり実行できる。計画では「定量比較に使わない」と文書化済みだが、コード側の警告はない。`hopping_used.txt` ダンプで確認可能なため実害は低い。
→ 必要なら防御的警告を入れてもよい（任意）。

### Low: 測定が slice 0 のみ

等時刻量として不偏推定であり正しいが、全スライス平均にすれば統計効率が上がる。
→ 将来の統計向上拡張候補。

## 推奨

1. （任意）上記 Low 指摘のうち、`expKinv` 削除と main の二重 from_scratch 削除は小さな整理として取り込んでよい。
2. （任意）本レビューで用いた grand-canonical ED 照合スクリプトを `tests/` ないし `VALIDATION.md` の手順に正式追加すると、U>0 の数値回帰が自動化できる。
3. それ以外は現状のまま発表・push して問題ない品質。
