---
date: 2026-06-25
datetime: 2026-06-25 18:55 JST
model: Codex (GPT-5)
summary: |
  DQMC replica 並列化の MPI 拡張設計。
  `parallel=mpi` では global replica id を MPI rank に分配し、rank 0 が bin・status・profiler を集約する。
  seed、bin 順序、stdout、replica_log を rank 数に依存させず、将来の hybrid へ自然に拡張できる境界を固定する。
  設計レビューを反映し、rank0 限定 failure の collective exit、status 意味、Gatherv 順序保証、Makefile 詳細を明記した。
---

# DQMC MPI Replica Parallelization Design

## 目的

OpenMP 版で実装済みの replica 並列を MPI rank 間へ拡張する。

初期 MPI 実装では、物理アルゴリズムは一切変えず、同一温度点の独立 Markov chain replica を rank に分配する。各 rank は担当する global `replica_id` のみを計算し、rank 0 が全 replica の bin と metadata を global id 順に集約して、既存と同じ jackknife と stdout 出力を行う。

## 非目的

- `dqmc_sweep()` 内の site loop / slice loop を MPI で分解しない。
- Green 関数、UDV、LAPACK/BLAS kernel の分散メモリ化はしない。
- replica exchange、tempering、autocorrelation 推定は入れない。
- 初期 MPI 実装では `parallel=hybrid` をまだ実行可能にしない。hybrid は MPI replica 分散の上に rank-local OpenMP replica loop を足す次段階とする。
- 複数温度点を rank に分配しない。温度点ごとに全 rank が同期して replica を分担する。

## 基本方針

既存の単一 replica 実行単位はそのまま使う。

```text
dqmc_run_replica(params, lattice, beta_index, Ltr, mu,
                 replica_id, seed, local_profiler, result)
```

MPI 版の beta loop は次の流れにする。

```text
MPI_Init
all ranks read same input and lattice
rank 0 opens profiler/replica_log outputs and writes rank0-only files
all ranks share setup failure by allreduce; if failed, finalize together

for beta:
    all ranks generate the same global seed table
    assign contiguous global replica ids to each rank
    each rank runs its local replicas
    gather failed status to rank 0
    rank 0 writes replica_log rows in global replica_id order
    all ranks share replica/log failure; if failed, skip later collectives and exit together
    gather ReplicaBin arrays to rank 0
    reduce profiler stats to rank 0
    rank 0 combines nrep * nbin bins, jackknifes, prints one data row
    all ranks share rank0 post-gather failure; if failed, exit together before next beta

MPI_Finalize
```

rank 0 以外は、物理量 stdout、`replica_log`、`profile_file`、`hopping_used.txt` を書かない。

## 入力仕様

既存 parser は `parallel=mpi` を予約値として受け付けている。MPI 有効 build では `parallel=mpi` を実行可能にする。

```text
parallel=mpi
nrep=16
replica_log=data/replicas/L6_U4_beta4_mpi_replicas.csv
profile=1
profile_file=data/profiling_runs/L6_U4_beta4_mpi_profile.csv
```

仕様:

- `nrep` は全 MPI communicator での global replica 数。
- `nwarm`, `nmeas`, `nbin` は replica あたりの値を維持する。
- 合成後の bin 数は `nrep * nbin`。
- `parallel=mpi` は MPI 有効 build のみで使用可能。
- 通常 serial build の `parallel=mpi` は従来どおり明示エラー。
- MPI build でも `parallel=serial` は rank 0 のみで実行するか、`mpirun -np 1` を要求する。初期実装では誤用を避けるため、`parallel=serial|omp` を `nranks > 1` で指定した場合は rank 0 で明示エラーにする。
- `parallel=hybrid` は MPI build でも v1 では予約値として明示エラー。

mode validation の優先順位は次に固定する。

1. parser で `parallel` の語彙を検査する。
2. `parallel=hybrid` は予約 mode として停止する。
3. build capability を検査する。`parallel=omp` で OpenMP 無効なら `ERROR: parallel=omp requires OpenMP-enabled build`、`parallel=mpi` で MPI 無効なら `ERROR: parallel=mpi requires MPI-enabled build`。
4. MPI communicator size を検査する。MPI build で `nranks > 1` かつ `parallel=serial|omp` なら `ERROR: parallel=serial under MPI requires mpirun -np 1` または `ERROR: parallel=omp under MPI requires mpirun -np 1`。

この順序により、MPI-only build で `parallel=omp` を指定した場合は rank 数に関係なく OpenMP capability error を優先する。

エラー例:

```text
ERROR: parallel=mpi requires MPI-enabled build
ERROR: parallel=serial under MPI requires mpirun -np 1
ERROR: parallel=omp under MPI requires mpirun -np 1
ERROR: parallel=hybrid is reserved for future hybrid implementation
```

## Build

MPI は optional build target として追加する。

```text
make dqmc
make dqmc_omp
make dqmc_mpi
make test
make test_omp
make test_mpi
```

Makefile 方針:

```make
MPICC ?= mpicc
MPIRUN ?= mpirun
MPI_CFLAGS = -DAFQMC_USE_MPI
MPI_OBJ = $(SRC:.c=.mpi.o)
TESTBIN_MPI = $(patsubst tests/test_%.c,tests/test_%_mpi,$(TESTS))

dqmc_mpi: $(MPI_OBJ)
        $(MPICC) $(CFLAGS) $(MPI_CFLAGS) -o $@ $(MPI_OBJ) $(LDLIBS)

tests/test_%_mpi: tests/test_%.c $(LIBSRC_MPI)
        $(MPICC) $(CFLAGS) $(MPI_CFLAGS) -o $@ $< $(LIBSRC_MPI) $(LDLIBS)

test_mpi: $(TESTBIN_MPI)
        $(MPIRUN) -np 1 tests/test_replica_mpi

clean:
        rm -f src/*.mpi.o dqmc_mpi $(TESTBIN_MPI)
```

OpenMP と同様に、MPI object は serial object と分ける。

```text
src/*.mpi.o
```

serial build/test は MPI の有無に依存させない。MPI が入っていない環境でも `make test` と `make test_omp` は壊さない。

## Rank Assignment

global replica id は beta 内で `0..nrep-1` とする。rank 数が変わっても、同じ global id は同じ seed と同じ出力位置を持つ。

担当範囲は contiguous block 分割にする。

```c
base = nrep / nranks;
rem  = nrep % nranks;
local_nrep = base + (rank < rem ? 1 : 0);
first_replica = rank * base + min(rank, rem);
```

例:

```text
nrep=10, nranks=3
rank 0: ids 0,1,2,3
rank 1: ids 4,5,6
rank 2: ids 7,8,9
```

この分割では負荷差は高々 1 replica である。`MPI_Gatherv` の `recvcounts[rank]` と `displs[rank]` を `mpi_rank_range()` の `first_replica` と `local_nrep` から計算することで、gather 後の buffer 位置は global replica id 順に by construction で厳密一致する。

```c
recvcounts[rank] = local_nrep[rank] * nbin * 6;
displs[rank] = first_replica[rank] * nbin * 6;
```

`nrep < nranks` の場合、一部 rank は `local_nrep=0` で collective のみに参加する。

## Seed と再現性

seed 生成式は現行のまま固定する。

```text
replica_id=0: base_seed + 1000 * beta_index
replica_id>0: splitmix64 系 mixer(base_seed, beta_index, replica_id)
```

全 rank は各 beta で同じ global seed table を生成し、重複チェックする。seed は rank assignment に依存しない。

要件:

- `parallel=mpi`, `nrep=1`, `nranks=1` は legacy single-chain と同じ乱数列を使う。
- `parallel=mpi`, `nrep=N` は `nranks=1,2,4,...` を変えても同じ global seed table を使う。
- bin 結合順が global id 順なら、BLAS thread 数 1 の条件で serial/OpenMP/MPI の物理量行は bitwise 一致する。

## Data Gather

`ReplicaBin` は C struct padding に依存して `MPI_BYTE` 送信しない。portable な wire format として、double 成分と count 成分を分けて gather する。

各 bin の double 成分:

```text
sum_sign_Ehub
sum_sign_Egc
sum_sign_Eph
sum_sign_N
sum_sign_D
sum_sign
```

送信 buffer:

```text
double local_bin_values[local_nrep * nbin * 6]
int    local_bin_counts[local_nrep * nbin]
int    local_failed[local_nrep]
```

`local_failed` は `ReplicaResult.status` 単体ではない。現行 serial/OpenMP 実装の判定と同じく、各 replica について次の合成済み flag を送る。

```c
local_failed[i] = replica_failed[i] || results[i].status != 0;
```

`results[i].status` は「成功で 0、未完了/失敗で非 0」の規約だが、`dqmc_run_replica()` 自体の返り値に由来する `replica_failed` も含める必要がある。

rank 0 は `MPI_Gatherv` で受け取り、global replica id 順に `ReplicaResult` または flat `ReplicaBin` 配列へ復元する。`recvcounts` と `displs` は `first_replica` から計算し、rank order や実行順ではなく global replica id 順を構造的に保証する。

status は bin より先に gather する。どれかの replica が失敗した場合、rank 0 は `replica_log` に `error` を記録し、物理量行は出さない。この failed beta では bin gather と profiler reduce を全 rank で揃ってスキップし、fail flag を共有して collective cleanup 後に終了する。

seed は rank 0 でも global id から再生成できるため、gather しない。

## stdout と replica_log

stdout は rank 0 のみが書く。header は MPI 情報を追加する。

```text
# lattice=chain n=6 U=4 mu=2 dtau=0.1 bipartite=1 parallel=mpi nrep=16 bins=1600 nranks=4
# T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign
```

`replica_log` も rank 0 のみが書く。行順は必ず global `replica_id` 昇順にする。

```csv
beta,T,replica_id,seed,nwarm,nmeas,nbin,status,rank
4,0.25,0,246813579,1000,20000,100,ok,0
4,0.25,1,14012418791647386686,1000,20000,100,ok,0
4,0.25,2,11473674334183429640,1000,20000,100,ok,1
```

既存 OpenMP 版の header prefix を保つため、MPI 専用の `rank` 列は末尾に追加する。parser 側の下流 script は header 名で読むことを推奨する。

CSV schema policy:

- 初期 MPI 実装では、既存 serial/OpenMP build の `replica_log` 8 列と profiler 14 列は変更しない。
- MPI build の `parallel=mpi` 実行時だけ、MPI metadata を末尾列として追加する。
- 下流 script は列数固定ではなく header 名で読む。既存列の prefix は維持する。
- 将来、全 build で `rank=0` / `nranks=1` を常時出す統一 schema へ移行する場合は、別の互換性変更として扱う。

## Profiler

各 replica は現行 OpenMP 版と同じく local `Profiler` に計測を入れる。rank 内で担当 replica の profiler を merge し、rank local の `Profiler` stat を作る。

MPI 集約は rank local stat に対する `MPI_Reduce(SUM)` とする。

集約対象:

```text
calls[PROF_PHASE_COUNT][PROF_REGION_COUNT]      unsigned long long
total_sec[PROF_PHASE_COUNT][PROF_REGION_COUNT]  double
```

rank 0 は reduced stat を file-backed `Profiler` に入れて `profiler_beta_end()` で CSV を出す。

`beta_total` は rank 0 の wall time とし、現行 OpenMP 版と同じく `thread_total_sec == wall_sec` とする。他 region の `thread_total_sec` は全 rank・全 replica の合算なので、`frac_beta > 1` になり得る。

CSV は既存列の末尾に MPI metadata を追加する。

```csv
beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta,wall_sec,thread_total_sec,nrep,parallel,nranks
```

列の意味:

- `nrep`: global replica 数。
- `parallel`: `mpi`。
- `nranks`: communicator size。
- `wall_sec`: rank 0 で測った beta wall time。region 別 wall が定義されない行では 0。
- `thread_total_sec`: rank local replica stats を全 rank で合計した時間。

profile file は rank 0 のみが open/write/close する。rank ごとの詳細 CSV は初期実装では出さない。

replica failure、`replica_log` write failure、または rank0 post-gather failure が起きた beta では、profiler CSV 行は出さない。失敗 beta で一部 rank だけが profiler reduce に参加すると hang の原因になるため、失敗が共有された時点で全 rank が bin gather と profiler reduce を揃ってスキップし、collective cleanup 後に終了する。正常 beta の profiler 出力だけを保証する。

## MPI Initialization and Error Handling

MPI build では `MPI_Init` を `main()` の先頭近くで呼び、`argc/argv` を渡す。すべての return path は `MPI_Finalize` を通る。

設計上の原則:

- rank 0 だけが error message を出す。ただし rank 固有の読込失敗など、原因が rank 依存の場合は `rank=N` を付けて stderr に出してよい。
- ある rank だけが早期 return して他 rank が collective で hang しないよう、MPI path では fail flag を `MPI_Allreduce(MAX)` で共有する。
- unrecoverable な collective 不整合や allocation failure では `MPI_Abort` を許容するが、通常の入力エラーや replica failure は collective cleanup 後に `MPI_Finalize` して exit code 1 を返す。
- `replica_log` write error や `profile_file` open/write/close error は rank 0 が検出し、fail flag を broadcast/allreduce して全 rank が終了する。
- beta loop 内では、次の collective に入る前に必ず fail flag を `MPI_Allreduce(MAX)` する。rank0 限定 failure は bare `return` せず、全 rank が同じ終了経路に入る。

rank0 限定 failure と collective の関係:

| 場所 | 例 | 必須処理 |
|---|---|---|
| startup / beta loop 前 | rank0 の `profiler_init()` / `replica_log` open / `hopping_used.txt` write failure | setup fail flag を allreduce し、全 rank が beta loop に入らず finalize |
| local replica 実行後 | `dqmc_run_replica()` failure, result status failure | `local_failed` を gather、rank0 が log を書き、fail flag を共有して bin gather/profiler reduce を全 rank でスキップ |
| bin gather 後 | rank0 の `Ehub/Egc/Eph/N/D/sign` arrays allocation failure | rank0 final fail flag を allreduce し、次 beta に入る前に全 rank が finalize |
| bin ratio 化 | rank0 の `replica_bin_values()` zero-sign failure | rank0 final fail flag を allreduce し、次 beta に入る前に全 rank が finalize |
| output close | rank0 の `replica_log` / `profile_file` close failure | close fail flag を allreduce し、全 rank が同じ exit code で finalize |

deterministic な入力エラー（unknown lattice、non-bipartite、`beta/dtau` 非整数など）は全 rank が同じ分岐に入る想定だが、stderr は rank0 のみに制限する。将来、rank0 read + broadcast へ変える場合もこの invariant を維持する。

代表的な helper:

```c
int mpi_any_failed(int local_failed, MPI_Comm comm);
void mpi_rank_range(int nrep, int nranks, int rank,
                    int *first_replica, int *local_nrep);
int mpi_gather_replica_bins(...);
int mpi_reduce_profiler(...);
```

## File I/O

rank 0 のみが書くファイル:

- stdout
- `replica_log`
- `profile_file`
- `hopping_used.txt`

全 rank が読むファイル:

- input file
- `latfile` if `lattice=file`

初期実装では全 rank が同じ shared filesystem 上の input/latfile を読む。将来、rank 0 read + broadcast に置き換えることは可能だが、まずは実装を単純に保つ。

## Hybrid への接続

MPI 実装後の `parallel=hybrid` は、同じ rank assignment の中で rank-local replica loop を OpenMP 化するだけでよい。

```text
global replicas -> MPI contiguous block assignment
rank-local replicas -> OpenMP static loop
rank local stats -> merge
rank stats -> MPI_Reduce to rank 0
```

このため、MPI 実装では次を守る。

- seed は global replica id のみから決める。
- rank-local execution order に物理出力を依存させない。
- gather 後の bin order は global replica id 昇順に固定する。
- profiler は replica local stats を first-class に保つ。

## テスト計画

Unit tests:

- `mpi_rank_range(nrep,nranks,rank)` が contiguous block 分割を返す。
- `nrep < nranks`, `nrep % nranks != 0`, `nrep % nranks == 0` を検証する。
- seed table は rank assignment に依存しない。
- pack/unpack した `ReplicaBin` が元の値に戻る。
- `MPI_Gatherv` 用の `recvcounts/displs` が `first_replica * nbin * 6` に基づき global replica id 順を厳密に作る。
- `local_failed = replica_failed || result.status != 0` の規約を helper test で固定する。

Build tests:

- `make test` は MPI 無しで従来どおり通る。
- `make dqmc_mpi` が `mpicc` で build できる。
- `make test_mpi` が MPI helper unit tests と small smoke を実行する。
- `make clean` が `src/*.mpi.o`, `dqmc_mpi`, `tests/test_*_mpi` を削除する。

Runtime tests:

- serial build の `parallel=mpi` は `ERROR: parallel=mpi requires MPI-enabled build` で停止する。
- MPI build の `parallel=hybrid` は予約 mode として停止する。
- MPI-only build の `parallel=omp` は OpenMP capability error を優先する。
- MPI build の `nranks>1` で `parallel=serial` は明示エラーで collective hang しない。
- `mpirun -np 1 ./dqmc_mpi` の `parallel=mpi,nrep=1` が legacy `./dqmc` の data row と一致する。
- `nrep=4` で `mpirun -np 1/2/4 ./dqmc_mpi` の data row が一致する。
- `nrep=5, nranks=2/3` のように割り切れない分割でも data row が serial `parallel=serial,nrep=5` と一致する。
- `nrep=2, nranks=4` で idle rank があっても hang しない。
- 複数 beta の run で各 beta ごとに gather/log/profile が正しく出る。
- `replica_log` は header + `nbeta * nrep` rows を持ち、`replica_id` は各 beta で `0..nrep-1` 昇順、rank 列は assignment と一致する。
- profiler CSV は `parallel=mpi`, `nrep`, `nranks` を含み、`dqmc_sweep`, `green_from_scratch`, `udv_lmul` が集約される。
- startup の rank0-only open failure、rank0-only measurement-bin allocation failure、zero-sign bin failure を小さな fault injection で起こし、全 rank が timeout なしに同じ exit code で終了することを確認する。
- replica failure beta では `replica_log` に `error` を出し、物理量 data row と profiler row は出さないことを確認する。

Performance smoke:

- `L=6, U=4, beta=4, dtau=0.1, nrep=4` で `np=1/2/4` の wall time を比較する。
- BLAS oversubscription を避けるため、検証時は `VECLIB_MAXIMUM_THREADS=1 OPENBLAS_NUM_THREADS=1` を設定する。

## 実装順序

1. Makefile に optional MPI object / `dqmc_mpi` / `test_mpi` target と clean 対象を追加する。
2. `mpi_rank_range()` と pack/unpack helper を追加し、unit test を書く。
3. MPI build の `main()` 初期化・rank 0 I/O guard・mode validation priority・`parallel=mpi` guard を追加する。serial build の挙動は維持する。
4. rank-local replica 実行を実装する。まず `nranks=1` で legacy/serial と一致させる。
5. `local_failed = replica_failed || result.status != 0` の status gather と `replica_log` rank 0 出力を実装する。
6. bin gather の `recvcounts/displs` を `first_replica` 由来にし、rank 0 jackknife を実装して `nranks=2/4` で serial `nrep` と一致させる。
7. profiler stat reduce と `nranks` CSV metadata を追加する。failed beta では profiler row を出さない仕様を実装する。
8. startup / post-gather allocation / zero-sign / close error path を audit し、collective hang がないことを small failure case で確認する。
9. L6 U4 の short scaling run を `data/profiling_runs/` に保存し、`LOG.md` に記録する。

## リスクと対策

- **collective hang**: rank 固有 failure の後に片方だけ return すると hang する。MPI path では fail flag allreduce と共通 cleanup を徹底する。
- **rank0-only failure**: startup open、post-gather allocation、zero-sign bin、file close は rank0 だけで検出される。次 beta や finalize 前に fail flag を全 rank で共有する。
- **file race**: `hopping_used.txt`, profile CSV, replica log を全 rank が書くと破損する。rank 0 I/O に限定する。
- **struct padding**: `ReplicaBin` を `MPI_BYTE` 送信すると compiler/ABI 依存になる。double arrays + int counts に pack する。
- **rank 数依存の乱数列**: rank-local id から seed を作ると scaling 比較不能になる。global replica id から seed を作る。
- **bin 順序依存の丸め差**: gather 後は global replica id 昇順、bin id 昇順で ratio/jackknife する。
- **BLAS oversubscription**: MPI rank 数と BLAS thread が掛け算になる。検証・推奨運用では BLAS thread を 1 に固定する。
- **profile 解釈**: `beta_total` は wall、他 region は rank/replica 合算 thread time。CSV header と LOG で明示する。

## レビュー反映履歴

- 2026-06-26: `docs/reviews/2026-06-25-dqmc-mpi-replica-parallelization-design-review.md` の High 3 / Medium 3 を反映した。rank0 限定 failure の allreduce、startup open failure、measurement-bin allocation failure、zero-sign bin failure、合成済み `local_failed`、Gatherv `displs` の global id 順保証、failed beta の profiler skip を明記した。
- 2026-06-26: Low 指摘を反映し、CSV schema は MPI 専用列を末尾追加して既存 serial/OpenMP schema を維持する方針、mode validation priority、Makefile clean / `test_mpi` target を追記した。
