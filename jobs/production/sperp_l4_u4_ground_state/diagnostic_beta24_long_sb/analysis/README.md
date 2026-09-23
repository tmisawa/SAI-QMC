---
date: 2026-08-22
datetime: 2026-08-22 19:24 JST
model: Codex (GPT-5)
status: review
topic: 4x4 U=4 beta=24 independent-seed Szz/Sperp convergence
summary: |
  independent series sbはintegrityとrun内SU(2) gateを通過した。
  sa-sb差はSzz、Sperp、DeltaSU2の全16 qで最大1.42 sigma以下、3 sigma超0点。
  U=4のremote implementation validationとbeta=24 long sampling protocolを採用可と判定する。
---

# U=4 beta=24 independent-seed convergence

## 結論

- series `sb` job/integrity: **PASS**。PJM job `6563047` はexit 0、120/120 replicas
  `ok`、120 unique seeds、sign=1、spin TSV各16 q/finite、全q和則、12 result checksumを
  全て通過した。
- series `sb` run内SU(2): **PASS**。
  $\Delta_{\mathrm{SU2}}(\pi,\pi)=-0.004255\pm0.003983$、1.07 sigma。
  全q最大も1.18 sigmaで、3 sigma超は0点。
- independent-seed gate: **PASS**。`sa`と`sb`の全q差は、$S^{zz}$最大1.41 sigma、
  $S_\perp$最大1.04 sigma、$\Delta_\mathrm{SU2}$最大1.24 sigma。全observableで
  3 sigma超は0点だった。
- よってSperp実装のremote physics validationを完了し、U=4 beta系列へ進める。
  beta=24 production estimateには2 long runの組合せを使う。

## 実行条件

- run id: `afqmc-sperp-l4-u4-dt0p025-b24-long-sb-genkai-20260822-r0`
- source commit: `463dc75be5327f636b2683f58f10343393fb7e17`
- Genkai, 1 node, 120 MPI processes, 1 thread/process
- square 4x4 PP, half filling, $t=-1$, $U=4$, $\beta=24$, `dtau=0.025`, `stab=4`
- `nrep=120`, `nwarm=10000`, `nmeas=50000`, `nbin=25`、2000 sweeps/bin
- base seed: `1094879373331422111`、independent series `sb`
- elapsed: 628.48 s; node: `a0768`

## sb単独値

| observable | sb QMC | ED ground state | QMC - ED | nominal separation |
| --- | ---: | ---: | ---: | ---: |
| $E/N$ | $-0.852044\pm0.000231$ | -0.8513659263 | -0.00067807 | 2.93 sigma |
| doublon/site | $0.1150691\pm0.0000540$ | 0.1151255607 | -0.00005647 | 1.05 sigma |
| $S^{zz}(\pi,\pi)$ | $0.909528\pm0.001976$ | 0.9118832201 | -0.00235501 | 1.19 sigma |
| $S_\perp(\pi,\pi)$ | $1.810547\pm0.005938$ | 1.8237664403 | -0.0132191 | 2.23 sigma |

EDは絶対零度、QMCは有限温度・有限`dtau`なので、energy差はcorrectness gateにしない。

## sa-sb再現性

| observable | sa | sb | seed差 | 全q最大seed差 |
| --- | ---: | ---: | ---: | ---: |
| $E/N$ | $-0.852146\pm0.000211$ | $-0.852044\pm0.000231$ | 0.326 sigma | - |
| doublon | $0.115080\pm0.000050$ | $0.115069\pm0.000054$ | 0.149 sigma | - |
| $S^{zz}(\pi,\pi)$ | $0.910381\pm0.002084$ | $0.909528\pm0.001976$ | 0.297 sigma | 1.412 sigma |
| $S_\perp(\pi,\pi)$ | $1.808422\pm0.005594$ | $1.810547\pm0.005938$ | 0.260 sigma | 1.037 sigma |
| $\Delta_\mathrm{SU2}(\pi,pi)$ | $-0.006170\pm0.003938$ | $-0.004255\pm0.003983$ | 0.342 sigma | 1.241 sigma |

240 replica seedsに衝突は無い。全q比較は独立runの報告誤差を二乗和して評価した。

## 2系列combined estimate

inverse-variance weighted estimateは

```text
Szz(pi,pi)     = 0.909932 +/- 0.001434
Sperp(pi,pi)   = 1.809421 +/- 0.004072
DeltaSU2       = -0.005223 +/- 0.002801  (1.86 sigma)
E/N            = -0.8520996 +/- 0.0001560
doublon        = 0.1150750 +/- 0.0000367
```

weighted $S_\perp$はED ground-state値からnominal 3.52 sigma低いが、同じQMC ensemble内の
paired SU(2)差は1.86 sigmaで0と整合する。また有限温度・有限`dtau` systematicと、既知の
`dSperp`過小評価可能性を含まないため、このED差を実装failureとはしない。betaおよび
`dtau`依存を分離して最終比較する。

## 和則・性能・provenance

```text
sum_q Szz   = 3.0794473069114909
sum_q Sperp = 6.1588946138229863
```

$\sum_qS_\perp=2\sum_qS^{zz}$は丸め誤差内。`measure_spin`は6000000 calls、
10.3957 s、beta wall 614.912 sの1.69%。

両runは同一source commit、compiler modules、flagsを使った。一方binary SHA-256は異なった。
build logでは`src/io.mpi.o`と`src/rng.mpi.o`のlink順が入れ替わっており、Makefileの
`$(wildcard src/*.c)`に由来するlink orderの非再現性と考えられる。両jobの個別manifest、
source archive、実行時integrityはPASSしており、seed再現性も良好なのでphysics gateには
しない。bit-reproducible buildが必要ならsource listの明示sortを別途検討する。

回収先:
`hpcflow/.hpcflow/runs/afqmc-sperp-l4-u4-dt0p025-b24-long-sb-genkai-20260822-r0/sync/`。
トップレベル20 filesのみを明示includeし、`build/`、binary、object fileは回収していない。
