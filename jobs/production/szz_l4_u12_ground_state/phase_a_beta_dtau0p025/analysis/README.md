---
date: 2026-08-22
datetime: 2026-08-22 10:19 JST
model: Codex (GPT-5)
summary: |
  Genkai Phase A beta ladder 6 jobのraw結果を回収・再検証した。
  計算本体は全て完走したが、job後処理のawk丸めにより6 jobがfalse-negative FAILとなった。
  raw結果は健全だが、低温Szzの点間変動が報告誤差を大きく超え、plateau未確立と判定した。
---

# L4 U12 Szz Phase A beta ladder解析

## 結論

6 jobのDQMC計算本体とraw出力は回収でき、120 replicas、有限値、符号、Szz行数、
全q和則を再検証して全点を`recovered_status=PASS`とした。ただし
`Szz(pi,pi)`のbeta=12--32データは報告された統計誤差に対して整合せず、現時点で
低温plateauまたはbeta外挿を主張できない。Phase Bのdtau scanへ進む前に、低温Szzの
自己相関、bin幅、thermalization、独立seed再現性を診断する必要がある。

## job statusがFAILになった理由

production scriptはSzz 16点の和を

```sh
SUM_SZZ=$(awk '... END {print sum}' szz.tsv)
```

で一度文字列化した。awkの既定出力が約6桁だったため、その丸め値とscalarのdoublonから
作った和則右辺を`1e-6`で比較し、全6 jobが計算完了後にexit 1となった。元の17桁Szzから
再計算した差は`1.5e-10`--`2.7e-9`であり、和則は成立している。今後のscriptでは
`printf "%.17g", sum`を用いる。

このfalse-negativeは結果生成後に起きたため`RESULTS.sha256`はremoteで生成されなかった。
回収した`out.dat`と`szz.tsv`のlocal SHA-256を`recovered_output_hashes.tsv`に固定した。

## 再検証内容

- PJM履歴: 6 jobすべて`EXT`。
- MPI実行: 46.41--275.31秒で完了し、各runにscalar 1行、Szz 16行を生成。
- replicas: 各run 120行、全て`status=ok`、rank重複なし。
- scalar: 全値finite、`N=16`、`sign=1`。
- Szz: 全16 q indexが一意、全値finite、`dSzz>=0`。
- full-precision all-q sum rule: 全runで差の絶対値`<3e-9`。
- source/input: job本体が計算前のmanifest検査を通過しており、source commitは
  `4ba47c5d8b2e1095bed82a81ed951b0b3ec7cfc8`。

## AF点のbeta依存性

ED/mVMCのtotal-spin規約との比較には`3*Szz(pi,pi)`を使う。4x4 P x P、U=12のED値は
`4.1455892258`。

| beta | `Szz(pi,pi)` | `3*Szz(pi,pi)` | EDとの差 |
| ---: | ---: | ---: | ---: |
| 4 | 0.94388 +/- 0.00701 | 2.8316 +/- 0.0210 | 1.3140 |
| 8 | 1.24542 +/- 0.00812 | 3.7363 +/- 0.0244 | 0.4093 |
| 12 | 1.31847 +/- 0.00836 | 3.9554 +/- 0.0251 | 0.1902 |
| 16 | 1.24784 +/- 0.00812 | 3.7435 +/- 0.0244 | 0.4021 |
| 24 | 1.29649 +/- 0.00828 | 3.8895 +/- 0.0248 | 0.2561 |
| 32 | 1.22365 +/- 0.00797 | 3.6709 +/- 0.0239 | 0.4747 |

beta=12,16,24,32を定数plateauへ重み付きfitすると、longitudinal値は
`1.270234 +/- 0.004089`だが、`chi2=85.12`、自由度3、`chi2/dof=28.37`となる。
隣接差は順に6.06 sigma、4.20 sigma、6.34 sigmaで、報告誤差では説明できない。
非AF q点もAF点と反相関した交互変動を示し、全q和則を保ったままweightが再配分されている。

## 次の判定

Phase Aは計算成功だが、physics gateは未合格とする。Phase Bを直ちにfan-outせず、少なくとも
beta=24,32で長いbinと長いwarmupを用いた独立seed再現runを行う。可能ならSzzをreplica単位
またはbin単位で保存し、chain内jackknifeだけでなくreplica-cluster誤差も評価する。
