---
date: 2026-06-25
datetime: 2026-06-25 15:51 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: DQMC profiling timer 実装の完成後確認（プラン適合・レビュー指摘反映・動作/メモリ検証）
summary: |
  commit d1be8ab の profiler/timer 実装を確認した。プラン・設計どおりに実装され、
  プランレビューの Medium/Low 指摘（hot 判定関数の static inline 化・profiler init 位置）も反映済み。
  全16テスト緑、U=0 解析解一致、profile=0/1 で stdout がビット一致（計測が計算を乱さない証明）、
  opt-in・既定 profile.csv・エラー fail-fast・ASan/UBSan クリーンを確認。指摘は .gitignore の軽微1件のみ。
---

# DQMC Profiling Timer 実装 確認レビュー

## 対象

- 実装 HEAD: `d1be8ab docs: log DQMC profiling timer implementation`
- 関連コミット: `4844e68`(core) / `fa32336`(io) / `60680b9`(beta・sweep wiring) / `f36be98`(green) / `3cd2aa9`(linalg) / `92e0b99`(プランレビュー反映)
- 実装プラン: `docs/superpowers/plans/2026-06-25-dqmc-profiling-timer.md`
- 設計書: `docs/superpowers/specs/2026-06-25-dqmc-profiling-timer-design.md`
- プランレビュー: `docs/reviews/2026-06-25-dqmc-profiling-timer-plan-review.md`
- 追加/変更ソース: `src/profiler.h`, `src/profiler.c`, `src/io.c/h`, `src/main.c`, `src/dqmc.c/h`, `src/green.c`, `src/linalg.c`, `tests/test_profiler.c`, `tests/test_io.c`, `tests/test_dqmc.c`, `tests/test_integration.c`

## 総評

**プラン・設計どおりに実装され、ユーザー報告（「`profile=1`/`profile_file=...` で beta/phase/region 別 CSV、通常実行は stdout 不変・`profile.csv` も作らない」）は実機で確認できた。致命的・重大なバグは無い。** プランレビューの指摘も反映済み。残る指摘は `.gitignore` の軽微 1 件のみ。

## プランレビュー指摘の反映状況

| 指摘 | 反映 |
|---|---|
| Medium: profiler **無効時**のホットパスオーバーヘッド | ✅ `profiler_now_if_enabled` / `profiler_add_elapsed` を `profiler.h` 内で `static inline` 化。無効時はインライン分岐に縮退し cross-TU 呼び出しが消える |
| Low: profiler init 位置でエラー経路クリーンアップが増える | ✅ 非二部チェックの後（beta ループ直前）に `profiler_init` を移動。クリーンアップ要の早期 return は beta/dtau の 1 箇所のみ |
| Low: commit trailer のモデル名 | ✅ 実装者 Codex 自身の trailer（実装モデルと一致しており正しい）|

## 検証結果（すべて合格）

| 項目 | 方法 | 結果 |
|---|---|---|
| 全テスト | `make clean && make test`（16 本、test_profiler・io profile 異常系含む） | ✅ ALL TESTS PASSED |
| 物理の非回帰 (U=0) | `input/1d_L4_U0.txt` を解析解と比較 | ✅ T=2: −1.8484686, T=0.125: −4.0（profiling 追加後も不変）|
| **計測が計算を乱さない** | 同一 seed で `profile=0` と `profile=1` の stdout を `diff` | ✅ **ビット完全一致** |
| opt-in | `profile=0`（既定）で `profile.csv` を作らないこと | ✅ 生成されない |
| CSV 出力 | header と全 15 region の存在を確認 | ✅ 欠落なし |
| stdout 純度 | `profile=1` の stdout に profiler 行が無いこと | ✅ 物理量のみ |
| 既定ファイル名 | `profile=1` かつ `profile_file` 未指定 | ✅ `profile.csv` を生成 |
| エラー fail-fast | `profile=1` かつ書込不可 `profile_file` | ✅ "failed to initialize profiler output"・exit=1・物理出力なし |
| メモリ安全性 | AddressSanitizer + UBSan で test_profiler と profiled dqmc を実行 | ✅ メモリエラー・未定義動作なし |
| コンパイル健全性 | `-std=c11 -O2 -Wall -Wextra` | ✅ 警告ゼロ |

### プロファイラが実際に機能している証拠（β=2, `all` phase）

```
region              calls    frac_beta
beta_total              1      1.000
dqmc_sweep            250      0.997
green_from_scratch   1502      0.810   ← 支配的
  udv_lmul          30040      0.684   ← その内訳（QR 積）が主因
green_wrap          10000      0.097
green_update        22670      0.051
la_gemm             84588      0.136
udv_inv_one_plus     1502      0.063
la_inverse           3004      0.036
```

- inclusive かつ nested で整合（`udv_lmul ⊂ green_from_scratch ⊂ dqmc_sweep`）。
- call 数も妥当（例: `green_wrap = 10000 = L×sweeps×2spin`、`green_from_scratch = 1502 ≒ (periodic+終端)×sweeps×2spin + init`）。
- **小サイズ系では再安定化（from_scratch の QR=`udv_lmul`）が実行時間の約 8 割**という狙い通りの内訳が得られている（将来の最適化対象を示唆）。

## Findings（軽微・任意対応）

### Low: `.gitignore` が profile CSV を対象外

`.gitignore` は `*.dat` を無視するが `*.csv` は対象外のため、既定の `profile.csv`（CWD 出力）や `profile_file=*.csv` の出力が未追跡ファイルとして `git status` に出る。誤コミット防止に `/profile.csv` か `profiles/` 運用を `.gitignore` に追加すると安全。なお既存の `data/**/*.csv` は curated として追跡したいので、blanket `*.csv` 無視ではなく個別指定が無難。

### 情報（設計どおり・バグではない）

- nested inclusive timer のため `frac_beta` の単純合計は 1 を超える（設計書に明記）。有効時は内側タイマーの clock コストが外側 region を押し上げる系統的バイアスがあるが、相対比較には支障なし。

## 結論

**実装は計画・設計どおり完成し、プランレビュー指摘も反映されている。** 物理結果は profile 有無でビット一致し、メモリ安全性・opt-in・エラー処理も確認済み。`.gitignore` の軽微 1 件を除き、そのまま使用・push して問題ない品質。
