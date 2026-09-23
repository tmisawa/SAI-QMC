---
date: 2026-08-22
datetime: 2026-08-22 19:57 JST
model: Codex (GPT-5)
status: in-progress
topic: 4x4 U=4 fixed-dtau Szz/Sperp beta ladder
summary: |
  beta=4 production pointをGenkaiで取得し、integrityと全q SU(2) gateを通過した。
  beta=4と独立2系列を統合したbeta=24低温anchorを共通表へ収録した。
  次は同じlong protocolでbeta=8を1 job実行する。
---

# U=4 fixed-dtau beta ladder

## 現在の結論

- $\beta=4$ job/integrity: **PASS**。PJM job `6563181` はexit 0、120/120 replicas
  `ok`、120 unique seeds、sign=1、spin TSV各16 q/finite、全q和則、12 result checksumを
  全て通過した。
- $\beta=4$ run内SU(2): **PASS**。
  $\Delta_{\mathrm{SU2}}(\pi,\pi)=-0.002198\pm0.001433$、1.53 sigma。
  全q最大は1.94 sigmaで3 sigma超は0点。
- 低温anchorには$\beta=24$ long run 2系列のinverse-variance weighted estimateを使う。
- `../../Benchmark_Hubbard`には4x4 U=4のground-state EDはあるが、beta=4のfinite-temperature
  ED/FTLM/TPQ referenceは見つからなかった。この点ではQMCのSU(2)・和則・beta連続性を
  主な内部検証とし、ED ground stateは低温極限の参照にのみ使う。

## beta=4実行条件

- run id: `afqmc-sperp-l4-u4-dt0p025-b4-prod-sa-genkai-20260822-r0`
- source commit: `463dc75be5327f636b2683f58f10343393fb7e17`
- Genkai, 1 node, 120 MPI processes, 1 thread/process
- square 4x4 PP, half filling, $t=-1$, $U=4$, $\beta=4$, `dtau=0.025`, `stab=4`
- `nrep=120`, `nwarm=10000`, `nmeas=50000`, `nbin=25`、2000 sweeps/bin
- base seed: `663581697154521189`、series `sa`
- elapsed: 114.24 s; node: `a0769`

## beta=4結果

```text
E/N                  = -0.803954 +/- 0.0001038
doublon              =  0.125860 +/- 0.0000190
Szz(pi,pi)           =  0.644632 +/- 0.000741
Sperp(pi,pi)         =  1.284868 +/- 0.002239
DeltaSU2(pi,pi)      = -0.002198 +/- 0.001433  (1.53 sigma)
```

全q和は`sum Szz=2.9931199121284751`、`sum Sperp=5.9862398242569519`で、
$\sum_qS_\perp=2\sum_qS^{zz}$は丸め誤差内。`measure_spin`は6000000 calls、
8.6363 s、beta wall 100.985 sの8.55%。

回収先:
`hpcflow/.hpcflow/runs/afqmc-sperp-l4-u4-dt0p025-b4-prod-sa-genkai-20260822-r0/sync/`。
トップレベル20 filesのみを明示includeし、`build/`、binary、object fileは回収していない。

## 次の点

$\beta=8$も同じlong protocolとし、betaごとに独立な決定論的base seedを用いる。
予定seedは`17542249865750719`。1 parameter = 1 job規則に従い、beta=8を解析してから
beta=12へ進む。
