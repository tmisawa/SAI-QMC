---
date: 2026-07-15
datetime: 2026-07-15 17:45 JST
model: GPT-5 Codex
topic: Track A centered scalar offset A5 validation items 2 and 3
summary: |
  centered modeはbeta=25およびbeta=30--32で既存経路と出力が一致し、
  beta=33.325ではproduction seed列の先頭4本が各11000 sweepを完走した。
  一方、既知seedはbeta=34の長run、beta=50/100の短runで型付きfail-fastし、
  beta=50/100への一般化とdefault昇格は否定された。
---

# Track A A5 Validation: Safe-Region Equivalence and Applicability Boundary

## Scope

- 実行環境: local macOS、外部計算機・scheduler jobなし。
- 模型: 4x4 PP、`U=16`、`dtau=0.025`、`stab=4`、half filling。
- lattice:
  `data/production_runs/kugui_F1cpu_2d4x4_U16_PP_dtau0p025_T0p04_to_0p01_nrep120_n10k_gridT_alt_fc0416a_prebuilt_U12U16_20260707/hopping_PP.txt`
- sweep: `alternating`。
- centered mode: `green_rebuild=centered`。
- 正式長: `nwarm=2000`、`nmeas=9000`、`nbin=100`、合計11000 sweep。

この検証は有限個のseedと一つの格子・結合条件に対する実証であり、すべての
field trajectoryに対する数学的保証ではない。

## Item 2: Safe-Region Equivalence

### beta=25 three-mode comparison

seed `14012418791647386686`、`nwarm=500`、`nmeas=2000`、`nbin=20`で
`combine`、`two_sided`、`centered`を比較した。

| mode | E_hub | dE_hub | doublon | sign | acceptance | runtime (s) |
|---|---:|---:|---:|---:|---:|---:|
| combine | -5.6498048 | 0.206 | 0.018694878 | 1 | 0.44965328 | 22.15 |
| two_sided | -5.6498048 | 0.206 | 0.018694878 | 1 | 0.44965328 | 19.10 |
| centered | -5.6498048 | 0.206 | 0.018694878 | 1 | 0.44965328 | 23.80 |

表示された全列は3 modeで一致した。stderrはすべて空だった。

### beta=30--32 Phase 5 baseline comparison

seed `14012418791647386686`、`nwarm=1000`、`nmeas=5000`、`nbin=50`で
centered modeを実行し、既存のcombine/two_sided Phase 5基準値と比較した。

| beta | E_hub | dE_hub | doublon | sign | acceptance |
|---:|---:|---:|---:|---:|---:|
| 30 | -4.5839561 | 0.116 | 0.015910774 | 1 | 0.44972805 |
| 31 | -4.6630959 | 0.128 | 0.015978759 | 1 | 0.4493698 |
| 32 | -4.5880617 | 0.154 | 0.016104701 | 1 | 0.44966587 |

`E_hub/E_gc/E_ph`、各誤差、`ntot`、doublon、sign、acceptanceを含む
全出力列がPhase 5のcombine/two_sided記録と一致した。centered runのruntimeは
211.39 s、stderrは空だった。

## Item 3: Multi-Seed Robustness and Low-Temperature Boundary

### beta=33.325 four-seed formal-length run

production replica seed列の先頭4本を`parallel=omp`、4 replicasで実行した。

| replica | seed | sweeps | status |
|---:|---:|---:|---|
| 0 | 246813579 | 11000 | ok |
| 1 | 14012418791647386686 | 11000 | ok |
| 2 | 11473674334183429640 | 11000 | ok |
| 3 | 4985831835181638333 | 11000 | ok |

- runtime: 515.04 s。
- stderr: 空。
- aggregate: `E_hub=-4.8017564`, `dE_hub=0.0512`、
  `doublon=0.016586686`, `dD=0.00015`、sign=1、
  `acceptance=0.44943051`, `dAcceptance=0.000055`。

これにより「既知の1 seedだけ」という未保証は4 seedの実証へ拡張された。
ただし、120 replicas全体または任意seedの保証ではない。

### Short-run diagnostic frontier

seed `14012418791647386686`、`nwarm=0`、`nmeas=2`でcentered companion
diagnosticを有効にした。

| beta | result | minimum remaining margin | first typed failure |
|---:|---|---:|---|
| 33.5 | pass | 15.339 | none; warning rowsあり |
| 34.0 | pass | 30.170 | none; warning rowあり |
| 34.25 | fail at first measurement sweep | 10.091 | `centered_update_margin`, tau=1352 |
| 34.5 | fail at first measurement sweep | 8.931 | `centered_update_margin`, tau=1376 |
| 34.75 | fail at first measurement sweep | 10.276 | `centered_update_margin`, tau=1380 |
| 35 | fail at first measurement sweep | 9.361 | `centered_update_margin`, tau=1384 |
| 50 | fail at first measurement sweep | 8.691 | `centered_update_margin`, tau=1372 |
| 100 | init failure | diagnostic未有効 | `centered_update_margin` |

beta=34.25以降はstored radiusのhard margin 8に達する前でも停止した。
これは次のB行列を掛けたQR入力のlog-domain上界を検査するinput-aware gateが、
単純なstored radiusより早く危険を検出したためである。

### Formal-length frontier

同じseedで正式長を実行した。

| beta | result | runtime (s) | detail |
|---:|---|---:|---|
| 33.5 | pass | 135.12 | 11000 sweeps、stderr空 |
| 33.75 | pass | 140.60 | 11000 sweeps、stderr空 |
| 34.0 | fail | 29.66 | `sweep_count=2684`, bin=7, meas=54 |

beta=34は2 sweep smokeでは通ったが長runで停止した。したがって、このseedと
正式長に対する持続可能境界は0.25刻みで次の範囲にある。

```text
33.75 < beta_fail <= 34.0
```

これはseed依存のtrajectory boundaryであり、beta=33.75を全seedに保証するものではない。

## Decision

1. **beta=25--32の整合性:** pass。centeredはsafe regionで既存経路と一致する。
2. **beta=33.325の対象範囲:** 4 production seeds x 11000 sweepsでpass。
   Track Aの当初acceptance targetは満たした。
3. **beta=50/100:** unsupported。beta=50は最初のmeasurement sweep、beta=100は
   initで`centered_update_margin`停止するため、追加検証待ちではなく現方式の範囲外。
4. **default mode:** `combine`から`centered`へ昇格しない。既存provenanceを変える上、
   centeredもbeta=34長run以降を解けず、一般解ではない。
5. **long double:** scoped beta=33.325にはbinary64 centeredで不要。一方、
   beta=100の本解としてportable long doubleに依存しない。
6. **next algorithm:** beta=50/100にはsingle factorへ潰さないstructured UDV chain
   solverが必要。centeredは限定的opt-inとして維持する。

## Verification Context

A4完成時点で次を通過済みである。

- `make test`
- `make test_omp`
- `make test_mpi`
- `make test_hybrid`
- `make test_slow`
- `git diff --check`

説明HTMLも同じ判定へ更新し、ローカルChromeでMathJax 82要素、`mjx-merror=0`、
390 px emulationで`scrollWidth=innerWidth=390`を確認した。
