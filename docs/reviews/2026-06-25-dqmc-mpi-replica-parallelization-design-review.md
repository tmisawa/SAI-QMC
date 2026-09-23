---
date: 2026-06-26
datetime: 2026-06-26 08:10 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: DQMC MPI replica 並列化設計のレビュー（既存コード整合・collective hang・再現性）
summary: |
  commit 52f1369 の MPI replica 並列化設計を現コード（main.c/replica.c/replica.h/profiler.c/io.c/Makefile）と
  突き合わせてレビューした。骨子（global replica id 固定 seed・contiguous block 分配・rank0 集約・
  struct 非依存 pack・物理量 bitwise 再現性）は堅実で既存構造とよく整合する。
  実質リスクは collective hang を生む rank0 限定 early-return パスの未列挙（High 3 件）。
  加えて status の意味・gather 順序の保証・失敗時 profiler 挙動の明文化（Medium 3 件）と
  CSV スキーマ非対称・omp ガード優先順位・Makefile clean の小修正（Low 3 件）。
---

# DQMC MPI Replica 並列化設計 レビュー

## 対象

- 設計書: `docs/superpowers/specs/2026-06-25-dqmc-mpi-replica-parallelization-design.md`（commit `52f1369`）
- 照合した現コード: `src/main.c`、`src/replica.c`、`src/replica.h`、`src/replica_run.c`、`src/profiler.c/h`、`src/io.c`、`Makefile`

## 総評

**骨子は妥当で、既存コードの構造とよく整合している。** global replica id のみから seed を決め、contiguous block で rank に分配し、rank 0 が global id 順に bin を集約する方針は、物理量の bitwise 再現性（BLAS thread=1）を保つうえで正しい。`ReplicaBin` を struct padding 非依存で pack する判断、profiler を rank-local stat として first-class に保つ判断も適切。

致命的な設計誤りは無い。**唯一の実質リスクは collective hang を生む「rank0 限定 early-return」パスの未列挙**（§High）。設計は「fail flag を allreduce」という原則は書くが、既存 `main()` に散在する rank0 限定 `return 1` の**どれが collective の前後に来るか**を列挙していないため、実装者が個別パスを取りこぼす危険がある。

## 既存コードと照合して確認できた点（正しい）

| 設計の主張 | 実コードでの確認 |
|---|---|
| `ReplicaBin` = double×6 + int count、double 配列＋int count に pack | `src/replica.h:6-14` で完全一致 |
| seed 式を現行のまま固定（id=0 は `base+1000*beta`、id>0 は splitmix64） | `src/replica.c:16-33` と一致、global id のみ依存で rank 非依存 |
| stdout header 末尾に `nranks` を追加（prefix 保持） | `src/main.c:113-116` は `bins=%d` 終端、末尾追加で互換維持 |
| `replica_log` 末尾に `rank` 列追加 | `src/main.c:134` は 8 列、末尾追加で互換維持 |
| profiler CSV 末尾に `nranks` 追加 | `src/profiler.c:65` は `...,nrep,parallel` 終端 |
| profiler 集約は `calls`(ull)/`total_sec`(double) を別配列で Reduce | `src/profiler.h:34-37` は混在型 struct なので別 gather は正しい |
| rank_range の contiguous block 式 | nrep=10,nranks=3 を手計算で検証（0-3/4-6/7-9）一致 |
| 物理量 bitwise 再現性（BLAS=1） | 物理は rank0 で global id 順配列を集約 → serial と同一順序の jackknife。profiler timing のみ rank 数依存だが物理に無影響 |

`parallel=mpi`/`hybrid` の予約エラーは既に `src/main.c:87-102` にあり、設計の方向と一致。

## High（着手前に設計へ追記すべき）

### H1. gather 後・rank0 限定の zero-sign bin 検査による β跨ぎ hang
`replica_bin_values()` が非ゼロを返すと rank0 だけ `return 1`（`src/main.c:288-314`）。これは bin gather の**後**だが、rank0 が抜けると他 rank は**次のβの collective（seed 後の gather）**で待ち続け hang する。bare return ではなく fail flag を `MPI_Allreduce(MAX)` して全 rank 終了が必須。

### H2. startup の rank0 限定 open 失敗
βループに入る**前**、rank0 のみで profiler open（`src/main.c:79-84`）と replica_log fopen（`src/main.c:124-133`）。失敗して rank0 だけ return すると、他 rank は最初のβの collective で hang。設計は「write error」は触れるが**初回 open 失敗**が抜けている。**βループ突入前に setup 成否の allreduce バリアを 1 回**入れる設計を明記。

### H3. rank0 限定の measurement-bin alloc 失敗
`Ehub` 等の確保は集約後 rank0 のみ（`src/main.c:262-286`）。失敗時は rank0 だけ return しないよう、`MPI_Abort`（設計が許容）か fail flag で全 rank 終了に。

> 推奨: 「**βループ内のすべての collective の直前に fail-flag を `MPI_Allreduce(MAX)` し、立っていたら全 rank 揃って残り collective をスキップして finalize**」という不変条件を設計に 1 文で固定し、H1〜H3 を「rank0 限定・collective 前/後」として表に列挙する。原則だけでは個別パスを取りこぼす。

## Medium（仕様の曖昧さ・要明記）

### M1. gather する status の中身
serial の失敗判定は `replica_failed[r] || results[r].status != 0`（`src/main.c:238`）の**両方**の畳み込み。`result.status` は「1 で開始 → 成功で 0」の反転規約（`src/replica_run.c:51,65`）なので、`result.status` 単体を送ると意味が逆になる。gather 対象は「合成済み failed フラグ」と明記。

### M2. gather 順序は「一致しやすい」でなく「厳密一致」
設計 L143 の「global id order と一致しやすい」は弱い。`MPI_Gatherv` の `displs[r] = first_replica[r]*nbin*6` を `mpi_rank_range` から計算すれば**構造的に厳密一致**する。再現性の根拠なので「by construction で厳密」と言い切る。

### M3. 失敗時に profiler 出力が消える挙動差分
serial は失敗でも profiler merge と replica_log 書き込みを行う（`src/main.c:224-248`）が、設計の擬似コードは失敗時に profiler reduce 前に exit。許容範囲だが「失敗βは profile 行を出さない」差分を明記。あわせて、失敗時は bin gather と profiler reduce を**全 rank で揃ってスキップ**する旨を明示（片側だけ collective 参加 → hang 防止）。

## Low（小修正）

### L1. CSV スキーマが build 間で非対称
MPI build のみ profiler 15 列 / replica_log 9 列、serial/omp は 14/8 列。設計は「header 名で読む」で回避するが、保守上は serial/omp も `nranks=1` / `rank=0` を常時出して**全 build 同一スキーマ**にする方が下流が楽。検討推奨。

### L2. MPI build での `parallel=omp` ガード優先順位
MPI build は `-DAFQMC_USE_MPI` のみで `-DAFQMC_USE_OPENMP` 無し想定。すると `parallel=omp` は np に関係なく既存の「requires OpenMP-enabled build」（`src/main.c:104`）に当たる。設計の「serial|omp を nranks>1 でエラー」は omp について冗長/矛盾気味。**build 能力チェック → 予約値チェック → nranks チェックの優先順位**を 1 箇所で定義し、エラーメッセージを一意化。

### L3. Makefile
`clean` への `src/*.mpi.o` / `dqmc_mpi` 追加、`test_mpi` のパターンルール（`tests/test_%_mpi`）が設計の Makefile 節に無い。実装順 1 で一緒に。

## 実機確認した事実

- `ReplicaBin` は double×6 + `int count`（`src/replica.h:6-14`）。pack 仕様と一致。
- seed 式は `src/replica.c:16-33` で設計どおり（id=0 legacy、id>0 splitmix64、衝突時再 mix）。
- profiler の `g_current_profiler` は既に `_Thread_local`（`src/profiler.c:8`）。MPI では各 rank 1 本で問題なし。
- 現 `main()` の rank0 限定 `return 1` は startup（profiler/replica_log open）と β内（alloc 失敗・zero-sign bin）に分布。deterministic な入力エラー（unknown lattice / non-bipartite / beta が dtau の非整数倍）は全 rank 同一分岐のため hang しないが、stderr 出力は rank0 のみへガードが必要。
