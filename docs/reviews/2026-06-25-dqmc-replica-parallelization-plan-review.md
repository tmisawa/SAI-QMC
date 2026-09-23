---
date: 2026-06-25
datetime: 2026-06-25 17:06 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: DQMC replica 並列化 実装プランのレビュー（設計反映確認・確定バグ・整合ギャップ）
summary: |
  commit aedbc0c の replica 並列化実装プランをレビューした。設計レビュー指摘(profiler TLS 化・
  replica_id=0 の旧 seed 互換・BLAS 1 スレッド・header prefix 維持・libomp フラグ)は全反映済み。
  ただし TDD をブロックする確定バグ 2 件(test_replica の e_ph 期待値、Makefile の filter-out)と、
  OpenMP で表面化する整合ギャップ 1 件(replica_log の serial 出力化)を検出。いずれも実機で確証した。
---

# DQMC Replica 並列化 実装プラン レビュー

## 対象

- 実装プラン: `docs/superpowers/plans/2026-06-25-dqmc-replica-parallelization.md`（commit `aedbc0c`、1788 行）
- 設計書: `docs/superpowers/specs/2026-06-25-dqmc-replica-parallelization-design.md`（設計レビュー反映済み, commit `11b42fe`）
- 設計レビュー: `docs/reviews/2026-06-25-dqmc-replica-parallelization-design-review.md`
- 確認した現コード: `src/main.c`, `src/profiler.c`, `src/measure.h`, `Makefile`

## 総評

**設計レビューの指摘はすべて反映されており、OpenMP の per-replica 隔離・TLS current profiler・replica_id 順の決定的合成・bit 一致検証も正しく設計されている。完成度は高い。**

ただし**そのまま実装すると TDD が止まる確定バグ 2 件**と、**OpenMP で表面化する整合ギャップ 1 件**がある。着手前の修正を推奨する。

設計レビュー指摘の反映状況:

| 指摘 | 反映 |
|---|---|
| Medium 1: profiler current のスレッドローカル化 | ✅ Task 5 で `static _Thread_local Profiler *g_current_profiler` |
| Medium 2: seed 方式と serial 等価 | ✅ Task 2 で `replica_id=0` は `base+1000*beta_index`（旧式）、`>0` は mixer、衝突チェック。baseline diff で bit 一致検証 |
| Low 3: BLAS 1 スレッド固定 | ✅ 全検証コマンドで `VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1` |
| Low 4: test_profiler ヘッダ更新 | ✅ prefix/substring 検証＋新列存在チェックへ |
| Low 5: macOS libomp フラグ | ✅ `-Xpreprocessor -fopenmp -I$(LIBOMP_PREFIX)/include … -lomp`、`LIBOMP_PREFIX ?= /opt/homebrew/opt/libomp` 上書き可 |

## 確定バグ（実機確認済み・TDD をブロック）

### High: Task 2 `tests/test_replica.c` の `e_ph` 期待値が誤り

`sample.E=-1.5, ntot=4, mu=2, U=4, nsite=4` のとき
`e_ph = E - 0.5·U·ntot + 0.25·U·nsite = -1.5 - 8 + 4 = -5.5`（`replica.c` の式・`main.c` と同一）。
しかしテストは:

- `CHECK(fabs(bin.sum_sign_Eph - (-3.0)) < 1e-12)` → 正しくは **-11.0**（2 サンプル合計）
- `CHECK(fabs(ep - (-1.5)) < 1e-12)` → 正しくは **-5.5**

`e_ph = e_hub` と取り違えた値で、**Task 2 Step 5 の `./tests/test_replica` が「OK」にならず落ちる**。実装（`replica.c`）の式は正しいので、修正は**テスト期待値**を `-11.0` / `-5.5` に直すこと。`e_gc`(-19.0/-9.5)・`N`(4.0)・`D`(0.125) は正しい。

### Medium-High: Task 7 `Makefile` の `LIBSRC_OMP` が `main.omp.o` を除外できない

```make
LIBSRC_OMP = $(filter-out src/main.c,$(OMP_OBJ))   # src/main.omp.o は除去されない
```

実機確認: `LIBSRC_OMP = src/dqmc.omp.o src/main.omp.o src/green.omp.o`（`main` 残存）。各 test は自前 `main()` を持つため、`tests/test_%_omp` が **`main` 重複でリンク失敗** → **Task 7 Step 6 `make test_omp` が通らない**。

修正:

```make
LIBSRC_OMP = $(filter-out src/main.omp.o,$(OMP_OBJ))
```

（serial 側 `LIBSRC = $(filter-out src/main.c,$(SRC))` は `.c` リストなので正しい。omp 側は `.omp.o` リストなのでパターンを合わせる必要がある。）

## 整合ギャップ（OpenMP で表面化）

### Medium: replica_log の行出力が Task 7 のループ置換で消える／race になる

Task 4 Step 6 は replica_log の `fprintf` を **replica ループ内**（serial）に置く。Task 7 Step 4 はそのループを OMP/serial 条件分岐に**丸ごと置換**するが、置換後のループ本体（OMP 枝・serial 枝とも）に `fprintf(replica_fp, …)` が**無い**。

- OMP 枝の中に置けば、共有 `FILE*` への並行書き込みで **data race / CSV 破損**。
- 現状のままだと replica_log 出力が**実質消失**。

設計の「file I/O は serial に行う」に従い、**並列領域の後に別の serial ループ（`r` 昇順）で `seeds[r]` / `results[r].status` を書く**よう、プランに明記すべき（決定的順序にもなる）。

## 軽微（Low）

- **`Ehub` 等の確保が Task 3/4 に分割**: `total_bins` が `p.nbin`→`nrep*nbin` に変わるので calloc の更新が必要（やや誤りやすいが可）。
- **`replica_fp` のエラー経路クローズ**: beta ループ前に open するため、ループ内の各 `return 1` で閉じる必要（プランは一般注記のみ、Step 5 のエラーブロックには未記載＝終了時 fd リーク。プロセス終了で実害はほぼ無し）。
- **commit trailer**: Codex 固定だが「実装モデルに置換」と明記済みで適切。

## 良い点（評価）

- 設計レビュー指摘の完全反映（上表）。
- OpenMP 正当性: per-replica の独立 `Model/Field/Dqmc/Rng`、`_Thread_local` current profiler、`results[r]`/`replica_profs[r]` への分離書き込みで **data race 無し**。
- 強い回帰検証: `nrep=1` の baseline diff、serial vs omp の固定 seed diff（BLAS=1）でビット一致を担保。
- numerator/denominator bin（符号問題対応・現行の bin 毎 ratio→jackknife と統計的に一致）。
- reserved mode／OMP guard／zero-sign fail-fast／seed 衝突 fail-fast。
- 妥当な TDD 実装順序。

## 結論

方向性・構造は良好。**確定バグ 2 件（test_replica 期待値、Makefile filter-out）と Medium 1 件（replica_log の serial 出力化）を修正すれば、安全に実装へ進める。** Low 3 件は実装時に反映すれば足りる。
