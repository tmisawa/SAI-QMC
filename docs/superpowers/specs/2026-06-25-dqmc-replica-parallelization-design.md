---
date: 2026-06-25
datetime: 2026-06-25 16:30 JST
model: Codex (GPT-5)
summary: |
  DQMC の replica 並列化設計。
  初期実装は同一温度・独立 seed の OpenMP replica 並列を対象にする。
  MPI/hybrid へ自然に拡張できるよう、replica 実行・bin accumulator・seed 出力・profiler 集約の境界を固定する。
  設計レビューを反映し、profiler current のスレッドローカル化と `nrep=1` の旧 seed 互換を明記した。
---

# DQMC Replica Parallelization Design

## 目的

同一温度点の統計計算を、独立 Markov chain replica で並列化する。

`dqmc_sweep()` 内の Trotter slice/site 更新は直前の Green 関数、補助場、乱数受理に依存するため、ここは並列化しない。初期実装では、物理アルゴリズムを変えずに安全に高速化できる replica 並列を OpenMP で実装する。

将来の MPI/hybrid 実装では、同じ replica 実行単位と bin accumulator を MPI rank 間で gather/reduce できる形にする。

## 非目的

- `dqmc_sweep()` の site loop や slice loop の直接 OpenMP 化はしない。
- Green 関数更新、UDV/QR、LAPACK kernel のアルゴリズム変更はしない。
- v1 では `parallel=mpi` と `parallel=hybrid` を実行可能にはしない。
- replica exchange、tempering、autocorrelation 推定は入れない。

## 基本方針

既存 DQMC コアは単一 replica 実行として維持する。

```text
dqmc_sweep()
green_*()
measure_sample()
```

新しく上位層に replica 実行関数を置く。

```text
run_replica(params, lattice, beta_index, replica_id, replica_seed)
    -> ReplicaResult { bins[nbin], metadata/status, profiler stats }
```

`main.c` の beta loop は、各 beta について `nrep` 個の replica を走らせ、返ってきた bin accumulator を連結して最終統計を出力する。

```text
for beta:
    run nrep replicas serial or omp
    combine nrep * nbin bins
    jackknife
    print one stdout data row
```

`parallel=serial` では replica loop を逐次実行する。`parallel=omp` では replica loop だけを OpenMP で並列化する。`parallel=mpi` と `parallel=hybrid` は parser では予約値として認識するが、v1 では明示エラーで止める。

## 入力仕様

`Params` に次を追加する。

```c
char parallel[16];
int nrep;
char replica_log[256];
```

入力例:

```text
parallel=omp
nrep=8
replica_log=data/replicas/L6_U4_beta4_replicas.csv
```

仕様:

- `parallel=serial`: 常に使用可能。
- `parallel=omp`: OpenMP 有効ビルドで使用可能。
- `parallel=mpi`: v1 では予約値。実行時に明示エラー。
- `parallel=hybrid`: v1 では予約値。実行時に明示エラー。
- `parallel` 既定値: `serial`。
- `nrep` 既定値: `1`。
- `nrep >= 1` を要求する。
- `nrep > 1` では replica seed の記録を既定で有効にする。
- `replica_log` 未指定かつ `nrep > 1`: `replicas.csv` に出力する。
- `replica_log` 未指定かつ `nrep == 1`: replica log は出力しない。
- `replica_log=none`: replica log を抑止する。
- 未知 `parallel`、非整数 `nrep`、`nrep < 1` は parser error にする。
- `parallel=omp` を OpenMP 無効ビルドで指定した場合は silent fallback せず明示エラーにする。

エラー例:

```text
ERROR: parallel=omp requires OpenMP-enabled build
ERROR: parallel=mpi is reserved for future MPI implementation
ERROR: parallel=hybrid is reserved for future MPI implementation
```

## 測定数と bin の意味

`nwarm`、`nmeas`、`nbin` は replica あたりの値とする。

```text
nrep=4
nwarm=5000
nmeas=100000
nbin=100
```

この入力では、各 replica が `5000` warmup sweep、`100000` measurement sweep、`100` bin を持つ。合計測定数は `400000`、合成後の bin 数は `400` である。

`nmeas % nbin == 0` の既存制約は replica あたりに適用する。

## 統計データ構造

replica ごとの平均値を再平均しない。各 bin は sign 付き numerator と denominator を保持する。

```c
typedef struct {
    double sum_sign_Ehub;
    double sum_sign_Egc;
    double sum_sign_Eph;
    double sum_sign_N;
    double sum_sign_D;
    double sum_sign;
    int count;
} ReplicaBin;
```

各 replica は `nbin` 個の `ReplicaBin` を返す。最終段では `nrep * nbin` 個の bin を連結し、各 bin で ratio を作ってから既存の `jackknife()` に渡す。

```text
Ehub_bin   = sum_sign_Ehub / sum_sign
Egc_bin    = sum_sign_Egc  / sum_sign
Eph_bin    = sum_sign_Eph  / sum_sign
N_bin      = sum_sign_N    / sum_sign
D_bin      = sum_sign_D    / sum_sign
sign_bin   = sum_sign      / count
```

半充填の current scope では sign は基本 1 だが、非半充填や符号問題を将来扱うため、常に numerator と denominator を分けて保持する。

`sum_sign == 0` の bin は、現行 scope では発生しない想定だが、実装では fail-fast する。黙って 0 や NaN を出力しない。

## Seed 設計

既存 run の再現性を保つため、`replica_id=0` は現行の seed 式を維持する。

```text
legacy_seed(base_seed, beta_index) = base_seed + 1000 * beta_index
replica_seed(beta_index, 0)        = legacy_seed(base_seed, beta_index)
```

これにより、`parallel=serial`, `nrep=1` は従来の single-chain run と同じ乱数列を使い、既存の比較データとビット再現しやすい。

`replica_id > 0` は、`base_seed`、`beta_index`、`replica_id` を混ぜる seed mixer で生成する。

```text
replica_seed(beta_index, replica_id>0)
    = mix_seed(base_seed, beta_index, replica_id)
```

`replica_id` は beta 内の global replica id とする。将来 MPI で rank 分散しても、同じ `nrep`、`base_seed`、`beta_index`、`replica_id` なら同じ seed になるようにする。

seed mixer は splitmix64 系の avalanche を使う。目的は、近い `replica_id` から規則的に近い乱数状態を作らないことである。

各 beta で生成した replica seed は重複チェックする。64-bit mixer で衝突は実用上ほぼ起きないが、衝突した場合は fail-fast し、同一乱数系列の replica を黙って混ぜない。

## Replica Log

`replica_seed` は物理量 stdout に混ぜず、CSV に出力する。

CSV header:

```csv
beta,T,replica_id,seed,nwarm,nmeas,nbin,status
```

例:

```csv
beta,T,replica_id,seed,nwarm,nmeas,nbin,status
4,0.25,0,123456789,5000,100000,100,ok
4,0.25,1,987654321,5000,100000,100,ok
```

`status` は v1 では `ok` または `error` とする。いずれかの replica が失敗した場合、最終物理量を出さずにエラー終了する。

## stdout 出力

物理量の列は現状維持する。既存の ED 比較・plot script が壊れないように、header のみ拡張する。

```text
# lattice=chain n=6 U=4 mu=2 dtau=0.1 bipartite=1 parallel=omp nrep=4 bins=400
# T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign
0.25 ...
```

`bins` は合成後の bin 数、つまり `nrep * nbin` とする。

## Profiler 設計

`parallel=omp` では共有 `Profiler` に複数 thread が直接加算すると data race になる。v1 では replica ごとに local profiler/stat を持たせ、beta 終了時に集約する。

既存の `green.c` と `linalg.c` は `profiler_current()` から current profiler を取得している。この current profiler pointer は通常の global ではなく、C11 `_Thread_local` にする。

```c
static _Thread_local Profiler *g_current_profiler = NULL;
```

各 replica は自分の local `Profiler` を持ち、同じ OpenMP thread 上で DQMC 初期化・warmup・measurement・cleanup を実行する間だけ次を設定する。

```text
profiler_set_current(&replica_profiler)
run replica
profiler_set_current(NULL)
```

worker thread は profile CSV に直接書かない。thread/replica local stats を beta 終了後に replica id 順で merge し、file I/O は serial に行う。

並列実行で必要な時間は 2 種類である。

```text
wall_sec          実際の経過時間。高速化率を見る。
thread_total_sec  各 replica/thread の時間合計。総計算コストと hotspot を見る。
```

既存 `profile_file` の CSV は列を拡張する。

```csv
beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta,wall_sec,thread_total_sec,nrep,parallel
```

列の意味:

- `total_sec`: 後方互換のため、parallel 実行では `thread_total_sec` と同じ値を入れる。
- `avg_sec`: `thread_total_sec / calls`。
- `frac_beta`: `thread_total_sec / wall_beta_sec`。
- `wall_sec`: beta/phase/region の wall timing。region に自然な wall timing がない場合は 0 とする。
- `thread_total_sec`: replica local stats を合算した inclusive time。
- `nrep`: beta で実行した replica 数。
- `parallel`: `serial` または `omp`。

並列実行では `frac_beta` が 1 を超え得る。これは wall time に対する合計 thread time なので正常である。

`profile=0` の hot path overhead は従来どおり抑える。`parallel=omp` でも、無効時に thread local profiler の重い初期化をしない。

## Build

OpenMP は optional にする。

```text
make dqmc
make dqmc_omp
make test
make test_omp
```

既定の `make dqmc` は serial build のままとする。OpenMP 有効 build では compile flag に OpenMP を追加し、`AFQMC_USE_OPENMP` のような feature macro を定義する。

macOS の default `cc` では OpenMP が使えない環境があるため、`dqmc_omp` target は利用可能な compiler/flag を明示する。OpenMP がない環境でも通常 build と test は壊さない。

BLAS/Accelerate/OpenBLAS の内部 thread とは別問題として扱う。v1 の replica 並列では、oversubscription を避けるため、必要に応じてユーザーが BLAS thread 数を 1 に制限する運用を推奨する。

OpenMP correctness や serial/OMP deterministic test では、BLAS の内部 reduction 順を固定するため、`VECLIB_MAXIMUM_THREADS=1` と `OPENBLAS_NUM_THREADS=1` を設定する。これは oversubscription 回避にも必要である。

macOS の Apple clang で OpenMP build を行う場合は、Homebrew libomp を使う想定にする。

```text
cc -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include \
   -L/opt/homebrew/opt/libomp/lib -lomp ...
```

`LIBOMP_PREFIX` のような Makefile 変数で `/opt/homebrew/opt/libomp` を上書きできるようにする。

## Future MPI / Hybrid

MPI 版では `ReplicaBin` 配列を rank 0 に gather し、rank 0 で現在と同じ ratio 化と jackknife を実行する。

```text
rank r:
    run assigned global replica ids
    produce local ReplicaBin array

rank 0:
    gather all ReplicaBin arrays
    combine nrep * nbin bins
    jackknife
    print stdout row
```

hybrid 版では、rank 内で OpenMP replica loop を使い、rank 間は MPI gather にする。

重要な制約:

- seed は global replica id から決まるため、rank 数に依存しない。
- replica ごとの accumulator は POD 風の連続配列にして、MPI gather しやすくする。
- stdout と replica log は rank 0 のみが書く。
- profile CSV は rank local CSV を出すか、rank 0 へ集約するかを MPI 実装時に選ぶ。v1 の OpenMP 設計では、集約可能な stat 構造を保つ。

## Error Handling

- `parallel=omp` かつ OpenMP 無効 build: エラー終了。
- `parallel=mpi|hybrid`: v1 では予約値としてエラー終了。
- replica で memory allocation や DQMC 初期化が失敗した場合: 全体をエラー終了。
- `sum_sign == 0` の bin: エラー終了。
- replica log/profiler file の open/write/close 失敗: エラー終了。
- OpenMP thread 内で直接 `stdout` に書かない。

## テスト計画

- parser: `parallel`, `nrep`, `replica_log` を読める。
- parser: 不正 `parallel`、非整数 `nrep`、`nrep < 1` を拒否する。
- reserved mode: `parallel=mpi` と `parallel=hybrid` は明示エラー。
- OpenMP guard: OpenMP 無効 build の `parallel=omp` は明示エラー。
- serial equivalence: `parallel=serial`, `nrep=1` が旧 seed 式を使い、既存出力と一致する。
- replica statistics: `nrep=2`, `nbin=3` で合成 bin 数が 6 になる。
- seed reproducibility: `replica_id=0` は `base_seed + 1000*beta_index` と一致し、`replica_id>0` は同じ base seed/beta/replica で安定し、近い replica id で重複しない。
- serial replica determinism: `parallel=serial`, `nrep>1` の出力が同じ入力で再現する。
- OpenMP correctness: `parallel=omp`, `nrep>1` が同じ seed 配列の serial replica 実行と一致する。スケジューリングに依存しないよう、集約順を replica id 順に固定し、テスト時は BLAS thread 数を 1 にする。
- replica log: `nrep>1` で seed CSV が出る。`replica_log=none` で出ない。
- profiler current: `_Thread_local` な current profiler により、OpenMP replica ごとの `green.c`/`linalg.c` instrumentation が自 replica の stats に入る。
- profiler CSV: `profile=1` で既存 header prefix を保ちつつ、追加列 `wall_sec`, `thread_total_sec`, `nrep`, `parallel` が出る。既存 `test_profiler.c` は prefix/substring 確認を保ち、新列の存在も追加検証する。
- regression: 既存 `make test` は serial build で全て通る。

## 実装順序

1. 入力 parser に `parallel`, `nrep`, `replica_log` を追加する。
2. `ReplicaBin` / `ReplicaResult` と seed mixer を追加する。
3. 既存 `main.c` の per-beta 実行を `run_replica()` に切り出し、`parallel=serial`, `nrep=1` の既存互換を確認する。
4. `nrep>1` の serial replica 合成を実装する。
5. replica log を追加する。
6. profiler current を `_Thread_local` 化し、serial build の既存 profiler test を通す。
7. OpenMP build target と `parallel=omp` replica loop を追加する。
8. profiler を replica local stats へ対応させ、wall/thread total を出す。
9. 予約 mode と OpenMP guard のエラーを確認する。

この順序なら、物理アルゴリズムを変えずに、統計合成と並列実行の差分を分けて検証できる。
