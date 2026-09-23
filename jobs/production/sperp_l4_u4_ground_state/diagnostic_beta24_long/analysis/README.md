---
date: 2026-08-22
datetime: 2026-08-22 18:50 JST
model: Codex (GPT-5)
status: review
topic: 4x4 U=4 beta=24 long-warmup/long-bin Szz/Sperp diagnostic
summary: |
  Genkai long diagnostic 1 jobはintegrity gateとpaired SU(2) primary gateを通過した。
  DeltaSU2(pi,pi)は短いcanaryの5.06 sigmaから1.57 sigmaへ改善し、全16 qも3 sigma未満。
  Szz(pi,pi)はEDと0.72 sigma、Sperp(pi,pi)はnominal 2.74 sigmaで整合範囲にある。
---

# U=4 beta=24 long Szz/Sperp diagnostic

## 結論

- job/integrity gate: **PASS**。PJM job `6562903` はexit 0、script status `PASS`、
  120/120 replicas `ok`、120 unique seeds、sign=1、各spin TSVはfiniteな16 q行。
  回収した`RESULTS.sha256` 12 entryは全て一致した。
- paired SU(2) primary gate: **PASS**。
  $\Delta_{\mathrm{SU2}}(\pi,\pi)=-0.006170\pm0.003938$、1.57 sigma。
  全16 qでも最大2.45 sigma（q=0）で、3 sigma超は0点だった。
- short canaryの5.06 sigma不整合は、long warmup/binで解消した。HPC上のjoint
  Szz/Sperp estimator、MPI gather、paired jackknife、出力経路に異常を示す証拠はない。
- 実装のremote physics validationはPASSと判定する。ただしproduction誤差には
  同一long protocolの別base seedを少なくとももう1本取り、run間散らばりを確認する。

## 実行条件

- run id: `afqmc-sperp-l4-u4-dt0p025-b24-long-sa-genkai-20260822-r0`
- source commit: `463dc75be5327f636b2683f58f10343393fb7e17`
- Genkai, 1 node, 120 MPI processes, 1 thread/process
- square 4x4 PP, half filling, $t=-1$, $U=4$, $\beta=24$, `dtau=0.025`, `stab=4`
- `nrep=120`, `nwarm=10000`, `nmeas=50000`, `nbin=25`、2000 sweeps/bin
- base seed: `625273478514988728`、independent series `sa`
- elapsed: 624.39 s; node: `a0767`

## 主要結果

| observable | long QMC | ED ground state | QMC - ED | nominal separation |
| --- | ---: | ---: | ---: | ---: |
| $E/N$ | $-0.852146\pm0.000211$ | -0.8513659263 | -0.00078007 | 3.69 sigma |
| doublon/site | $0.1150801\pm0.0000500$ | 0.1151255607 | -0.00004547 | 0.91 sigma |
| $S^{zz}(\pi,\pi)$ | $0.910381\pm0.002084$ | 0.9118832201 | -0.00150218 | 0.72 sigma |
| $S_\perp(\pi,\pi)$ | $1.808422\pm0.005594$ | 1.8237664403 | -0.0153440 | 2.74 sigma |

EDは絶対零度、QMCは有限温度・有限`dtau`なので、energyの3.69 nominal sigmaを
correctness failureとはしない。特にenergyは`dtau`外挿前である。spinについては
$S^{zz}$、$S_\perp$とも3 sigma以内で、同一ensembleのpaired SU(2)差もPASSした。

## short canaryとの比較

| observable | short canary | long diagnostic | long - short |
| --- | ---: | ---: | ---: |
| $S^{zz}(\pi,\pi)$ | $0.84727\pm0.00696$ | $0.91038\pm0.00208$ | +8.68 sigma |
| $S_\perp(\pi,\pi)$ | $1.83435\pm0.01991$ | $1.80842\pm0.00559$ | -1.25 sigma |
| $\Delta_\mathrm{SU2}(\pi,\pi)$ | $+0.06990\pm0.01381$ | $-0.00617\pm0.00394$ | -5.30 sigma |

主な変化は$S^{zz}$であり、短いcanaryのspin-sector thermalization/autocorrelation不足を
支持する。scalarもenergy 3.32 sigma、doublon 5.56 sigma動いているため、canaryの
`nwarm=500,nmeas=2000`はproduction値には不十分だった。

## 和則・性能・回収

```text
sum_q Szz   = 3.0793593168506428
sum_q Sperp = 6.1587186337012829
```

$\sum_qS_\perp=2\sum_qS^{zz}$は丸め誤差内。stdout doublonからのlocal-moment和則も
job内の`5e-6` gateを通過した。`measure_spin`は6000000 calls、10.5435 s、
beta wall 611.121 sの1.73%であり、joint observableの追加costは小さい。

回収先は
`hpcflow/.hpcflow/runs/afqmc-sperp-l4-u4-dt0p025-b24-long-sa-genkai-20260822-r0/sync/`。
トップレベル20 filesのみを明示includeし、`build/`、binary、object fileは回収していない。

## 次の判定

実装確認は完了した。論文品質の誤差とbeta系列の採用条件を確立するため、次は同じprotocolの
independent series `sb`を1本実行する。予定base seedは`1094879373331422111`。
`sa`/`sb`の$S^{zz}$、$S_\perp$、$\Delta_\mathrm{SU2}$を全qで比較し、3 sigma超が無ければ
U=4 beta系列へ進む。
