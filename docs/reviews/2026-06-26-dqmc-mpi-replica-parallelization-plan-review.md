---
date: 2026-06-26
datetime: 2026-06-26 08:37 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: DQMC MPI replica 並列化「実装計画」のレビュー（構造ギャップ・collective hang・UB）
summary: |
  2026-06-26 の MPI 実装計画を全文（2016 行）と既存コード（main.c/measure.h/profiler.c）を突き合わせてレビューした。
  helper 単体・rank 分配/gatherv 算術・pack/unpack・再現性ロジックは堅実で設計レビュー指摘も取り込み済み。
  ただし計画どおり実装すると破綻/hang する構造ギャップが High 3 件:
  (H1) β毎レガシー後処理テールが MPI 分岐されず空の global results を処理、
  (H2) ループ前後の collective バリアが検証のみで実装ステップ欠落、
  (H3) 非 root の global prof 未初期化で UB。
  加えて Medium 3 件（idle rank の calloc(0)、goto-cleanup の宣言ホイスト、failed フラグのスコープ）、Low 4 件。
---

# DQMC MPI Replica 並列化 実装計画 レビュー

## 対象

- 計画書: `docs/superpowers/plans/2026-06-26-dqmc-mpi-replica-parallelization.md`（全 2016 行）
- 設計書: `docs/superpowers/specs/2026-06-25-dqmc-mpi-replica-parallelization-design.md`
- 照合した現コード: `src/main.c`、`src/measure.h`、`src/profiler.c/h`、`src/replica.c/h`、`tests/test_util.h`、`tests/test_profiler.c`、`Makefile`

## 総評

helper の単体テスト・算術・再現性ロジックは正しく、先行の設計レビュー指摘（fail-flag allreduce、status 合成、gather 順序の厳密化）も `replica_mpi_local_failed` / `replica_mpi_gatherv_layout` で取り込まれている。**ただし「既存 main.c の per-beta 後処理を MPI 用に分岐する」構造変更が過小記述で、計画どおり実装すると破綻/hang する箇所が High 3 件。** これらは着手前に明示ステップとして計画へ埋めるべき。

## 検証して正しいと確認できた点

- rank 分配・gatherv layout・pack/unpack の算術: Task 2 のテスト値を手計算で再検証（nrep=10/nranks=3、nrep=5/nbin=2/nranks=3、idle nrep=2/nranks=4）すべて整合。
- seed の rank 数非依存: 全 rank が既存ループ（`src/main.c:182-184`）で global table を生成し `seeds[gid]` を引く設計。
- bin 再構成順: `displs=first_replica*nbin*6` で global id 順 Gatherv → `global_bins[r*nbin+bi]` → serial と同一順序の jackknife。bitwise 一致の主張は妥当。
- profiler reduce の混在型分離（calls=ull / total_sec=double を別 `MPI_Reduce`）、BETA_TOTAL の扱い（rank_prof/local prof は beta_end 未呼出で 0、rank0 が wall で上書き）も `src/profiler.c` の挙動と整合。
- Task 2 のテストは `MeasSample` の実フィールド（E/ekin/eint/ntot/doublon, `src/measure.h`）と一致しコンパイル可能。`file_contains` は `tests/test_profiler.c` に static 定義済みで Task 3 が再利用可。

## High（実装前に計画へ追記すべき）

### H1. β毎の「レガシー後処理テール」が MPI 用に分岐されていない
既存 `src/main.c:224-339`（profiler_merge → replica_log → `results` からの bin 抽出 → jackknife → stdout → `profiler_beta_end`）は Task 5 の `if(mpi){…} else {…}` の**後ろにそのまま残る**。MPI 分岐は LOCAL 配列（`local_results`）に書くので、このテールは**未使用の global `results`（空）**を処理し、zero-sign bin エラーか二重出力になる。計画は MPI 後処理を Task 6–8 で `if(mpi)` 分岐内に足すが、「**レガシーテールを `else`/非 MPI に閉じ込める（または MPI 分岐末尾で `continue`）**」という明示ステップが無い。最大の構造ギャップ。

### H2. βループ突入前／終了後の collective バリアが「検証」だけで「実装ステップ」が無い
Task 4 Step 3 は profiler_init / replica_log open を rank0 限定にする（`src/main.c:79`, `src/main.c:124`）。だが βループ最初の collective は Task 6 の status gather。**rank0 の open 失敗を共有する `mpi_any_failed` を「βループ前」に置く実装ステップが無い**（Task 9 Step 3 は hang しないことを検証するだけ）。同様にループ後の `fclose`/`profiler_close` 失敗共有（Step 5 は検証のみ）。「rank0 setup 後・ループ前の 1 回 allreduce」「ループ後 close 失敗 allreduce」を明示ステップ化しないと、計画どおり実装しても Task 9 検証で hang する。

### H3. 非 root の global `prof` 未初期化（UB）
Task 4 で `profiler_init` を rank0 限定にすると、非 root の `prof` は未初期化のまま `profiler_set_current(&prof)`（`src/main.c:85`）・`profiler_beta_begin(&prof)`（`src/main.c:158`）・`profiler_beta_end` 等に渡され、ガベージ `enabled`/`fp` を触る危険。**非 root は `profiler_init_memory(&prof, p.profile)` で初期化する**ことを明記すべき。

## Medium

### M1. idle rank の `calloc(0)` を失敗扱いにする（Task 5）
`alloc_replica_arrays(local_nrep=0, …)` は `calloc(0,…)` が NULL を返す実装で `return 1` → `local_setup_failed` → `mpi_any_failed` で**全 rank 巻き込み失敗**。Task 6 の status alloc は `local_nrep > 0` でガードしている（plan 行 1094 付近）のに Task 5 は非一貫。`nrep<nranks`（Task 7 Step 5 の idle テスト）が libc 依存で壊れ得る。helper も count==0 を成功扱いにする。

### M2. `goto mpi_beta_cleanup` には全資源の先頭 NULL 初期化が前提
Task 9 の単一ラベルは Tasks 5–8 でインライン宣言された約 19 個のポインタを解放する。C で goto が初期化子を飛び越えると不定値になるため、**Task 5 の段階から MPI 分岐の全資源を分岐先頭で宣言＋NULL 初期化**しておく必要がある。計画は Task 9 でラベルを後付けするだけで前段タスクの宣言位置を直していない。先頭ホイストを Task 5 に織り込むべき。

### M3. `mpi_any_failed` に渡すフラグのスコープ
Task 7 Step 2 の `root_post_failed` は rank0 ブロック内宣言に読めるが、直後の `mpi_any_failed`（全 rank が同値で呼ぶ collective）に渡る。**分岐スコープで宣言・非 root では 0 初期化**を明記（`any_failed` ほか各 `*_failed` 同様）。

## Low

- **L1. omp×MPI の検証分岐が dead code**（Task 4 Step 4）: MPI build は OpenMP 無効なので guard #2（`#ifndef AFQMC_USE_OPENMP` の omp エラー）が先に捕捉し、後段 `nranks>1 && omp` は到達不能。簡素化推奨。
- **L2. profile=0 でも profiler reduce が走る**（Task 8）: collective として正しいが無駄。`if(p.profile)` で囲える（全 rank 同条件で安全）。
- **L3. Task 11 の出力パス不整合**: `profile_np*.csv`/`replicas_np*.csv` は sed でファイル名のみ置換しディレクトリを付けないため CWD（repo root）に出力され `git add $run_dir` に含まれない。`out_np*.dat` だけ run_dir。run_dir prefix を付ける。
- **L4. 些細**: Task 5 の `local_setup_failed` 宣言が未提示、`local_seeds` コピーは事実上不要。

## 実装着手時の推奨順

1. Task 5 着手前に、MPI 分岐を「per-beta 完結ブロック＋末尾 `continue`」として設計し、レガシーテール（`src/main.c:224-339`）を `else` に閉じ込める（H1）。
2. 同時に MPI 分岐内の全資源を先頭で NULL 宣言し goto-cleanup を成立させる（M2）。
3. Task 4 で非 root `prof` を memory 初期化（H3）、rank0 setup 後・ループ前の allreduce バリアを追加（H2 前半）。
4. ループ後 close 失敗 allreduce を追加（H2 後半）。
5. `alloc_replica_arrays` を count==0 成功扱いに（M1）。
