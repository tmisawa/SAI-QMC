---
date: 2026-06-26
datetime: 2026-06-26 09:20 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: DQMC MPI replica 並列化「実装」のレビュー（再現性・collective hang・先行指摘の解消確認）
summary: |
  HEAD 4b43f4c の MPI replica 並列実装を現コード精査＋実機検証した。
  先のプランレビューの High 3 / Medium 3 はすべて解消をコードで確認。
  独立検証で serial=omp=mpi が bitwise 一致（np=1/2/4、非整数分割 nrep=5 含む）、
  idle rank・rank0 限定 open 失敗を含む全 failure path で hang 無し、出力 schema 正常。
  実装は正しく堅牢でマージ可能品質。残りは軽微な最適化観察 3 件のみ。
---

# DQMC MPI Replica 並列化 実装レビュー

## 対象

- 実装 HEAD: `4b43f4c`（worktree clean）
- コミット群: `8d0b7f1`(build) → `0ca3a3b`(helper test) → `21a2fc4`(profiler meta) → `b468539`(MPI 実行) → `4b43f4c`(scaling data)
- 精査ファイル: `src/main.c`、`src/replica_mpi.c/h`、`src/profiler.c/h`、`tests/test_mpi_replica.c`、`Makefile`
- 先行レビュー: `docs/reviews/2026-06-26-dqmc-mpi-replica-parallelization-plan-review.md`

## 総評

**実装は正しく堅牢で、そのままマージ可能な品質。** 先のプランレビューで挙げた High/Medium はすべてコードで解消を確認。独立した実機検証でも全項目をパス。残課題は軽微な最適化観察 3 件のみ（バグなし）。

## 実機で確認した結果（macOS arm64 / open-mpi 5.0.8 / mpicc=gcc-15）

| 項目 | 結果 |
|---|---|
| build (`dqmc`/`dqmc_mpi`/`dqmc_omp`) | `-Wall -Wextra` で警告ゼロ |
| `make test` / `test_mpi` / `test_omp` | 全 PASS |
| 再現性 serial nrep1 = mpi np1 | bitwise 一致 |
| mpi nrep4 を np=1/2/4 | 3 者 bitwise 一致 |
| 非整数分割 serial nrep5 = mpi np2 = mpi np3 | bitwise 一致 |
| serial = omp = mpi(np1) (nrep4) | 三経路 bitwise 一致 |
| idle rank (nrep2, np4) | データ行 1 本・hang なし |
| commit 済み smoke data out_np1/2/4 | 3 者 bitwise 一致（永続証拠） |

### collective hang 回避（最重要リスク）— すべて OK

- `parallel=mpi`(serial build) / `hybrid` / `serial under np>1` → 即エラー rc=1、hang なし。
- rank0 限定 open 失敗（bad `replica_log` / bad `profile_file`）を np=2 で → 即終了・hang なし。ループ前 `setup_failed` の `mpi_any_failed` バリア（`src/main.c:420`）が機能。

## 先行プランレビュー指摘の解消（コードで確認）

- **H1**（レガシーテール未分岐）: MPI 分岐末尾 `continue`（`src/main.c:726`）で legacy serial/omp テールを完全分離。MPI は自前で jackknife/`profiler_beta_end`。
- **H2**（前後バリア欠落）: ループ前 `src/main.c:420` ＋ ループ後 close 失敗バリア `src/main.c:840`。
- **H3**（非 root prof UB）: 非 root は `profiler_init_memory(&prof,…)`（`src/main.c:379-381`）。
- **M1**（idle rank の `calloc(0)`）: `alloc_replica_arrays` が `nrep==0` を成功扱い（`src/main.c:95-97`）。
- **M2/M3**（goto-cleanup 宣言ホイスト・failed フラグのスコープ）: 全資源を MPI 分岐先頭で NULL 宣言（`src/main.c:500-521`）、`*_failed` も分岐スコープで 0 初期化。

## collective 整合の精査

MPI 分岐内の 10 collective（`mpi_any_failed`=Allreduce ×6、`MPI_Gatherv` ×3、`mpi_reduce_profiler_stats` 内の Allreduce+Reduce×2）はすべて**グローバル値で `goto` 判定**されるため、全 rank が同一地点で同一の collective 列をスキップ／実行し、ロックステップが崩れない。`p.profile` は全 rank 同条件で reduce の有無も一致。profiler の `BETA_TOTAL` は rank0 wall で上書き、reduce は calls(ull)/total_sec(double) を分離、と設計どおり。

## 残った軽微な観察（バグではない・任意対応）

1. **未使用確保**: MPI モードでも global `results`/`replica_profs`/`replica_failed`（size `p.nrep`）を確保するが使用は `seeds` のみ（`src/main.c:456-458`）。rank/beta ごとに `Profiler×nrep`(~1.4KB×nrep) の無駄。`seeds` だけ確保に絞れる。
2. **命名**: テストは `tests/test_mpi_replica.c`、helper は `replica_mpi_*`。語順が逆で軽い不一致（機能影響なし）。
3. **schema 分岐**: profiler 15 列 / replica_log 9 列（MPI）と 14 列 / 8 列（serial/omp）の差は設計どおりの意図的分岐。下流は header 名で読む前提。

## サニタイザ注記

ASan/UBSan は本機の mpicc(gcc-15) で sanitizer ランタイムがリンクできず未実施（LeakSanitizer は macOS arm64 非対応）。メモリ／cleanup 経路は手動トレースで健全と確認（goto 前に全資源 NULL 宣言、二重 free・NULL deref なし、idle rank で `calloc(0)`→`free(NULL)` 安全）。Linux 環境があれば `make` を gcc+`-fsanitize=address,undefined` で再確認すると望ましい。

## 結論

マージ可能。観察 1 は気が向いたら程度の最適化。
