---
date: 2026-08-21
datetime: 2026-08-21 23:16 JST
model: Codex (GPT-5)
status: validation
topic: equal-time longitudinal spin structure factor Szz(q)
summary: |
  S^{zz}(q) 実装について、algebraic oracle、U=0 独立式、PH mapping、sum rule、
  sign/bin、TSV、serial/OpenMP/MPI/hybrid、後方互換、bin 幅、local performance、
  sanitizer の検証結果を記録する。実装レビュー後の rectangular oracle と
  shell-test 自動実行も追記した。
---

# Szz Structure Factor Validation

## 1. Environment and Scope

- branch: `feat/szz-structure-factor`
- local platform: macOS / Accelerate BLAS/LAPACK
- date: 2026-08-21 JST
- remote/HPC job: 未実行（この検証では許可対象外）
- baseline directory: `/tmp/afqmc-szz-baseline.mGuRaE`

ローカル Open MPI wrapper は configure 時の `gcc-15` を参照していたため、build 時は
`OMPI_CC=cc` を指定した。これは source failure ではなく toolchain のローカル設定である。

## 2. Baseline and Compatibility

実装前に `make test` を実行し `ALL TESTS PASSED` を確認した。次の stdout を保存した。

```text
input/1d_L4_U0.txt
input/1d_L4_U4.txt
```

実装後に同じ binary/input を再実行し、両 stdout の unified diff が空であることを
確認した。`szz_q=none` では `szz.tsv` が作成されないことも
`tests/test_szz_output.sh` で確認した。

interacting chain (`L=4,U=4,beta=2`, fixed seed) では Szz の on/off で scalar data row
が完全一致した。従って Szz branch は RNG、field、Green、scalar measurement を
変更していない。

## 3. Algebraic and Physics Tests

`tests/test_structure_factor.c`:

- $G_\uparrow=G_\downarrow=\tfrac12I$: 全 q で `Szz=1/8`。
- fully polarized product state: `Szz(0)=N/4`、他 q は 0。
- Néel product state: `Szz(Q_AF)=N/4`、`Szz(0)=0`。
- deterministic dense Green: direct `q,i,j` oracle と displacement estimator が一致。
- 2x3 rectangular square の全6 qを direct oracle と照合し、`Lx!=Ly` の
  displacement/index mapping を unit test で固定した。
- negative displacement を条件加算で canonical 化し、負 index を作らない。
- $C^{zz}_{ij}=C^{zz}_{ji}$ と sine-weighted imaginary part の消失を直接確認。
- chain の nontrivial pair で `Szz(q)=Szz(-q)`。
- 全 q sum rule が onsite moment と一致。

`tests/test_integration.c`:

- periodic chain と square の U=0 DQMC Green を、独立な momentum-space formula

$$
S^{zz}(q)=\frac1{2N}\sum_k f_k(1-f_{k+q})
$$

  と照合した。
- square は異なる2 seed で同じ式に一致し、U=0 の field/seed independence を確認した。

`tests/test_ph_symmetry.c`:

- chain/square の全 q で、直接構築した down-spin Green と PH-mapped Green の Szz が
  一致した。

実 DQMC の `szz_q=all` run は serial/OpenMP/MPI/hybrid の root finalization で
bin-wise local-moment sum rule を通過した。

## 4. Statistics and Output

- sign=-1 を含む synthetic samples で numerator/denominator ratio を検証した。
- vector API は caller `nq` と result shape の不一致を拒否する。
- scalar と Szz の accumulator は、全値を検証してから transactionally 更新する。
- output script で 12 columns、q ordering、folded q、requested/effective beta、finite 値、
  `szz_file=none` error、internal-whitespace error を確認した。
- ratio failure と sum-rule failure は別の診断になり、sum rule は
  `lhs`、`rhs`、`diff`、`tol`、`sum_sign` を出力する。

bin-width check (`chain L=4,U=4,beta=2,nwarm=200,nmeas=800,seed=246813579`):

| nbin | bin size | Szz(AF) | dSzz(AF) |
| ---: | ---: | ---: | ---: |
| 10 | 80 | 0.33725572734897652 | 0.012238744067016234 |
| 20 | 40 | 0.33725572734897658 | 0.015035056742551515 |
| 40 | 20 | 0.33725572734897663 | 0.014639491824923693 |

この短い run では誤差推定自身が約10--25%ばらつき得るため、20/40 bins の値が
約2.7%内でも plateau 到達の根拠にはならない。production 条件では測定数を増やし、
同一測定列を複数の bin size に再 binning して lattice/beta ごとに判定する。

## 5. Parallel and MPI Integrity

`tests/test_mpi_replica.c` は次を検証する。

- q-fastest `[replica][bin][q]` pack ordering。
- zero local replicas / disabled `nq=0`。
- MPI `int` count/displacement overflow rejection。
- scalar fixed-width pack は `REPLICA_MPI_BIN_DOUBLES=8` のまま。

runtime の Szz receive buffer は root で全要素を `NAN` に初期化する。
Gatherv 後に全 slot の finite overwrite を検査し、`all` ではさらに bin-wise sum rule を
検査する。物理的に正当な all-zero vector は拒否しない。

`tests/test_szz_parallel.sh` で次を確認した。

- OpenMP 2 threads。
- MPI 2 ranks。
- MPI 4 ranks / 3 replicas (`nranks>nrep`)。
- hybrid 2 ranks x 2 threads。
- all-q の data rows と scalar row が上記4形態で一致。
- selected-q を MPI 4 ranks / 3 replicas で完走。

root の `szz_file` open failure を MPI 2 ranks で発生させ、全 rank が hang せず
exit status 1 で終了することも確認した。

## 6. Test Matrix

成功した command:

```text
make test                         -> ALL TESTS PASSED
make test_omp                     -> ALL OMP TESTS PASSED
OMPI_CC=cc make test_mpi          -> ALL MPI TESTS PASSED
OMPI_CC=cc make test_hybrid       -> ALL HYBRID TESTS PASSED
make test_slow                    -> ALL SLOW TESTS PASSED
./tests/test_szz_output.sh        -> OK
./tests/test_szz_parallel.sh      -> OK
```

`make test` は `test_szz` を通じて serial output script を、`make test_hybrid` は
`test_szz_parallel` を通じて cross-mode script を自動実行する。

ASan/UBSan build (`-fsanitize=address,undefined`) で enabled U=0 all-q run を実行し、
sanitizer error は無かった。macOS の当該 ASan runtime は leak detection 非対応という
notice を出したため、leak sanitizer の結果は含めない。

## 7. Local Performance Sample

`square 8x8,U=4,beta=1,nwarm=10,nmeas=100` の serial profiler:

| selector | beta_total [s] | measure_szz calls | measure_szz total [s] |
| --- | ---: | ---: | ---: |
| none | 0.4051470002 | 0 | 0 |
| af | 0.4999759998 | 100 | 0.0018550041 |
| all | 0.3247239999 | 100 | 0.0018320009 |

短時間 run なので wall-time ordering は noise を含む。重要な確認点は、`measure_szz`
が enabled 時だけ100回記録され、hot path allocation 無しで全 q も estimator 設計の
$O(N^2+NN_q)$ 経路を使ったことである。性能値は採否 gate にはしない。

## 8. Remaining Production Validation

- interacting finite-temperature ED との定量比較と dtau 外挿は、本実装の deterministic
  unit/U=0 oracle とは別の production physics validation として残す。
- L=16 以上の memory/wall-time 測定、kugui/Genkai の multi-rank validation は
  remote 実行許可を得た後に行う。
