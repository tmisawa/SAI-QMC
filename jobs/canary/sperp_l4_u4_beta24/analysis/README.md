---
date: 2026-08-22
datetime: 2026-08-22 18:28 JST
model: Codex (GPT-5)
status: review
topic: 4x4 U=4 beta=24 Szz/Sperp Genkai canary
summary: |
  Genkaiカナリア1 jobはexit 0で、checksum、120 replica、全q和則、出力schemaを通過した。
  Sperp(pi,pi)はEDと0.53 sigmaで整合したが、paired SU(2)差は5.06 sigmaであり、
  短いwarmup/measurementでのspin sector収束は未達と判定する。
---

# U=4 Szz/Sperp Genkai canary

## 結論

- job/integrity gate: **PASS**。PJM job `6562854` はexit 0、script status `PASS`、
  120/120 replicas `ok`、sign=1、各spin TSVはfiniteな16 q行で、回収した
  `RESULTS.sha256` 11 entryは全て一致した。
- $S_\perp$ estimator check: **PASS**。$S_\perp(\pi,\pi)=1.83435\pm0.01991$ は
  ED ground-state値 `1.8237664402822897` と0.53 sigmaで整合する。
- short-run spin convergence: **FAIL/HOLD**。
  $\Delta_{\mathrm{SU2}}=S_\perp/2-S^{zz}=0.06990\pm0.01381$、5.06 sigmaで0から離れる。
  SU(2)はこの模型のensembleで有限`dtau`でも成立するため、Trotter誤差では説明しない。
- 従って実装のHPC実行経路と$S_\perp$の値は健全だが、このカナリアだけを
  productionの$S^{zz}$値または誤差評価には採用しない。U=4 beta系列はまだ投入しない。

## 実行条件

- run id: `afqmc-sperp-l4-u4-dt0p025-b24-canary-genkai-20260822-r0`
- source commit: `463dc75be5327f636b2683f58f10343393fb7e17`
- Genkai, 1 node, 120 MPI processes, 1 thread/process
- square 4x4 PP, half filling, $t=-1$, $U=4$, $\beta=24$, `dtau=0.025`, `stab=4`
- `nrep=120`, `nwarm=500`, `nmeas=2000`, `nbin=20`
- elapsed: 39.22 s; node: `a0767`

## 主要結果

| observable | QMC | ED ground state | QMC - ED | nominal separation |
| --- | ---: | ---: | ---: | ---: |
| $E/N$ | $-0.849992\pm0.000612$ | -0.8513659263 | +0.00137393 | 2.24 sigma |
| doublon/site | $0.115752\pm0.000110$ | 0.1151255607 | +0.00062647 | 5.70 sigma |
| $S^{zz}(\pi,\pi)$ | $0.84727\pm0.00696$ | 0.9118832201 | -0.0646115 | 9.28 sigma |
| $S_\perp(\pi,\pi)$ | $1.83435\pm0.01991$ | 1.8237664403 | +0.0105809 | 0.53 sigma |

EDは絶対零度、QMCは有限温度・有限`dtau`なので、energyとdoublonのnominal sigmaは
correctness gateにしない。特にdoublonとenergyには`dtau`外挿が必要である。一方、同一QMC
ensemble内の$S_\perp=2S^{zz}$は有限`dtau`でも成立するため、paired SU(2)差は
thermalization/autocorrelationの直接診断として使える。

全qの和は

```text
sum_q Szz   = 3.0739837432232062
sum_q Sperp = 6.1479674864464107
```

で、$\sum_qS_\perp=2\sum_qS^{zz}$は丸め誤差内で成立した。stdoutに丸めて出力された
doublonから再構成したlocal-moment和則との差も`3.4e-8`以下である。これは各sampleで
成立する代数的和則なので、SU(2) ensemble収束とは独立な実装・データ完全性チェックである。

profilerではjoint `measure_spin` が240000 calls、合計0.4250 s、beta wallの1.65%だった。
component別の`measure_szz`/`measure_sperp`への誤帰属は無い。

## 要約生成の不具合

remoteの`canary_summary.tsv`はheader 1行だけだった。原因は投入済みPJMの後処理で、
AWK `printf`に8個の変換指定を置きながら値を7個しか渡していたことにある。さらにpipelineの
末尾が`while`だったためAWKの非zero終了がjob statusへ伝播しなかった。headerもsingle-quoted
`echo`によりliteral `\\t`だった。

生の`out.dat`、`szz.tsv`、`sperp.tsv`、`spin_consistency.tsv`には影響しない。
投入物のprovenanceを保つためPJMとremote生成物は変更せず、このdirectoryの
`canary_summary.tsv`を生データから独立に再作成した。次の投入スクリプトでは、要約行数を
明示検査し、pipelineに依存しない生成へ直す。

## 次の判定

短いrunでは$S^{zz}$が低く、$S_\perp/2=0.91717(100)$の方がEDの$S^{zz}$に近い。
これは「実空間対角$S^{zz}$は揺らぎ・自己相関が大きい」という事前懸念と整合するが、
単一カナリアから原因を確定はしない。

次は同じ$U=4,\beta=24,dtau=0.025$で、独立base seed、`nwarm=10000`,
`nmeas=50000`, `nbin=25`（2000 sweeps/bin）のlong diagnosticを1本行う。
primary gateはpaired $\Delta_\mathrm{SU2}(\pi,\pi)$が3 sigma以内であること。
通過後に別seedの再現run、続いてbeta系列へ進む。

## 回収先

- hpcflow result:
  `hpcflow/.hpcflow/runs/afqmc-sperp-l4-u4-dt0p025-b24-canary-genkai-20260822-r0/sync/`
- include指定が広く、remote `build/`内の不要なテキスト1901 files（27 MB）もlocalへ
  同期された。該当local directoryだけを
  `/tmp/afqmc-sperp-u4-unwanted-sync.qe9nNm`へ隔離した。binary/objectは取得しておらず、
  remoteは変更していない。
