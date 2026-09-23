---
date: 2026-06-24
datetime: 2026-06-25 11:06 JST
model: Codex (GPT-5)
status: design
topic: 有限温度 補助場量子モンテカルロ (DQMC/BSS) の C 実装
summary: |
  半充填ハバード模型の有限温度 補助場 QMC (BSS/行列式 QMC) を C で実装する設計書。
  大塚博士論文 付録A に忠実な BSS 法、一般ホッピング行列、LAPACK/BLAS、E(T) を
  grand-canonical ED/TPQ と比較検証。Codex 第7回レビューを反映し、非可換積テスト、
  UDV stress test、入力 parser 厳格化などを計画に追加済み。
---

# 有限温度 補助場量子モンテカルロ (DQMC/BSS) 実装 設計書

- 日付: 2026-06-24（最終更新 2026-06-25, Codex 第7回レビュー反映）
- 言語: C (C11)
- リポジトリ: `AF_QMC/`
- 実装リファレンス: 大塚雄一 博士論文「格子上の電子系における乱れ及び相互作用の効果」付録A
  （`references/…[博士論文_200203].md`。式 A.1–A.135 は検証済み。`LOG.md` 参照）

## 1. 目的とゴール

ハバード模型に対する**有限温度・補助場（行列式/BSS）量子モンテカルロ法**を C 言語で実装する。
ゴールは、**小サイズクラスタにおけるエネルギーの温度依存性 E(T) を、外部の厳密対角化(ED)・TPQ の結果と突き合わせて検証する**こと。

### スコープ（確定事項）

- **実装するのは DQMC のみ**。ED・TPQ は外部ツール（HΦ 等）または別途計算した結果を用い、数値比較する。
- **対象は半充填（half-filling）二部格子**。二部格子・粒子正孔対称により符号問題が発生しない、最も素直な検証ターゲット。non-bipartite 格子は v1 では実行エラーにし、絶対符号の初期化を含む一般 sign 計算は将来拡張とする。
- **格子は柔軟に**：内部表現を一般ホッピング行列 `t_{ij}` に統一。組み込みジェネレータ（chain, square; PBC/OBC; t 指定）＋任意格子のファイル入力。
- **線形代数は LAPACK/BLAS**（macOS: Accelerate、Linux: OpenBLAS/LAPACK）。
- **アルゴリズムは古典的 BSS 法**（付録 A.2 に忠実）。近代的高速化（delayed/submatrix 更新、checkerboard 分解）は正しく動作後の拡張フックとしてのみ考慮（YAGNI）。

### 非スコープ（当面やらないこと）

- ドープ系（半充填以外）・符号問題のある領域。
- 非等時刻グリーン関数・動的物理量。
- 大規模系向け高速化（まず正しさ、次に速度）。

## 2. 物理定式化（参照式）

ハミルトニアン（QMC 形式、付録 A）:

- 運動項 + 化学ポテンシャル: `(K_σ)_{ij} = t_{ij} + δ_{ij}(w_i − μ)`  (A.9)  ※乱れ `w_i` は当面 0。
- 相互作用: `e^{−Δτ U n_{i↑} n_{i↓}}` を離散 Hubbard–Stratonovich 変換 (A.10):

  `e^{−ΔτU n↑n↓} = (1/2) Σ_{s=±1} exp[(λs − ΔτU/2) n↑] exp[(−λs − ΔτU/2) n↓]`,
  `cosh λ = exp(ΔτU/2)`  (A.11)

- 補助場ポテンシャル: `(−Δτ V_{lσ})_{ij} = δ_{ij}(λσ s_{il} − ΔτU/2)`  (A.22)
- B 行列: `B_{mσ} = e^{−Δτ K_σ} e^{−Δτ V_{mσ}}`,  `B_{mσ}^{−1} = e^{+Δτ V_{mσ}} e^{+Δτ K_σ}`  (A.109/110)
- トレース＝行列式: `Tr_F ∏_l D_l = det(I + ∏_l e^{−A_l})`  (A.35)
- 等時刻グリーン関数: `g_{mσ} = (I + B_{m−1σ}⋯B_{1σ} B_{Lσ}⋯B_{mσ})^{−1}`  (A.95)、`g_{ij} = ⟨c_i c_j†⟩`
- 密度: `⟨c_i† c_j⟩ = δ_{ij} − g_{ji}`  (A.56)

**半充填**: 非対称形 `U n↑n↓` を使うため `μ = U/2`（`LOG.md` の注意に従う）。
粒子正孔対称形 `U(n↑−½)(n↓−½)` を使う ED/TPQ と比較する際は、エネルギー原点の定数シフトと μ の扱いを揃える（§5 参照）。

## 3. モジュール構成

コアは格子を `exp(±Δτ K_σ)` という行列としてしか知らない、という境界で疎結合化する。

```
src/
  lattice.c/h   格子ジェネレータ → ホッピング行列 t_{ij} 生成
                (chain, square; PBC/OBC; +ファイル入力。二部格子の副格子符号も出力)
  model.c/h     Hubbard パラメータ (U, μ=U/2)、K_σ 行列の構築 (A.9)
  linalg.c/h    LAPACK/BLAS 薄いラッパ:
                expm(対称K→固有分解で行列指数), matmul, inverse, QR,
                UDV 安定化積, 行列式(符号 + log|det|)
  field.c/h     補助場 s_{il}∈{±1} の格納、λ (A.11)、N_{imσ}=e^{−2λσs}−1 (A.93)
  green.c/h     等時刻グリーン関数: 定義からの計算 (A.95)+UDV安定化、
                rank-1 更新 (A.107)、wrapping (A.108)
  dqmc.c/h      BSS メインループ: スイープ、受理判定 (A.99)、符号追跡、
                安定化スケジュール、warmup/measurement 相
  measure.c/h   E(T) 測定 (運動E A.56 + U⟨n↑n↓⟩)、二重占有、
                ビン平均とジャックナイフ誤差
  io.c/h        パラメータファイル読込、結果出力 (T, E_hub/dE_hub, E_gc/dE_gc,
                E_ph/dE_ph, ntot/dN, doublon/dD, sign)
  rng.c/h       再現可能な乱数 (seed 固定; xoshiro 等)
  main.c        パラメータ読込→格子/モデル構築→DQMC実行→出力
tests/          単体テスト (U=0 自由電子解析解、安定化残差、ED小サイズ照合)
input/          サンプル入力 (1D L=4 U=4 等)
Makefile        macOS=Accelerate / Linux=OpenBLAS 切替
```

各ユニットは責務が単一で、`green`/`dqmc` 以外は独立にテスト可能。

## 4. データフロー（BSS メインループ）

```
初期化:
  K_σ = t_{ij} + δ_{ij}(−μ)                         (A.9, half-filling μ=U/2)
  expK = expm(−Δτ K_σ), expK_inv = expm(+Δτ K_σ)    ← 一度だけ計算
  λ: cosh λ = exp(ΔτU/2)                             (A.11)
  s_{il} をランダム初期化、B 行列群を構成
  g_σ = (I + B_{Lσ}⋯B_{1σ})^{−1}                     (A.95) を UDV 安定化で計算

1スイープ (時間スライス l=1..L を掃く):
  for 各サイト i:
    R = |1+(1−g_{ii↑})N_{i↑}| · |1+(1−g_{ii↓})N_{i↓}|   (A.99)
    if メトロポリス受理(R):
      s_{il} 反転; g_σ を rank-1 更新 (A.107); 符号 ×= sign(R)
  次スライスへ wrapping: g_σ ← B_{lσ} g_σ B_{lσ}^{−1}    (A.108)
  一定間隔ごとに g_σ を定義から再計算して安定化 (UDV)

相構成:
  warmup スイープ × N_warm → 測定スイープ × N_meas
  測定スイープ毎に measure() を呼びビンに蓄積

出力: 各 T(=1/β) について `E_hub/E_gc/E_ph`, ジャックナイフ誤差, `ntot`, `doublon`, `sign`
温度スキャン: β を振る (Δτ 固定で L=β/Δτ 可変)
```

## 5. 測定式

グリーン関数規約 `g_{ij} = ⟨c_i c_j†⟩`、密度 `⟨c_i† c_j⟩ = δ_{ij} − g_{ji}` (A.56)。

- **運動エネルギー**（純ホッピング部、μ 項は分離）:
  `⟨K_hop⟩ = Σ_{ij,σ} t_{ij} (δ_{ij} − (g_σ)_{ji})`
- **相互作用エネルギー**: 各補助場配置で Wick 分解（同サイト同時刻、スピン独立）:
  `⟨n_{i↑} n_{i↓}⟩ = (1 − g_{ii↑})(1 − g_{ii↓})`、`⟨H_U⟩ = U Σ_i ⟨n_{i↑} n_{i↓}⟩`
- **全エネルギー**: `E = ⟨K_hop⟩ + U Σ_i (1−g_{ii↑})(1−g_{ii↓})`、符号付き平均 `⟨E·sign⟩/⟨sign⟩`（半充填では sign=1）。
- **二重占有**（副産物の検証量）: `d = (1/N) Σ_i ⟨n_{i↑} n_{i↓}⟩`

### ED/TPQ との整合（重要）

- 本実装の μ を含まない Hamiltonian を `H0 = K_hop + UΣ_i n_{i↑}n_{i↓}` と定義する。
  DQMC の grand-canonical 重みは `exp[-β(H0−μN)]`、半充填では `μ=U/2`。
- ED/TPQ が粒子正孔対称形 `U(n↑−½)(n↓−½)` の場合、エネルギー原点が定数シフトする。
  `U n↑n↓ = U(n↑−½)(n↓−½) + (U/2)(n↑+n↓) − U/4`。
- 比較時は**どちらの形式・どの定数・μ の扱い**で E を定義しているかを揃える。
  測定出力は両形式に変換できるよう、`⟨K_hop⟩`, `U·Σ⟨n↑n↓⟩`, `Σ⟨n↑+n↓⟩` を個別に出し、
  そこから `E_hub=⟨H0⟩`, `E_gc=⟨H0−μN⟩`, `E_ph=⟨H0 − (U/2)N + (U/4)n_site⟩` を再構成して出力する。
- **アンサンブルの整合（重要）**: DQMC は grand-canonical（μ=U/2）。半充填の粒子正孔対称点で
  ⟨N⟩=n_site にはなるが、固定 N の canonical ensemble と有限温度・有限サイズで一致するとは限らない。
  → **比較は grand-canonical ED を原則**とする（ED 側も同じ μ で grand-canonical trace を計算）。
  canonical TPQ/ED を使う場合は直接一致を主張せず、アンサンブル差があり得る検証項目として扱う。

## 6. 検証戦略（多層）

1. **U=0 自由電子**: `E(T) = Σ_{kσ} ε_k f(ε_k)`（解析解）と一致。線形代数・グリーン関数初期化のテスト。
2. **Δτ→0 外挿**: 固定 β で Δτ=0.05,0.1,0.2 を取り、Trotter 誤差が O(Δτ²) で消えることを確認。
3. **安定化テスト**: 低温で「rank-1 更新で転がした g」と「定義から再計算した g」の残差が許容内（`LOG.md` の Sherman–Morrison 残差 ~1e-15 と整合）。UDV は強いスケール分離を含む stress test も行う。
4. **本命: ED/TPQ 照合**: 1D L=4〜6、2D は小 OBC 系（2×2/2×3/2×4）で E(T) 曲線を grand-canonical ED と重ねる（2×2 PBC は二重結合のため使わない。4×4 は grand-canonical ED には大きすぎ TPQ 向け）。`dtau²` 外挿後に比較し、許容差は QMC 統計誤差＋外挿（Trotter 系統）誤差。U=2,4,8 をスキャン。
5. **符号チェック**: 半充填二部格子で `⟨sign⟩=1` を確認（崩れたら実装バグの検出器）。

## 7. 誤差評価

ビニング＋ジャックナイフ。自己相関を考慮しビン幅を可変に。各 T で `E_hub/E_gc/E_ph` の各 `E ± δE` を出力。

## 8. ビルド・実行

- **Makefile**: `uname` で macOS→`-framework Accelerate`、Linux→`-lopenblas -llapack` を自動切替。`-std=c11 -O2 -Wall`。
- **入力**: プレーンテキストの key=value
  （`lattice=chain`, `Lx=4`, `Ly=1`, `pbc=1`, `t=-1.0`, `U=4.0`,
   `dtau=0.1`, `beta_list=1,2,4,8`, `nwarm`, `nmeas`, `nbin`, `seed`, `stab`）。
   別名として `bc=periodic|open`、`stabilize_interval` も受理する（パーサで alias 処理）。
   `bc` は `periodic/open/pbc/obc/1/0` 以外をエラーにし、typo を OBC として黙って解釈しない。
   `lattice` は `chain|square|file` 以外をエラーにし、typo を chain として黙って解釈しない。
   未知 key と不正数値はエラーにし、`nmeas` は `nbin` で割り切れることを要求する。
   `lattice=file` の dense hopping は v1 では対称かつ対角ゼロに限定し、読み込み時に検証する。
- **出力**: `T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign` テーブル
  （`E_hub`=⟨H0⟩, `E_gc`=⟨H0−μN⟩, `E_ph`=粒子正孔対称形。H0=K_hop+Un↑n↓）。gnuplot/pandas で即プロット可。
- **実行例**: `./dqmc input/1d_L4_U4.txt > out.dat`

## 9. 実装上の注意（LOG.md 由来）

- グリーン関数規約は `g = ⟨c c†⟩`。密度は `δ − gᵀ`。
- 半充填は `μ = U/2`（非対称 `U n↑n↓` 形）。
- 低温（大 β）では UDV/QR 安定化が必須。素朴な B 行列積は破綻する。
- `N_{imσ} = e^{−2λσ s} − 1`（A.93）。
- 符号問題は半充填二部格子では発生しない。v1 は non-bipartite を実行エラーにするため `sign` は絶対符号として 1 に保たれる。
  non-bipartite を将来サポートする場合は、flip での相対符号だけでなく初期配置の `det(I+P↑)det(I+P↓)` の符号も計算する。
- 原論文 (A.103) 2 行目に符号タイポあり（正: `g' = g + (I−g)(I−Δ)g'`）。最終式 (A.107) は正しく、実装は (A.107) に従えば影響なし。

## 10. 実装順序（概要）

1. linalg ラッパ + expm + 行列式 + UDV（単体テスト）
2. lattice（chain/square）+ model（K_σ 構築）
3. green 初期化 (A.95) → U=0 自由電子 E(T) で検証
4. field + rank-1 更新 (A.107) + wrapping (A.108) + 安定化
5. dqmc メインループ + 受理判定 (A.99) + 符号追跡
6. measure（E, doublon）+ ジャックナイフ
7. ED/TPQ 照合（1D L=4 U=4 等）
8. ファイル入力格子・サンプル入力整備

詳細な実装計画は writing-plans スキルで別途作成する。
