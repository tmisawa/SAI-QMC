---
date: 2026-06-25
datetime: 2026-06-25 17:57 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: DQMC replica 並列化 実装の完成後確認（プランレビュー指摘の修正・決定性・スレッド安全性・物理不変性）
summary: |
  commit 12b2378/9bf4bb7 の replica 並列化実装を確認した。プランレビューの確定バグ 2 件＋整合ギャップ 1 件は
  すべて修正済み。serial/OMP 全テスト緑、U=0 解析解一致、リファクタ前と nrep=1 がビット一致、
  serial nrep=4 と omp nrep=4 がビット一致、ThreadSanitizer で無競合、ASan/UBSan クリーンを実機確認した。
  致命的バグ無し。指摘は運用上の留意 2 点のみ。
---

# DQMC Replica 並列化 実装 確認レビュー

## 対象

- 実装コミット: `082ffd8`(bins/seeds) / `0f2fc65`(runner 切り出し) / `42bd864`(serial combine) / `843f533`(profiler TLS) / `dfac8b6`(local profiler) / `12b2378`(OpenMP) / `9bf4bb7`(LOG)
- 実装プラン: `docs/superpowers/plans/2026-06-25-dqmc-replica-parallelization.md`（`dd9bfd3` でプランレビュー反映済み）
- プランレビュー: `docs/reviews/2026-06-25-dqmc-replica-parallelization-plan-review.md`
- 確認ソース: `src/main.c`, `src/replica.c/h`, `src/replica_run.c/h`, `src/profiler.c/h`, `Makefile`, `tests/test_replica.c`

## 総評

**プランどおりに正しく実装され、プランレビューで指摘した確定バグ 2 件＋整合ギャップ 1 件はすべて修正済み。並列化の正しさ（決定性・データ競合なし）と物理の不変性を多角的に実機で実証した。致命的・重大なバグは無い。**

## プランレビュー指摘の修正確認

| 指摘 | 修正 |
|---|---|
| High: `tests/test_replica.c` の `e_ph` 期待値誤り | ✅ `-11.0` / `-5.5` に修正、test_replica 緑 |
| Medium-High: Makefile `filter-out` が `main.omp.o` を残す | ✅ `LIBSRC_OMP = $(filter-out src/main.omp.o,$(OMP_OBJ))`、`make test_omp` リンク成功 |
| Medium: replica_log が OMP で消失/race | ✅ 並列領域**後**の serial ループで `r` 昇順出力。status も `ok`/`error` を反映（設計より改善）|

## 検証結果（すべて合格）

| 項目 | 方法 | 結果 |
|---|---|---|
| serial 全テスト | `make clean && make test`（17 本, test_replica 含む） | ✅ ALL TESTS PASSED |
| OpenMP 全テスト | `make dqmc_omp && make test_omp` | ✅ ALL OMP TESTS PASSED |
| 物理 (U=0) | 自由電子解析解と比較 | ✅ T=2: −1.8484686, T=0.125: −4.0 |
| リファクタ前との一致 | `da35273` を別ビルドし nrep=1 出力を `diff` | ✅ **ビット完全一致**（legacy seed で既存データ再現性維持）|
| serial=omp 決定性 | nrep=4 を serial と omp で `diff`（BLAS=1） | ✅ **ビット完全一致** |
| データ競合 | ThreadSanitizer で OMP nrep=4 | ✅ **NO DATA RACE** |
| メモリ安全性 | ASan+UBSan（test_replica・serial nrep=4） | ✅ クリーン |
| 予約 mode/omp ガード | mpi/hybrid/omp(serial build) | ✅ 全て exit=1＋明示エラー |
| replica_log | nrep=3 の CSV | ✅ id=0 は base seed、id>0 は混合 seed、status=ok |
| profiler 並列集約 | OMP profiled run の CSV | ✅ `wall_sec/thread_total_sec/nrep/parallel` 列、`dqmc_sweep` frac≈3.92（≈4 replica 並走）|

### 核心の正しさ（コードレビューと実証の整合）

- **per-replica 完全隔離**: `dqmc_run_replica` が独立した `Model/Field/Dqmc/Rng` を確保し、並列ループ内の書き込みは `results[r]`/`replica_profs[r]`/`replica_failed[r]` のみ。共有 mutable 状態なし → TSan で無競合を実証。
- **`_Thread_local` current profiler**: 各スレッドが自 replica の profiler を参照し、`green.c`/`linalg.c` の計測が正しい replica に入る。
- **決定的合成**: 並列領域**後**に `r` 昇順で merge / bin 変換 / replica_log / jackknife → serial と omp がビット一致。
- **legacy seed**（`replica_id=0` = `base + 1000*beta_index`）でリファクタ前と完全一致。

## 軽微な点（バグではない・運用上の留意）

- **決定性は単一スレッド BLAS が前提**: serial=omp のビット一致は `VECLIB_MAXIMUM_THREADS=1`（/ `OPENBLAS_NUM_THREADS=1`）下で検証。小行列では実害ほぼ無いが、設計どおりテスト/運用で 1 に固定する想定。コード強制ではなく運用要件。
- **profiler `beta_total` の `thread_total_sec` は wall 値**（replica は `beta_end` を呼ばず merge のみのため）。他 region は replica 合算で `frac_beta>1` になる一方、`beta_total` だけ `frac=1`。設計の `wall_sec=beta_total`・`frac_beta=thread/wall` の意図どおりだが、解析時に留意。

## 結論

**replica 並列化は完成し、決定性・スレッド安全性・物理不変性・後方互換のすべてを実機で確認した。** プランレビュー指摘も完全反映されており、そのまま使用・push して問題ない品質。
