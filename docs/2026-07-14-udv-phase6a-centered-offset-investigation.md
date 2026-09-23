---
date: 2026-07-14
datetime: 2026-07-14 23:40 JST
model: GPT-5 Codex
status: review
topic: Phase 6A centered scalar offset feasibility on the real DQMC trajectory
summary: |
  beta=33.325 の既知 seed と beta=30--32 の実 trajectory で centered
  exponent margin を測定し、失敗直前の実 UDV factor を 500 桁参照と比較した。
  centered inversion は実 factor でも相対誤差 6.8e-13 だったが、目標点の
  exponent margin は 31.9--34.4 と小さい。Track A prototype は進める価値が
  あるが、長 run 完走はまだ保証されず、chain solver の代替にはならない。
---

# Phase 6A Centered Scalar Offset Investigation

## Question

Phase 6 design review が提案した centered scalar offset は、合成された
well-conditioned factor だけでなく、実際に失敗する DQMC trajectory でも成立するか。

確認項目:

1. centered exponent radius と `ln(DBL_MAX)` までの margin。
2. beta=30--32 の sweep 間揺らぎ。
3. 実 UDV `T` の条件性。
4. 失敗直前の実 `U,D,T` に対する centered Db/Ds inversion の forward error。

## Conditions

- lattice: 4x4 PP hopping file
- U=16, dtau=0.025, stab=4
- seed `14012418791647386686`
- alternating sweep
- `green_rebuild=two_sided`
- serial local run

beta=33.325 は `nwarm=1,nmeas=2,nbin=2`。beta=30,31,32 は
`nwarm=20,nmeas=2,nbin=2` で診断した。

centered quantities:

```text
offset = (max_logD + min_logD) / 2
radius = (max_logD - min_logD) / 2
normal_margin = ln(DBL_MAX) - radius
```

## Centered Margin Results

| beta | observed sweeps | worst radius | worst normal margin |
|---:|---:|---:|---:|
| 30 | 22 | 608.168 | 101.615 |
| 31 | 22 | 631.150 | 78.633 |
| 32 | 22 | 645.599 | 64.184 |
| 33.325 | initial forward sweep until failure | 677.902 | 31.881 |

beta=30--32 の sweep ごとの最悪 margin:

- beta=30: 101.6--132.5
- beta=31: 78.6--117.2
- beta=32: 64.2--85.6

beta=33.325 では tau=1320/1324 の factor が
`min_logD=-647.658`, `max_logD=708.146`, spread=1355.804 となり、centered
なら double normal range 内に入る。ただし片側 margin は 31.88 しかない。

## Actual T Conditioning

spread_logD > 1000 の実 factors について `cond_inf(T)` を一時診断した。

| beta | samples | median cond_inf(T) | max cond_inf(T) |
|---:|---:|---:|---:|
| 30 | 1954 | 1.80e7 | 1.31e12 |
| 31 | 2210 | 4.73e6 | 2.26e12 |
| 32 | 2761 | 1.62e7 | 9.94e12 |

したがって review experiment の直交 `T` は実 trajectory を代表しない。
ただし、これは centered Db/Ds formula の失敗を直接意味しないため、実 factor
そのものを高精度参照と比較した。

## High-Precision Check on the Actual Factor

beta=33.325 initial forward sweep から、spread が 1350 を初めて越えた有限 factor
を一時採取した。

```text
n = 16
min_logD = -644.9993847270093
max_logD =  705.7737890040993
spread   = 1350.7731737311087
offset   =   30.3872021385450
radius   =  675.3865868655544
margin   =   34.3961260278296
cond_inf(T) = 1.8298129e6
cond_2(T)   = 5.4202456e5
```

centered `Dhat = sign(D) exp(log(abs(D))-offset)` は
`4.82e-294 ... 2.07e293` で、zero/inf を生じなかった。

既存 `udv_inv_one_plus_work()` と同じ Db/Ds algebra を effective logD で評価し、
`G=(I+UDT)^-1` の mpmath 500 桁 direct reference と比較した。

```text
G finite: true
relative forward error: 6.82e-13
max absolute error:     7.51e-11
```

spread=1301.78, margin=58.89 の少し手前の実 factor では相対誤差
`2.57e-13` だった。

## Interpretation

1. centered offset は、失敗直前の実 `T` が非直交かつ moderately
   ill-conditioned でも、Green inversion 自体を有効精度で実行できた。
2. review の Track A は「合成行列だけの見込み」から「実 factor で inversion
   feasibility を確認」へ前進した。
3. ただし検証した factor は現行 linear-D で overflow する直前に採取したもの。
   offset を lmul/rmul/combine で伝播する実装と、trajectory 全体の完走は未検証。
4. margin 31.9 は小さく、長 run の field fluctuation で centered range 自体を
   越える可能性がある。Track A は beta=33.325 限定の experimental fix とする。
5. beta=100 の予測 spread には届かないため、structured chain solver の代替ではない。

## Verdict

Track A の小さい TDD prototype へ進む価値はある。ただし acceptance は厳格にする。

- `UDV` に scalar `log_offset` を追加し、identity/copy/lmul/rmul/combine を覆う。
- centered normalization 前に必要 radius を計算し、設定 margin 未満なら fail-fast。
- one-sided/two-sided inverse は effective logD で Db/Ds を構築する。
- beta=25--32 で現行経路との Green/observable 一致を確認する。
- beta=33.325 known seed を短 run、次に full target で検証する。
- margin、T condition、Green residual/参照差を診断に残す。

実装後に margin が負になる、または Green accuracy gate を外れる場合は Track A を
延命せず停止し、structured `O(m N^3)` chain solver の再設計へ戻る。

## Temporary Instrumentation

T condition と factor dump のコードは調査後に削除した。tracked source に一時
診断変更は残していない。raw diagnostic files は `/tmp/afqmc_phase6a_*` にある。
