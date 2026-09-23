---
date: 2026-08-22
datetime: 2026-08-22 10:58 JST
model: Codex (GPT-5)
status: review
topic: 4x4 U=12 beta=24,32 low-temperature Szz convergence diagnostic
summary: |
  4 jobのintegrityは全てPASS。beta=24は独立seed再現性PASSだが、beta=32は
  Szz(pi,pi)が3.41 sigma、全q最大6.14 sigma離れ、事前基準をFAILした。
  Phase A beta=32の非収束は確認できたが、新run自身も未収束のためPhase Bは保留する。
---

# Low-temperature Szz convergence diagnostic

## 結論

- job integrity gate: **PASS**。4 job全てPJM exit 0、script status PASS、120/120 replicas
  `ok`、sign=1、finite、16 q点、checksum一致、全q和則PASS。
- beta=24 independent-seed gate: **PASS**。`Szz(pi,pi)`系列差は0.639 sigma、
  全q最大差は1.777 sigma。
- beta=32 independent-seed gate: **FAIL**。`Szz(pi,pi)`系列差は3.409 sigma、
  全q最大差はq=(0,0)の6.142 sigma。16 q行中5行が3 sigmaを超えた。
- Phase B gate: **HOLD**。beta=32の統計誤差と独立seed再現性を解決するまで
  dtau scanを開始しない。

## Szz(pi,pi)

| beta | series sa | series sb | seed差 | long-run weighted | `3*Szz` weighted |
|---:|---:|---:|---:|---:|---:|
| 24 | 1.29173 +/- 0.00953 | 1.28329 +/- 0.00914 | 0.639 sigma | 1.28734 +/- 0.00660 | 3.86201 +/- 0.01979 |
| 32 | 1.27129 +/- 0.00948 | 1.31636 +/- 0.00921 | 3.409 sigma | 1.29447 +/- 0.00661 | 3.88342 +/- 0.01982 |

weighted値は2系列が整合する場合のinverse-variance estimate。beta=32では再現性gateが
FAILしているため、表のweighted値をproduction estimateとして採用しない。

long-run weighted beta=24と32の差は0.007137、combined errorで0.765 sigma。
見かけ上はplateauと整合するが、beta=32系列間不一致のためplateau確定には使えない。

## Phase Aとの比較

- beta=24: Phase A `1.29649 +/- 0.00828`に対しlong-run weighted
  `1.28734 +/- 0.00660`。
  差は0.865 sigmaで整合。
- beta=32: Phase A `1.22365 +/- 0.00797`に対しlong-run weighted
  `1.29447 +/- 0.00661`。
  long runは0.070829高く、差は6.840 sigma。Phase A beta=32点は再現しない。
- Phase Aの測定長だけを2.5倍した独立sample scalingなら誤差はbeta=24,32で
  約0.00524, 0.00504を期待するが、long-bin各系列の平均誤差は0.00933, 0.00935。
  比は1.78, 1.85で、短bin誤差が自己相関を十分反映していなかった可能性を支持する。
  ただしwarmupとbin幅も同時に変更したため、この比を自己相関時間の直接推定とはしない。

## 全qおよびscalar observable

- beta=24のseed差は全qで3 sigma未満。最大はq index 5/15の1.777 sigma。
- beta=32では最大がq=(0,0)の6.142 sigma。q index 6/14が3.506 sigma、
  q=(pi,pi)が3.409 sigma、q index 8が3.150 sigma。spin weightのseed依存は
  AF点だけの単発fluctuationではない。
- 一方、series間のenergy差はbeta=24,32で0.058, 1.245 sigma、doublon差は
  0.425, 0.484 sigma。scalar observableは再現しており、問題は低温spin correlationに強い。
- 全q和則残差の絶対値は最大`2.9e-9`で、observable実装や出力integrityの破れではない。

## EDに対するenergyとdoublon

4x4 P x P、half-filled、U=12のED ground-state referenceは、energy per site
`-0.37451396229402456`、double occupancy per site `0.027786853018750002`。

| beta | `E/N` long-run weighted | EDとの差 | `D` long-run weighted | EDとの差 |
|---:|---:|---:|---:|---:|
| 24 | -0.3798851 +/- 0.0001845 | -1.434% | 0.02740218 +/- 0.00001249 | -1.384% |
| 32 | -0.3802651 +/- 0.0001714 | -1.536% | 0.02743581 +/- 0.00001125 | -1.263% |

energy/doublonはseed間だけでなくPhase Aとも1 sigma以内で再現する一方、EDとの差は
QMCの報告統計誤差で約29--34 sigma、約31 sigma。Hamiltonian、粒子数、ensemble規約が
一致する比較なら、有限温度energyがground-state energyより低いことはexact thermal
effectだけでは説明できない。

energyを`E/N=K/N+U*D`に分けると、EDは`K/N=-0.70795620`、beta=24 QMCは
`K/N=-0.70871133`。beta=24のtotal energy差`-0.00537115/site`のうち
`-0.00461602/site`は低いdouble occupancyに伴うinteraction energy差で、主成分はdoublon。
finite-dtau propagator/estimator systematic、またはED/QMC Hamiltonian規約の差を
dtau scan前に再監査する。有限温度、dtau、ensembleのsystematicを分離するまでは
EDとの統計sigmaをaccuracy claimに使わない。

## 誤差評価上の注意と次の診断

現行実装は`nrep*nbin=3000`個のbinを一列にしてjackknifeする。2000 sweep/binでも
同一replica内のbinに残存相関があれば誤差を過小評価する可能性がある。

次はbeta=32に対し、replicaを独立blockとして評価する。最小変更案は同じwarmup・測定長で
`nbin=1`とし、120個の独立replica meanをjackknifeすること。より一般的にはSzzの
replica/bin出力を追加し、replica-blocked errorとbin-width dependenceを同一raw dataから
解析する。いずれも追加jobまたはsource変更なので、本診断には含めず未実施。

## 回収と検証

- 回収先: `hpcflow/.hpcflow/runs/<run-id>/sync/`
- `hpcflow sync`は19個のtext/result fileを明示includeして使用した。
- `source_4ba47c5.tar.gz`、`dqmc*`、object file、`build/`は回収していない。
- 各runの`RESULTS.sha256`は全11 entryが一致した。
- 数値表: `diagnostic_summary.tsv`, `scalar_ed_comparison.tsv`。
