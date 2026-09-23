---
date: 2026-08-21
datetime: 2026-08-21 22:10 JST
model: Codex (GPT-5)
summary: |
  半充填ハバード模型の E(T) を grand-canonical ED/TPQ と比較する検証手順。
  アンサンブル整合・dtau→0 外挿・規約変換・符号/粒子数チェック・2D の ED サイズ制約をまとめる。
  S^{zz}(q) の実装検証文書への入口も追加した。
---

# 検証手順: E(T) を ED/TPQ と比較

QMC 出力列:

```text
T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign
```

- `E_hub = <H0> = ekin + eint`
- `E_gc = <H0 - mu N>`
- `E_ph = <H0 - (U/2)N + (U/4)n_site>`
- `H0 = K_hop + U sum_i n_i↑ n_i↓`

## 0. アンサンブルの整合

DQMC は `mu=U/2` の grand-canonical ensemble。
粒子正孔対称点で `<N>=n_site` にはなるが、固定粒子数の canonical ensemble と有限温度・有限サイズで一致するとは限らない。

比較の原則:

- grand-canonical ED と比較する。
- ED 側も `Z = Tr exp[-beta(H0 - mu N)]`、`mu=U/2` とする。
- `mu` は重みに一度だけ入れ、演算子として何を測るかを明確にする。
- 主比較は QMC の `E_hub=<H0>` と grand-canonical ED の `<H0>`。
- `E_gc` を使う場合は ED 側も `<H0-mu N>` を返す。

canonical TPQ/ED を使う場合は、直接一致を主張せず、アンサンブル差があり得る検証項目として扱う。

## 1. dtau→0 外挿

固定 beta で `dtau=0.2,0.1,0.05` などを実行し、各エネルギーを `dtau^2` で線形外挿する。

```text
E(dtau) = E(0) + c dtau^2
```

U=4,8 では統計誤差を小さくすると Trotter 系統誤差が支配的になることがある。
ED/TPQ 比較は原則として外挿後に行う。

## 2. ED/TPQ との比較

最初の対象:

- 1D L=4, PBC
- U/t=4
- half filling
- grand-canonical ED

規約変換:

```text
E_ph = E_hub - (U/2) ntot + (U/4) n_site
E_gc = E_hub - mu ntot
```

相手が `U(n↑-1/2)(n↓-1/2)` 形なら `E_ph` 列を使う。
相手が非対称 `U n↑n↓` 形の `H0` を測っているなら `E_hub` 列を使う。

判定目安:

```text
|E_qmc(dtau→0) - E_ED| < 2 * (統計誤差 + 外挿誤差)
```

外挿前の生データで比較する場合は、統計誤差だけで判定しない。

## 3. 粒子数と符号

必ず同時に確認する:

- `ntot ≈ n_site`
- `sign = 1`

v1 は non-bipartite lattice を実行エラーにする。
そのため `sign` は半充填二部格子における絶対符号として 1 に保たれる想定。

## 4. 2D 検証の注意

- `2x2` PBC は同一 pair の二重結合が出るため、定量比較には使わない。
- grand-canonical ED は Hilbert 空間が `4^N` で増える。
- 2D の ED 照合用は小 OBC 系に限定する。

候補:

- `2x2` OBC
- `2x3` OBC
- `2x4` OBC / ladder

`4x4` は grand-canonical ED には大きすぎるため、TPQ や将来検証向けとして分けて扱う。

任意格子入力を使う場合は、DQMC が使った hopping 行列を ED 側にも渡し、規約を一致させる。

## 5. Szz(q) の検証

equal-time longitudinal spin structure factor の規格化、U=0 独立 oracle、sum rule、
PH mapping、parallel/MPI integrity、bin-width check は
`docs/2026-08-21-szz-structure-factor-validation.md` を参照する。

入力 selector と TSV schema は
`docs/2026-08-21-szz-structure-factor-usage.md` にまとめている。
