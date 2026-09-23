---
date: 2026-08-22
datetime: 2026-08-22 17:44 JST
model: Codex (GPT-5)
status: validation
topic: equal-time transverse spin structure factor Sperp(q)
summary: |
  S_perp estimator の独立 oracle、sum rule、並列整合性、後方互換性を記録する。
  有限 dtau での ensemble SU(2) 恒等式を厳密列挙と相互作用系MCで検証する。
---

# Sperp Structure Factor Validation

## Scope

- branch: `feat/sperp-structure-factor`
- local platform: macOS / Accelerate BLAS/LAPACK
- remote/HPC job: 未実行（今回の goal の対象外）
- pre-implementation baseline: `/tmp/afqmc_sperp_baseline.bJagm5`
- Open MPI wrapper workaround: configure 時の `gcc-15` が無いため `OMPI_CC=clang`

## Deterministic Physics Tests

`tests/test_structure_factor.c` は production estimator と独立な literal `(q,i,j)` oracle
を使い、dense asymmetric $G_\uparrow\ne G_\downarrow$ の実空間各 pair と Fourier 出力を
照合する。これにより spin pairing、係数、Kronecker delta、Green index reversal を検出する。

追加の解析状態:

- $G_\uparrow=G_\downarrow=0.5I$: 全 q で `Sperp=1/4`、`Sperp=2*Szz`。
- fully polarized / Néel product state: 全 q で `Sperp=1/2`。
- spin swap invariance、$q\leftrightarrow-q$、imaginary part cancellation。
- all-q sum rule $\sum_qS_\perp=(N_e-2ND)/2$。
- combined Szz/Sperp path と個別 path の一致。
- NULL、shape mismatch、non-finite input の rejection。

`tests/test_integration.c` は U=0 chain/square DQMC Green に対し、独立な free-fermion
formula の2倍と Sperp を照合する。`tests/test_ph_symmetry.c` は direct down Green と
PH-mapped down Green の全 q Sperp が一致することを確認する。

## Interacting Ensemble SU(2) Validation

離散HS変換は各 time slice で厳密な恒等式であり、補助場をすべて和すると
$\prod_l(e^{-\Delta\tau K}e^{-\Delta\tau V})$ が得られる。$K$ と $V$ はともに
SU(2) invariant なので、個々のspin-channel HS配置は z 軸を選んでも、ensemble では
任意の `dtau` で $S_\perp=2S^{zz}$ が成立する。

production と同じ行列規約で全HS配置を独立に厳密列挙した結果:

| system | configurations | U | dtau | $\max_q\lvert S_\perp-2S^{zz}\rvert$ |
| --- | ---: | ---: | ---: | ---: |
| 2-site chain, $L_\tau=3$ | 64 | 4 | 0.4 | 2.2e-16 |
| 2-site chain, $L_\tau=3$ | 64 | 4 | 0.05 | 2.2e-16 |
| 2-site chain, $L_\tau=3$ | 64 | 8 | 0.4 | 5.6e-17 |
| 2-site chain, $L_\tau=3$ | 64 | 8 | 0.05 | 5.6e-17 |
| 4-site chain PBC, $L_\tau=2$ | 256 | 8 | 0.4 | 6.1e-16 |
| 4-site chain PBC, $L_\tau=2$ | 256 | 8 | 0.1 | 3.9e-16 |

相互作用系MCでは `chain L=4, U=8, beta=2, dtau=0.2, nrep=8, nbin=20,
nmeas=20000` を24個の独立 seed で実行した。seed 間の結果は次のとおりで、全 q が
0から1.27標準誤差以内だった。

| q | $\overline{\Delta_\mathrm{SU2}}$ | seed 間 s.e.m. | 0からの隔たり |
| ---: | ---: | ---: | ---: |
| 0 | -4.3e-05 | 1.1e-03 | 0.04 sigma |
| 1 | +9.0e-04 | 9.6e-04 | 0.94 sigma |
| 2 | -1.8e-03 | 1.4e-03 | 1.27 sigma |

`dtau=0.05` でも `DeltaSU2` に `dtau` 依存の傾向は見られなかった。したがって
`DeltaSU2` はTrotter誤差ではなく、実装と統計の健全性を調べる診断として扱う。

## Accumulation, Output, and Parallel Integrity

- scalar、Szz、Sperp を全検査後に transactionally bin 更新する。
- disabled/enabled の `nq/pointer` invariant と half-enabled rejection を unit test する。
- MPI は observable ごとに独立した dynamic-width `MPI_Gatherv` を使い、root receive
  buffer を `NAN` 初期化して全 slot の finite overwrite を確認する。
- `tests/test_sperp_output.sh` は schema、Szz byte compatibility、scalar row compatibility、
  paired DeltaSU2、joint profiler region、file/selector failure、profile/replica logを含む
  active output path collision を検査する。
- `tests/test_sperp_parallel.sh` は serial/OpenMP/MPI/hybrid の固定 seed 一致、
  MPI 4 ranks / 3 replicas、component ごとに異なる q 幅を検査する。

## Backward Compatibility

実装前に保存した次の出力と実装後の出力を `cmp` した。

```text
input/1d_L4_U0.txt                  stdout: byte-identical
input/1d_L4_U0_szz_all.txt          stdout: byte-identical
input/1d_L4_U0_szz_all.txt          szz.tsv: byte-identical
```

両 observable を同時に有効にした固定 seed run でも、Szz-only の `szz.tsv` は
byte-identical、scalar data rows は byte-identical である。

## Test Record

実装中に確認済み:

```text
make test                         -> ALL TESTS PASSED
make test_sperp                   -> OK
make test_omp                     -> ALL OMP TESTS PASSED
OMPI_CC=clang make test_mpi       -> ALL MPI TESTS PASSED
OMPI_CC=clang make test_hybrid    -> ALL HYBRID TESTS PASSED
make test_slow                    -> ALL SLOW TESTS PASSED
OMPI_CC=clang make test_sperp_parallel -> OK
```

ASan/UBSan (`-fsanitize=address,undefined`) では、focused structure-factor unit test と
`input/1d_L4_U0_spin_all.txt` の full serial run を実行し、sanitizer error は無かった。
`git diff --check` も成功した。

8x8 square、`U=4,beta=1,nwarm=10,nmeas=100`、Szz/Sperp all-q の短い local
profiler sample では、`dqmc_sweep=0.157482 s` に対し one-pass joint measurement は
`measure_spin=0.003983 s`（beta wall の約2.09%）だった。短時間計測なので性能保証値では
ないが、measurement hot path に allocation/trigonometric call が無く、sweep に対して小さい
ことを確認した。
