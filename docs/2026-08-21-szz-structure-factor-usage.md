---
date: 2026-08-21
datetime: 2026-08-21 23:13 JST
model: Codex (GPT-5)
status: current
topic: equal-time longitudinal spin structure factor Szz(q) usage
summary: |
  AF_QMC の等時刻 longitudinal spin structure factor S^{zz}(q) 機能について、
  入力 selector、有限格子 momentum、TSV schema、規格化、parallel 実行、制約、
  all-q 測定のメモリ見積もりを利用者向けにまとめる。
---

# Szz Structure Factor Usage

## 1. Definition

出力する量は full（非 connected）の縦スピン構造因子

$$
S^{zz}(\mathbf q)=\frac1N\sum_{i,j}
e^{-i\mathbf q\cdot(\mathbf r_i-\mathbf r_j)}
\langle S_i^zS_j^z\rangle,
\qquad S_i^z=\frac12(n_{i\uparrow}-n_{i\downarrow})
$$

である。factor 3 は適用しない。SU(2) 対称系で
$S(\mathbf q)=3S^{zz}(\mathbf q)$ が必要なら後処理で 3 倍する。
反強磁性 order parameter のサイズ解析には TSV の
`Szz(Q_AF)/N` を使う。

## 2. Input

既定は無効であり、従来 input の挙動と stdout は変わらない。

```text
szz_q=none
szz_file=szz.tsv
```

有効な selector:

```text
# active な各方向の q=pi
szz_q=af

# finite-size Fourier grid の全 Lx*Ly 点
szz_q=all

# momentum index の明示リスト（出力順もこの順）
szz_q=0:0,2:0,2:2
```

momentum index と波数の対応は

$$
q_x=2\pi m_x/L_x,\qquad q_y=2\pi m_y/L_y
$$

である。`all` は `my` outer、`mx` inner の順になる。

制約:

- canonical index `0<=mx<Lx`, `0<=my<Ly` を使う。
- chain は `my=0` のみ。
- `af` は長さ 1 より大きい active direction が偶数でなければならない。
  長さ 1 の方向は inactive として `m=0` を使う。
- duplicate、負 index、範囲外、malformed token はエラー。
- explicit list は selector 全体で最大 255 文字。多数の q には `all` を使う。
- `szz_q` と `szz_file` の内部空白は許可しない。
- `szz_file=none` は無効化指定ではなくエラー。無効化は `szz_q=none` を使う。
- `lattice=file` は site coordinates を持たないため Szz を有効化できない。

## 3. Output

Szz は既存 stdout ではなく `szz_file` の long-form TSV に出力する。
Szz 有効時も stdout の scalar data columns は変わらない。

```text
beta_requested  beta  T  q_index  mx  my  qx_over_pi  qy_over_pi  qx_folded_over_pi  qy_folded_over_pi  Szz  dSzz
```

- `beta_requested`: input の `beta_list`。
- `beta`: 実際に使った `Ltr*dtau`。
- `T`: effective beta の逆数。
- `q_index`: selector 内の 0-origin index。
- raw q columns: canonical index に対応する $[0,2\pi)$ 表現。
- folded q columns: $(-\pi,\pi]$ 表現。例えば `Lx=4,mx=3` は `-0.5`。
- `Szz`: sign-reweighted bin estimate の平均。
- `dSzz`: 現行 scalar observables と同じ bin jackknife error。

double columns は `%.17g` で保存する。各 beta の全行を書いた直後に flush する。

`szz_q=all` では各 bin に対して

$$
\sum_{\mathbf q}S^{zz}(\mathbf q)
=\frac14\left(N_e-2N D\right)
$$

を検査し、破れた run はエラー終了する。

## 4. Examples

serial:

```sh
make dqmc
./dqmc input/1d_L4_U0_szz_all.txt
```

OpenMP:

```sh
make dqmc_omp
OMP_NUM_THREADS=2 ./dqmc_omp input/1d_L4_U0_szz_all_omp.txt
```

MPI/hybrid:

```sh
make dqmc_mpi dqmc_hybrid
mpirun -np 2 ./dqmc_mpi input/1d_L4_U0_szz_all_mpi.txt
OMP_NUM_THREADS=2 mpirun -np 2 ./dqmc_hybrid \
  input/1d_L4_U0_szz_all_hybrid.txt
```

機能の focused integration tests:

```sh
./tests/test_szz_output.sh
./tests/test_szz_parallel.sh
```

## 5. Statistical and Boundary Notes

- $S^{zz}(\mathbf Q_\mathrm{AF})$ はエネルギーより自己相関時間が長い場合がある。
  `dSzz` の bin-width plateau を確認し、未到達なら `nmeas` を増やす。
- OBC の q は並進対称性の量子数ではなく、site coordinate embedding 上の
  discrete Fourier sample である。
- phase table は rank あたり `8*N*nq` bytes を使う。`szz_q=all` では
  `nq=N` なので `8*N^2` bytes/rank（32x32: 約8 MB、48x48: 約42 MB、
  64x64: 約134 MB）になる。
- MPI/hybrid root はさらに二つの全-bin vector に約
  `16*nrep*nbin*nq` bytes を使う。例えば 32x32、`nrep=120`、`nbin=100`、
  `szz_q=all` では約197 MBである。大規模格子では selected q を優先する。
- equal-time transverse 成分は
  `docs/2026-08-22-sperp-structure-factor-usage.md` の `sperp_q` で測定できる。
  connected、imaginary-time、dynamic structure factor、任意実数 q は含まない。
