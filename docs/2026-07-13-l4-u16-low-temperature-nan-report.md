# L=4, U=16 低温 NaN 原因調査レポート

作成日: 2026-07-13 JST

対象: 2D 4x4, U=16, dtau=0.025, half-filled PH symmetric DQMC runs

## 結論

L=4, U=16 の低温行は、PBS/MPI ジョブの異常終了ではなく、長い Monte Carlo trajectory の途中で Green 関数または観測量が非有限化し、その NaN が bin 集計へ入った結果である。

2026-07-13 時点で特定できた直接原因は次の 2 段階。

1. beta=33.325 以降の低温強結合条件で、特定 seed の trajectory が測定後半に非有限値を生成する。
2. `replica_bin_values()` が finite check を持たないため、NaN を含む bin が `status=ok` のまま jackknife へ渡され、最終 `out.dat` が NaN になる。

2026-07-14 に fail-fast finite check と UDV 段階診断を追加した結果、同じ seed では `udv_combine()` のスケール積

```c
C[i + j * n] = l->D[i] * (l->T * r->U)[i + j * n] * r->D[j];
```

が double precision で overflow し、`C[0,0] = -inf` を作ることを確認した。つまり最初に捕まる非有限化は LAPACK 呼び出しそのものではなく、QR 前の UDV combine 行列 `C` の生成である。

## 2026-07-14 follow-up: fail-fast 実装後の再現

実装した検査:

- `measure_sample_is_finite()` を追加し、`MeasSample` の全フィールドを検査。
- `replica_bin_add()` を戻り値付きにし、non-finite sample/sign と派生エネルギーを拒否。
- `replica_bin_values()` で bin sum と出力値の finite check を追加。
- 測定直前に `D.Gu.g`, `D.Gd.g`, `D.sign` を検査し、non-finite Green/sign を replica failure にする。
- jackknife 入力・出力を root 側で検査。
- `udv_inv_one_plus_work()`, `udv_lmul_work()`, `udv_rmul()`, `udv_combine()` に段階診断を追加。
- `LinalgWork.failed` latch を追加し、UDV 更新で非有限値を検出したら次の Green 再構成で nonzero return にする。

同じ再現条件:

```ini
L=4x4, U=16, dtau=0.025, beta=33.325, nwarm=2000,
nmeas=9000, nbin=100, stab=4, seed=14012418791647386686,
sweep_order=alternating
```

まず測定側だけの fail-fast では、従来 NaN が出ていた trajectory が次で停止した。

```text
ERROR: non-finite Green/sign before measurement
beta=33.325000000000003 T=0.030007501875468863
replica=0 seed=14012418791647386686
sweep_count=10397 bin=93 meas=26 sign=1
```

`bin=93`, `meas=26` は 0-based で、warmup 後の測定 sweep 8397 回目に相当する。

UDV 段階診断を追加すると、同じ seed は最初の warmup sweep 中に次で止まる。

```text
ERROR: udv_combine non-finite matrix at stage=C row=0 col=0 value=-inf n=16
ERROR: dqmc warmup numerical breakdown ... sweep_count=0 status=1
```

これは診断を厳しくしたことで、従来は見逃していた UDV combine 中の overflow を最初に捕まえたもの。従来コードはこの非有限中間値を即 failure にせず、後続の Green/observable NaN と bin 集計 NaN として表面化していた。

一方、clean window 側の beta=25 は同 seed の短め smoke (`nwarm=200,nmeas=200,nbin=20`) で finite 完走した。

次の数値改善候補:

- `udv_combine()` で `l->D[i] * TLUR[i,j] * r->D[j]` をそのまま double で作らない。
- 行/列スケールを分離した QR、対数スケール管理、または SVD fallback を検討する。
- 低温強結合での production では、修正までは現行どおり L=4,U=16 は `T >= 0.04` の clean window のみ採用する。

## Production での観測

主対象 run:

- `data/production_runs/kugui_F1cpu_2d4x4_U16_PP_dtau0p025_T0p04_to_0p01_nrep120_n10k_gridT_alt_fc0416a_prebuilt_U12U16_20260707`
- `data/production_runs/kugui_F1cpu_2d4x4_U16_APP_dtau0p025_T0p04_to_0p01_nrep120_n10k_gridT_alt_fc0416a_prebuilt_U12U16_20260707`

条件:

| item | value |
|---|---:|
| Lx, Ly | 4, 4 |
| U | 16 |
| dtau | 0.025 |
| beta list | 25.0, 33.325, 50.0, 100.0 |
| nrep | 120 |
| nwarm | 2000 |
| nmeas | 10000 |
| nbin | 100 |
| sweep order | alternating |

結果:

| beta | T | PP | APP | 判定 |
|---:|---:|---|---|---|
| 25.0 | 0.04 | finite | finite | usable |
| 33.325 | 0.0300075 | NaN | NaN | bad |
| 50.0 | 0.02 | NaN | NaN | bad |
| 100.0 | 0.01 | NaN | NaN | bad |

`replicas.csv` では beta=25, 33.325, 50, 100 の各 120 replicas がすべて `status=ok` だった。したがって、scheduler kill、MPI abort、または replica failure としては検出されていない。

## 比較 run から分かること

### Boundary condition 依存ではない

PP と APP の両方で beta=33.325 から NaN になる。境界条件固有の一点問題ではない。

### stab interval を短くしても直らない

`U=12,16 stab=2 diagnostic` の L=4, U=16 でも beta=33.325, 50, 100 が NaN になった。`stab=4` から `stab=2` へ短縮しても境界は改善していない。

これは単純な wrap drift を安定化頻度だけで抑えればよい、という状況ではないことを示す。`green_from_scratch` / `green_from_stack` から呼ばれる stabilized Green reconstruction 自体の条件が厳しい可能性が高い。

### U beta のしきい値傾向と整合する

L=4 strong-coupling runs では、おおまかに beta U が 400 付近までは finite、さらに低温へ進むと NaN が出る。

| U | last clean beta | last clean U beta | first bad beta | first bad U beta |
|---:|---:|---:|---:|---:|
| 12 | 33.325 | 399.9 | 50.0 | 600.0 |
| 16 | 25.0 | 400.0 | 33.325 | 533.2 |
| 20 | 20.0 | 400.0 | 33.325 | 666.5 |

U=16, dtau=0.025 では beta=33.325 の time slices は 1333。Hubbard-Stratonovich factor の `lambda = acosh(exp(U dtau / 2))` は約 0.653733 なので、`lambda * beta / dtau` は約 871.4 になる。行列積のスケール分離が非常に大きく、double precision の UDV 再構成が厳しい領域に入っている。

## ローカル再現

ローカルの `dqmc` / `dqmc_mpi` を使い、同じ hopping file と seed 系列で再現を行った。

### 短い run では即座に壊れない

beta=33.325, seed=246813579, serial, `nwarm=0,nmeas=2,nbin=2` は finite。`stab_drift_file` でも `failed=0`, `max_inf=4.3e-9` だった。

beta=33.325, serial, `nwarm=2000,nmeas=200,nbin=20` も finite。したがって、初期化直後または最初の数 sweep で必ず壊れるわけではない。

### production と同じ長さで NaN が再現する

ローカル MPI で `mpirun -np 4`, beta=33.325, `nrep=4,nwarm=2000,nmeas=10000,nbin=100` を走らせると NaN を再現した。

比較として、同じ `mpirun -np 4` でも beta=25 は finite。また beta=33.325 でも `nmeas=200` は finite。MPI 集約の一般的なバグではなく、低温長時間 trajectory で NaN が混入している。

### 特定 seed だけが壊れる

上記 `nrep=4` の各 seed を個別 serial で走らせると、seed 1 のみ NaN になった。

| replica id | seed | result |
|---:|---:|---|
| 0 | 246813579 | finite |
| 1 | 14012418791647386686 | NaN |
| 2 | 11473674334183429640 | finite |
| 3 | 4985831835181638333 | finite |

seed `14012418791647386686` について `nmeas` を変えると、`nmeas=8000` までは finite、`nmeas=9000` で NaN になった。つまり warmup 2000 後、測定の 8000 から 9000 sweep の間で非有限値が混入している。

## コード上の NaN 伝播経路

該当コード:

- `src/replica_run.c`
- `src/replica.c`
- `src/measure.c`
- `src/green.c`
- `src/linalg.c`

測定側の流れ:

1. `dqmc_run_replica()` が `dqmc_sweep()` を呼ぶ。
2. `D.status != 0` は確認している。
3. `measure_sample()` で `D.Gu.g` / `D.Gd.g` から observables を作る。
4. `replica_bin_add()` が finite check なしで bin sums へ加算する。
5. `replica_bin_values()` は `count <= 0` と `sum_sign == 0` だけを見る。
6. NaN を含む sum がそのまま jackknife に渡る。

特に `src/replica.c` の `replica_bin_values()` は次のような構造で、`sum_sign_Ehub`, `sum_sign_N`, `sum_sign_D` などの finite check がない。

```c
if (bin->count <= 0 || fabs(bin->sum_sign) == 0.0) {
    return 1;
}

*Ehub = bin->sum_sign_Ehub / bin->sum_sign;
*Egc = bin->sum_sign_Egc / bin->sum_sign;
*Eph = bin->sum_sign_Eph / bin->sum_sign;
*N = bin->sum_sign_N / bin->sum_sign;
*D = bin->sum_sign_D / bin->sum_sign;
*sign = bin->sum_sign / (double)bin->count;
return 0;
```

このため、非有限値は replica failure として検出されない。production の `replicas.csv` が `status=ok` のままなのはこの挙動と整合する。

## 数値破綻の候補箇所

観測量は `measure_sample()` で Green matrix から作られる。Green matrix は安定化再構成で以下の経路を通る。

- `green_from_scratch()`
- `green_from_stack()`
- `green_from_boundary_factors()`
- `udv_inv_one_plus_work()`

`udv_inv_one_plus_work()` では UDV factor から `(1 + P)^{-1}` を作るため、主に以下を行う。

1. `D` を `Db` / `Ds` に分ける。
2. triangular `T` から `Tinv` を作る。
3. `M = Db^{-1} U^T T^{-1} + Ds` を作る。
4. `M` を invert する。
5. `g = T^{-1} M^{-1} Db^{-1} U^T` を作る。

低温強結合では time-slice product のスケール分離が大きく、ここで `Tinv`, `M`, `Minv`, `g` のどこかが非有限化する可能性がある。ただし現行ログには中間行列の finite 状態がないため、最初の発生箇所は未確定。

PH symmetry のため sign は production output でも 1 のまま。今回の NaN は sign denominator がゼロになった問題ではなく、Green/observable の非有限化として扱うべき。

## 現時点の判断

確定:

- L=4, U=16 の beta=33.325 以降は、現在の production 設定では信頼できない。
- L=4, U=16 の usable window は `T >= 0.04` とするのが妥当。
- failure は job termination ではない。
- failure は boundary condition 固有ではない。
- failure は `stab=2` への短縮だけでは改善しない。
- NaN は少なくとも一部 seed の長時間 trajectory で再現する。
- finite check 不足により、NaN を含む replica/bin が `status=ok` として出力される。

未確定:

- 最初に NaN を作る exact operation。
- `g` が先に壊れているのか、`measure_sample()` の observable 演算で初めて壊れるのか。
- UDV 再構成アルゴリズムの改良で beta=33.325 が usable になるか。
- `stab=1`, 別分解、SVD fallback, higher precision で境界がどこまで下がるか。

## Handoff: 次にやるべきこと

### 1. まず finite check を入れて fail-fast にする

目的: NaN を `status=ok` で出さない。これは物理・数値改善より先に必要。

実装候補:

- `measure_sample()` 後に `MeasSample` の全フィールドを `isfinite()` で検査する。
- `D.Gu.g` / `D.Gd.g` の finite check を測定直前に入れる。
- fail 時には beta, T, replica id, seed, bin id, measurement index, sweep_count, stab, Ltr を stderr に出して `dqmc_run_replica()` を nonzero return にする。
- `replica_bin_add()` またはその呼び出し側で non-finite sample/sign を拒否する。
- `replica_bin_values()` でも bin sums と出力値の finite check を行う。
- `jackknife()` または `jackknife_and_print()` でも入力配列の finite check を行う。

最低限の修正対象:

- `src/replica_run.c`
- `src/replica.c`
- `src/measure.c` または `src/measure.h`
- tests under `tests/`

推奨テスト:

- `replica_bin_values()` が NaN sum を reject する unit test。
- `measure_sample` 由来の non-finite を replica failure にする integration-style test。
- MPI pack/unpack 後の NaN bin が root 側で failure になる test。

### 2. 該当 seed で最初の破綻位置を取る

fail-fast を入れたら、まず以下と同等の入力を一時ファイルに作って再実行する。

```ini
lattice=file
latfile=data/production_runs/kugui_F1cpu_2d4x4_U16_PP_dtau0p025_T0p04_to_0p01_nrep120_n10k_gridT_alt_fc0416a_prebuilt_U12U16_20260707/hopping_PP.txt
Lx=4
Ly=4
pbc=1
t=-1.0
U=16
dtau=0.025
beta_list=33.325
nwarm=2000
nmeas=9000
nbin=100
stab=4
parallel=serial
nrep=1
profile=0
seed=14012418791647386686
sweep_order=alternating
```

実行は serial `./dqmc input.in` でよい。MPI-enabled build を使う場合は `mpirun -np 1 ./dqmc_mpi input.in` とし、`parallel=serial` のままにする。

重要条件:

- L=4, U=16
- beta=33.325
- dtau=0.025
- seed=14012418791647386686
- nwarm=2000
- nmeas=9000
- nbin=100
- stab=4
- sweep_order=alternating

この run は `nmeas=8000` では finite、`nmeas=9000` で NaN になるため、最初の failure を比較的狭い範囲で捕まえられる。

### 3. Green/linalg 側の診断を追加する

観測直前の Green が非有限なら、次は `udv_inv_one_plus_work()` の中間行列を段階的に調べる。

推奨 finite check points:

- after `dtrtri()` for `Tinv`
- after `la_gemm()` for `UT_Tinv`
- after constructing `M`
- after `la_inverse_work()` for `Minv`
- after constructing `tmp`
- after constructing `gp`
- after final `g`

fail 時に出したい情報:

- beta, Ltr, tau, sweep_count
- spin sector
- min/max/finite count for `s->D`, `Db`, `Ds`
- first non-finite matrix name and index
- LAPACK `info` if available

この段階で exact operation が分かれば、安定化アルゴリズムの改善に進める。

### 4. 安定化改善を試す

`stab=2` では改善しなかったため、次は単なる頻度短縮以外を検討する。

候補:

- `stab=1` の確認。ただし計算量は増える。
- `udv_inv_one_plus_work()` の finite guard を強化し、失敗時に hard failure とする。
- `M` inversion 周辺の条件数診断を追加する。
- 小系・低温だけ SVD fallback を試す。
- QR/SVD の pivoting や scale handling を見直す。
- 必要なら long double / higher precision diagnostic で beta=33.325 の可解性を確認する。

### 5. データ整理上の扱い

現行 summary では、L=4, U=16 は `T >= 0.04` の clean window のみ通常 summary に残し、`T=0.0300075, 0.02, 0.01` は bad parameter HTML 側で扱う方針を維持する。

コード修正後に再 run するまでは、既存の beta=33.325 以下の NaN 行を「低温強結合の数値破綻」として除外し、物理的な extrapolation には使わない。

## 推奨作業順

1. finite check / fail-fast を実装し、NaN が `status=ok` にならないようにする。
2. seed `14012418791647386686` の beta=33.325, `nmeas=9000` を再実行し、最初の failure sweep/bin を特定する。
3. failure が Green 非有限なら `udv_inv_one_plus_work()` の中間行列 finite check を追加する。
4. exact operation が分かったら、SVD fallback などの安定化改善を小系で比較する。
5. 改善版で L=4, U=16 の beta=33.325 を再検証し、usable window を更新できるか判断する。
