---
date: 2026-06-26
datetime: 2026-06-26 10:08 JST
model: Codex (GPT-5)
summary: |
  3日間の有限温度 DQMC ハッカソン成果をまとめた。
  Hubbard 模型の意義、AF-QMC の概要、実装、レビュー、検証、並列化、
  FullDiag benchmark の achievement と最終発表用 1-2 枚スライド案を整理した。
---

# Finite-T DQMC Hackathon Achievements

## 3日間の成果

有限温度 Hubbard 模型向けの DQMC/BSS コードを、設計から実装、検証、並列化、ベンチマークまで一通り到達した。

主な achievement:

- C 実装の有限温度 DQMC/BSS コードを構築。
- UDV 安定化、rank-1 Green 更新、wrap、測定、jackknife、温度 scan を実装。
- 出力物理量:
  - `E_hub`
  - `E_gc`
  - `E_ph`
  - `N`
  - `doublon`
  - `sign`
- `profile=1` による CSV profiler 出力を実装。
- replica 並列化を実装。
  - serial replica
  - OpenMP replica
  - MPI replica
- MPI では rank 数非依存 seed、global replica id 順集約、failure path の hang 回避を確認。
- FullDiag reference と比較。
  - `L=4,6,8`
  - `U/t=4`
  - `mu=U/2`
  - energy と doublon の温度依存性が ED とよく一致。

## 数字で見た成果

```text
commits:        81
src:            27 files / 2879 lines   (実装コードの総行数)
tests:          19 files / 1214 lines   (テストコードの総行数)
review docs:    16
data files:     100
```

注: "src LOC" = ソース(.c/.h)の総行数、"test LOC" = テスト(`tests/`)の総行数。
いずれも「何行のコードか」という規模指標。

MPI strong scaling:

```text
Condition: L=6, U=4, beta=4, nrep=8

np   speedup
1    1.00
2    1.93
4    2.83
8    4.15
```

FullDiag benchmark:

```text
Condition: 1D PBC chain, U/t=4, mu=U/2, dtau=0.05

L   max |diff E_hub/L|   max |diff doublon|
4   0.006336             0.001269
6   0.005809             0.001467
8   0.006306             0.000857
```

## 重要だった進め方 — 成功の2つの鍵

短期間で「それっぽい DQMC」ではなく**式と規約に裏付けられた DQMC** に到達できたのは、次の2点が効いた。

### 鍵1: 手法の詳細が載った博士論文を最初に用意した

大塚博士論文 付録 A という詳細な手法 reference を手元に揃えていたため、実装の各ステップを推測でなく**式と規約に照らして一つずつ確認**できた。

確認できた点:

- Hubbard-Stratonovich 変換。
- Green 関数定義。
- determinant ratio / rank-1 update。
- wrapping / UDV stabilization。
- energy / doublon measurement。

### 鍵2: Codex ⇔ Claude のレビュー往復で質の高い設計書を作った

設計書・実装計画・実装を **Codex と Claude の間で何度も往復レビュー**し、各段階を別モデルが批判的に検証することで、QMC 特有の危ない点を**実装前に**潰せた。

実装前に潰した落とし穴:

- `mu` の二重カウント。
- Green 関数規約の取り違え。
- `E_hub`, `E_gc`, `E_ph` の定義ズレ。
- ED との ensemble/energy 定義のズレ。
- odd PBC や 2x2 PBC の格子規約。
- MPI collective hang / rank0-only failure path。

→ 16 本の review docs が、この往復レビューの記録。

## スライド案 1: Hubbard Model and AF-QMC

Title:

```text
Hubbard Model and Finite-T Auxiliary-Field QMC
```

Main message:

```text
Hubbard model は電子相関を記述する最小模型。
有限温度 AF-QMC は、その熱力学量を非摂動的に計算する標準手法。
```

Layout:

- Left: Hubbard model の定義と意義
- Right: AF-QMC の計算フロー

Hubbard model:

```text
H = -t sum_<ij>,sigma (c^dag_i,sigma c_j,sigma + h.c.)
    + U sum_i n_i,up n_i,down
    - mu sum_i,sigma n_i,sigma

This work:
  1D Hubbard chain
  U/t = 4
  half filling: mu = U/2
```

Why it matters:

```text
Minimal model of correlated electrons
Captures competition between hopping and onsite repulsion
Prototype for Mott physics, magnetism, and strongly correlated materials
Small systems have exact diagonalization references for validation
```

AF-QMC overview:

```text
1. Trotter decomposition:
     exp[-beta H] -> product of short imaginary-time slices

2. Hubbard-Stratonovich transformation:
     interaction term -> auxiliary Ising fields

3. Monte Carlo sampling:
     sample auxiliary-field configurations with determinant weight

4. Measurements:
     compute Green function, energy, density, doublon

5. Stabilization:
     use UDV/QR-like stabilization for low temperature
```

Key point:

```text
At half filling on a bipartite lattice, sign problem is absent.
This makes it an ideal first benchmark for a new DQMC implementation.
```

## スライド案 2: Implementation, Validation, Scaling

Title:

```text
Finite-T DQMC from Scratch: Implementation, Validation, Parallelism
```

Main message:

```text
3日間で finite-temperature DQMC code を C で実装し、
FullDiag と比較して doublon の温度依存性が ED と一致することを確認。
OpenMP/MPI replica 並列も実装した。
```

Left: implementation achievements

```text
Implemented
  BSS/DQMC core
  UDV stabilization
  rank-1 Green update
  wrapping
  energy / doublon / sign
  jackknife binning
  CSV profiler

Parallelism
  serial / OpenMP / MPI replica
  (MPI 並列も実装済み)
```

Keys（強調したい2点）:

```text
Keys to quality
  1. 手法詳細の博士論文 (大塚 付録A) を用意
       -> 実装を式・規約に照らして検証
  2. Codex <-> Claude のレビュー往復
       -> 実装前に QMC 特有の落とし穴を排除
          (mu 二重カウント / Green 規約 /
           energy 定義 / MPI hang ...)
       -> 16 review docs が記録
```

Center: FullDiag validation — doublon(T) 図

```text
[Figure] doublon の温度依存性: DQMC vs FullDiag
  data/benchmark_L468_U4_full_diag_20260626/
    qmc_vs_fulldiag_doublon.png

  1D PBC Hubbard chain, U/t=4, mu=U/2
  L = 4, 6, 8, dtau = 0.05
  ED 曲線 と DQMC 点がよく一致 (max |dD| ~ 0.0015)
```

Right: numbers

```text
81 commits
2879 src LOC
1214 test LOC
16 reviews
serial / OpenMP / MPI replica parallel
```

Bottom: next steps

```text
dtau^2 -> 0 extrapolation for L=4,6,8
longer statistics
MPI+OpenMP hybrid
specific heat estimator
larger benchmark runs
```

## 1枚に圧縮する場合

1枚だけにするなら、以下の構成がよい。

Title:

```text
Finite-T AF-QMC for the Hubbard Model: Built and Validated in 3 Days
```

Four compact blocks:

```text
1. Physics target
   Hubbard model:
     hopping t vs onsite repulsion U
   U/t=4, half filling, 1D chains
   E(T), doublon(T)

2. Method
   Auxiliary-field QMC
   Trotter decomposition
   Hubbard-Stratonovich fields
   determinant weight
   stabilized Green functions

3. Built
   BSS/DQMC in C
   UDV stabilization
   Green update
   energy/doublon/sign
   OpenMP/MPI replica parallel

4. Validated
   FullDiag L=4,6,8
   doublon(T) matches ED
   max |dD| ~ 0.0015
   max |dE/L| ~ 0.006
```

Keys:

```text
1. 手法詳細の博士論文 (大塚 付録A) を用意
2. Codex <-> Claude のレビュー往復で高品質な設計書
```

Footer:

```text
81 commits / 2879 src LOC / 1214 test LOC / 16 reviews
```
