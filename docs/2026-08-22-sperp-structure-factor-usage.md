---
date: 2026-08-22
datetime: 2026-08-22 17:44 JST
model: Codex (GPT-5)
status: current
topic: equal-time transverse spin structure factor Sperp(q) usage
summary: |
  S_perp(q)=Sxx(q)+Syy(q) の入力、出力、SU(2) consistency 診断を説明する。
  joint profiler region と出力先衝突検査、独立 seed を用いた誤差評価上の注意を含む。
---

# Sperp Structure Factor Usage

## Definition

AF_QMC が出力する transverse spin structure factor は

$$
S_\perp(\mathbf q)
=\frac12\left[S^{+-}(\mathbf q)+S^{-+}(\mathbf q)\right]
=S^{xx}(\mathbf q)+S^{yy}(\mathbf q),
$$

$$
S_\perp(\mathbf q)=\frac1N\sum_{ij}
e^{-i\mathbf q\cdot(\mathbf r_i-\mathbf r_j)}
\left\langle S_i^xS_j^x+S_i^yS_j^y\right\rangle
$$

である。raw ladder sum ではなく、その半分である。SU(2) invariant な現行 Hubbard
model の ensemble では任意の `dtau` で $S_\perp=2S^{zz}$ となる。一方、個々の
spin-channel HS sample は z 軸を選ぶため、sample ごとの一致は要求しない。

## Input

既定では無効であり、従来 input の乱数列、scalar stdout、Szz TSV を変更しない。

```text
sperp_q=none
sperp_file=sperp.tsv
spin_consistency_file=none
```

`sperp_q` は `szz_q` と同じ `none|af|all|mx:my,...` 文法、有限格子 momentum、
ordering を使う。`sperp_q!=none` で `sperp_file=none` はエラーである。

同一 q で Szz と Sperp を測定し、SU(2) consistency も保存する例:

```text
szz_q=all
szz_file=szz.tsv
sperp_q=all
sperp_file=sperp.tsv
spin_consistency_file=spin_consistency.tsv
```

`spin_consistency_file` を有効にするには両 observable が enabled で、`nq,mx,my` の
順序まで一致する必要がある。有効な `profile_file`、`replica_log`、Szz、Sperp、
consistency output のパスが文字列として同一なら setup error とする。空の
`profile_file` と `replica_log` には、それぞれ有効時の既定値 `profile.csv` と
`replicas.csv` を適用してから検査する。v1 は `./a.tsv` と `a.tsv` の
canonical-path 同一性までは判定しない。

selector が文字列として同一なら immutable momentum plan と phase table を共有する。
selector が異なる測定も可能だが consistency output は使えない。

## Output

`sperp_file` は次の long-form TSV schema を持つ。

```text
beta_requested  beta  T  q_index  mx  my  qx_over_pi  qy_over_pi  qx_folded_over_pi  qy_folded_over_pi  Sperp  dSperp
```

`Sperp` は sign-reweighted replica/bin estimate の平均、`dSperp` は bin jackknife error。
`sperp_q=all` では各 bin について

$$
\sum_{\mathbf q}S_\perp(\mathbf q)
=\frac12\left(N_e-2ND\right)
$$

を検査する。

`spin_consistency_file` は同じ momentum metadata と `DeltaSU2,dDeltaSU2` を出力する。
各 bin で先に

$$
\Delta_{\mathrm{SU2},b}(\mathbf q)
=\frac12S_{\perp,b}(\mathbf q)-S^{zz}_b(\mathbf q)
$$

を作り、その配列を paired jackknife する。Szz と Sperp の別々の error の二乗和では
ないため、両者の covariance が保存される。

## Example and Tests

```sh
make dqmc
./dqmc input/1d_L4_U0_spin_all.txt

make test_sperp
OMPI_CC=clang make test_sperp_parallel
```

`test_sperp_parallel` は serial、OpenMP、MPI 2/4 ranks、hybrid を同一 seed で比較し、
`nranks>nrep` と Szz/Sperp で異なる q 幅も検査する。

## Cost and Statistical Notes

- 両 observable を有効にした measurement は ordered `(i,j)` loop を一度だけ走査し、
  Fourier contraction だけを各 selector ごとに行う。追加 RNG 呼び出しはない。
  profiler はこの joint path を `measure_spin`、単独 path を `measure_szz` または
  `measure_sperp` として記録する。
- phase table は plan ごとに `8*N*nq` bytes。同一 selector なら共有する。
- root の gathered numerator と q-major bin ratio は observable ごとに概ね
  `16*nrep*nbin*nq` bytes。32x32、`nrep=120`、`nbin=100`、両方 all-q なら
  合計約394 MBである。
- 大規模格子で selector が異なる場合は少なくとも一方を selected q にする。
- Szz と同様、低温 AF 成分は長い自己相関を持ち得る。ただし `dSperp` は bin-width
  plateau だけでは較正できない。`chain L=4, U=8, beta=2, dtau=0.2, nrep=8,
  nmeas=20000` の24独立 seed 測定では、run 内の pooled-bin jackknife error と
  seed 間の empirical standard error の比が、bin size 200/1000/5000 と q にわたり
  0.75--0.88だった。この一条件から一般的な補正係数は定められない。
- 論文品質の誤差には、bin-width plateau の確認に加えて基底 seed を変えた独立 run を
  複数実行し、run 間の散らばりから標準誤差を評価する。上記条件では
  `dDeltaSU2` のseed間標準誤差/報告値は q ごとに 1.06、1.08、0.85
  （平均1.00）だったが、これも条件限定の較正結果である。
