---
date: 2026-06-25
datetime: 2026-06-25 14:20 JST
model: Codex (GPT-5)
summary: |
  DQMC 実行時間 profiler/timer の設計。
  通常実行では無効、入力 `profile=1` で有効化し、beta ごと・phase ごと・region ごとの CSV を出力する。
  QMC の意味単位と線形代数ホットスポットを手動 instrumentation し、後処理・plot しやすい計測データを得る。
---

# DQMC Profiling Timer Design

## 目的

DQMC 実行のどこに時間がかかっているかを、温度点ごとに後処理できる形で記録する。

特に知りたいのは、`beta` が大きくなり Trotter slice 数 `Ltr=beta/dtau` が増えたときに、どの処理が支配的になるかである。全体時間だけではなく、warmup、measurement、Green 関数再構築、wrapping、rank-1 update、測定、UDV/LAPACK まわりの内訳を見る。

## 基本方針

- 通常実行では profiler は無効にする。
- 入力ファイルの `profile=1` で有効化する。
- 物理量の stdout 出力は変更しない。
- profiling 結果は CSV ファイルへ出力する。
- 計測は外部 profiler 任せではなく、QMC 的に意味のある region に手動 instrumentation する。
- region の時間は inclusive time とする。親 region と子 region を単純合計すると `beta_total` を超える場合がある。

## 入力

`Params` に次を追加する。

```c
int profile;
char profile_file[256];
```

入力例:

```text
profile=1
profile_file=data/profiles/L6_U4_dtau0.1_profile.csv
```

仕様:

- `profile=0`: デフォルト。profiler は無効。
- `profile=1`: profiler を有効化。
- `profile_file` 未指定かつ `profile=1`: `profile.csv` を使う。
- `profile_file` は `profile=0` のとき無視する。
- 未知 key、非数値 `profile`、`profile` が 0/1 以外の場合は既存 parser 方針に合わせてエラーにする。

## 出力

CSV header:

```csv
beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta
```

列の意味:

- `beta`: 入力 beta。
- `T`: `1/(Ltr*dtau)`。
- `dtau`: Trotter step。
- `Ltr`: Trotter slice 数。
- `phase`: `setup`, `warmup`, `measurement`, `finalize`, `all`。
- `region`: 計測対象名。
- `calls`: region の呼び出し回数。
- `total_sec`: region の inclusive total wall time。
- `avg_sec`: `total_sec/calls`。`calls=0` の場合は `0`。
- `frac_beta`: `total_sec/beta_total_sec`。nested timer のため、複数 region の `frac_beta` 合計は 1 にならない。

出力例:

```csv
beta,T,dtau,Ltr,phase,region,calls,total_sec,avg_sec,frac_beta
1,1,0.1,10,warmup,dqmc_sweep,1000,0.123456,0.000123456,0.42
1,1,0.1,10,warmup,green_wrap,20000,0.030000,0.0000015,0.10
1,1,0.1,10,measurement,measure_sample,10000,0.020000,0.000002,0.07
```

## Architecture

`src/profiler.h` と `src/profiler.c` を追加する。

主要 API:

```c
typedef enum {
    PROF_PHASE_SETUP,
    PROF_PHASE_WARMUP,
    PROF_PHASE_MEASUREMENT,
    PROF_PHASE_FINALIZE,
    PROF_PHASE_ALL
} ProfPhase;

typedef enum {
    PROF_BETA_TOTAL,
    PROF_MODEL_INIT,
    PROF_FIELD_INIT,
    PROF_DQMC_INIT,
    PROF_DQMC_SWEEP,
    PROF_GREEN_FROM_SCRATCH,
    PROF_GREEN_WRAP,
    PROF_GREEN_UPDATE,
    PROF_MEASURE_SAMPLE,
    PROF_JACKKNIFE,
    PROF_UDV_LMUL,
    PROF_UDV_INV_ONE_PLUS,
    PROF_LA_GEMM,
    PROF_LA_INVERSE,
    PROF_LA_EXPM_SYM,
    PROF_REGION_COUNT
} ProfRegion;

typedef struct {
    int enabled;
    FILE *fp;
    /* beta metadata, current phase, counters, timings */
} Profiler;

void profiler_init(Profiler *p, int enabled, const char *path);
void profiler_close(Profiler *p);
void profiler_beta_begin(Profiler *p, double beta, double T, double dtau, int Ltr);
void profiler_beta_end(Profiler *p);
void profiler_phase_set(Profiler *p, ProfPhase phase);
double profiler_now(void);
void profiler_add(Profiler *p, ProfRegion region, double elapsed_sec);
```

Hot path では begin/end 関数呼び出しを増やしすぎないよう、macro を用意する。

```c
#define PROF_BEGIN(prof, var) double var = profiler_now_if_enabled(prof)
#define PROF_END(prof, region, var) profiler_add_elapsed(prof, region, var)
```

`PROF_BEGIN` は profiler 無効時に `0.0` を返し、`PROF_END` は profiler 無効時に即 return する。C の portable な cleanup attribute には頼らず、明示的な begin/end macro だけを使う。

通常実行では `enabled=0` の分岐で即 return し、計測用ファイルは開かない。

## Profiler の渡し方

`main.c` で `Profiler prof` を作り、`Dqmc` に pointer を持たせる。

```c
typedef struct {
    int n;
    int L;
    Model *m;
    Field *f;
    Green Gu;
    Green Gd;
    Rng *rng;
    int stab_interval;
    double sign;
    Profiler *prof;
} Dqmc;
```

`dqmc_init` に `Profiler *prof` を渡す。`prof=NULL` または `prof->enabled=0` の場合は無効扱いにする。

下位の `green.c` と `linalg.c` では、引数を大きく増やさないため、`profiler_current()` で現在の profiler を参照する。`main` が beta 実行前に `profiler_set_current(&prof)`、終了時に `profiler_set_current(NULL)` する。

この折衷により、`dqmc_sweep` など QMC 上位層は明示的に profiler を持ち、線形代数の深い関数は既存 API を壊さず計測できる。

## Instrumentation Placement

`main.c`:

- beta 全体: `PROF_BETA_TOTAL`
- `model_init`: `PROF_MODEL_INIT`
- `field_init`: `PROF_FIELD_INIT`
- `dqmc_init`: `PROF_DQMC_INIT`
- warmup loop: phase `warmup`
- measurement loop: phase `measurement`
- `measure_sample`: `PROF_MEASURE_SAMPLE`
- `jackknife`: `PROF_JACKKNIFE`
- cleanup: phase `finalize`

`dqmc.c`:

- `dqmc_sweep`: `PROF_DQMC_SWEEP`
- accepted flip 時の `green_update`: `green_update` 側で `PROF_GREEN_UPDATE`
- slice wrapping: `green_wrap` 側で `PROF_GREEN_WRAP`
- periodic stabilization と sweep 終端再安定化: `green_from_scratch` 側で `PROF_GREEN_FROM_SCRATCH`

`green.c`:

- `green_from_scratch`: `PROF_GREEN_FROM_SCRATCH`
- `green_wrap`: `PROF_GREEN_WRAP`
- `green_update`: `PROF_GREEN_UPDATE`

`linalg.c`:

- `la_gemm`: `PROF_LA_GEMM`
- `la_inverse`: `PROF_LA_INVERSE`
- `la_expm_sym`: `PROF_LA_EXPM_SYM`
- `udv_lmul`: `PROF_UDV_LMUL`
- `udv_inv_one_plus`: `PROF_UDV_INV_ONE_PLUS`

`green_build_B` は最初は region にしない。必要なら次の改善で追加する。まずは重い UDV/LAPACK と QMC 操作単位を優先する。

## Phase Handling

profiler は現在 phase を 1 つ持つ。

- beta setup 中は `setup`
- warmup loop 中は `warmup`
- measurement loop 中は `measurement`
- jackknife と出力直前の集計は `finalize`

`profiler_add` は現在 phase の counter に加算する。同じ region について phase 別 counter と `all` counter の両方を更新する。

## Error Handling

- `profile=1` で `profile_file` を開けない場合は実行エラーにする。
- CSV 書き込み失敗は `profiler_close` または `profiler_beta_end` で検出できる範囲でエラー扱いにする。
- profiler のエラーで物理計算を silent に続けない。計測を頼んでいる run では失敗を明示する。
- `profile=0` のときは profiler file I/O を一切行わない。

## Tests

追加テスト:

1. `tests/test_io.c`
   - `profile=1` と `profile_file=/tmp/afqmc_profile.csv` が parse される。
   - `profile` default は 0。
   - `profile=2` や `profile=foo` はエラー。

2. `tests/test_profiler.c`
   - `profile=0` では file を作らない、または header を出さない。
   - `profile=1` では CSV header が出る。
   - `profiler_add` 後に `calls` と `total_sec` が出力される。
   - beta metadata と phase/region 名が CSV に出る。

3. 既存統合テスト
   - `make test` が通る。
   - 既存 input は `profile` 未指定でも動く。

手動確認:

```sh
make test
make dqmc
./dqmc input/1d_L4_U4.txt > /tmp/qmc.out
```

profile 有効 input を作り、CSV が生成され、stdout の物理量列が変わらないことを確認する。

## 非目標

- thread safety は扱わない。
- flamegraph/call graph は作らない。
- 全関数を自動 tracing しない。
- stdout に profiler summary を混ぜない。
- 外部 profiler の代替を完全に目指さない。必要なら Instruments/perf と併用する。

## 実装リスク

- `la_gemm` の呼び出し回数が多いため、`profile=1` では計測オーバーヘッドが見える可能性がある。
- nested inclusive timer なので、CSV の `frac_beta` を積み上げグラフとして解釈すると誤る。解析時は region ごとに見る。
- global current profiler は単純だが thread unsafe。現状の single-thread C 実装では問題にしない。
- `dqmc_init` の signature 変更は既存テストを広く更新する必要がある。更新漏れを `make test` で検出する。
