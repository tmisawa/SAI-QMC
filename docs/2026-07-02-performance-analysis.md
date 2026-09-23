---
date: 2026-07-02
datetime: 2026-07-02 15:10 JST
revised: 2026-07-02 15:35 JST (レビュー反映: §3(1)・§4 の現行見積もりを stab=4/stab=2 に分離)
model: Claude Fable 5
summary: |
  16×16 (n=256) 対応に向けた性能分析。実測プロファイル（Mac/Accelerate, serial）で
  sweep 時間の 80–87% が green_from_scratch（実体は udv_lmul の QR 連鎖）と特定。
  コストは O(n³L²/stab)/sweep で、kugui 実測の「β²則」と整合。最優先は UDV 部分積
  スタック導入（O(L²/stab)→O(L)、β=20 級で 1〜2桁短縮）。次いで delayed update、
  半充填 PH 対称性による down スピン省略、レプリカ MPI × threaded BLAS ハイブリッド。
---

# 16×16 に向けた DQMC 性能分析と高速化計画

## 1. 実測プロファイル

環境: macOS (Apple Silicon) / Accelerate / serial ビルド。`profile=1` の計測。

### 8×8 (n=64), U=4, dtau=0.1, β=4 (L=40), stab=8

measurement フェーズ 40 sweep、1 sweep ≈ 44 ms:

| region | total | 割合 |
|---|---|---|
| green_from_scratch | 1.523 s | **87%**（うち udv_lmul 1.412 s） |
| green_update | 0.156 s | 9% |
| green_wrap | 0.069 s | 4% |
| measure_sample | 0.0005 s | 無視できる |

### 16×16 (n=256), 同条件

1 sweep ≈ 1.19 s（8×8 の約 27 倍 ≒ n³ スケール）:

| region | total/4sweep | 割合 | 1回あたり |
|---|---|---|---|
| green_from_scratch | 3.806 s | **80%** | 79 ms/call |
| udv_lmul | 3.638 s | (76%) | 1.9 ms/call |
| green_update | 0.785 s | 16% | 14 µs/call |
| green_wrap | 0.169 s | 3.5% | 0.53 ms/call |

## 2. ボトルネックの構造

1 sweep あたりの呼び出し構造（現行実装）:

- `green_from_scratch`: `(L/stab + 1) × 2スピン` 回。**毎回 L 枚全部の B を
  `udv_lmul`（QR 1回 + gemm 2–3回、各 O(n³)）で掛け直す** →
  **O(n³ · L²/stab) / sweep**。これが支配項。
- `green_update`: ランク1更新 O(n²) × 提案 n·L·2 回 → O(n³L)。
- `green_wrap`: gemm 2回 × L × 2スピン → O(n³L)。

L² 依存が kugui 実測（2d4: β=16 で 5.9h、β²則）と一致する。つまり現在の
低温側の実行時間爆発は物理ではなく**安定化アルゴリズムの再計算コスト**。
β=20, dtau=0.025 (L=800, stab=4) では 1 sweep に QR が
2×(800/4)×800 = 32万回走っている。

## 3. 高速化案（優先順）

### (1) UDV 部分積スタック — 最優先、アルゴリズム改善

QUEST / ALF 等の標準実装が使う手法。sweep 前に右側部分積
B_{L-1}…B_{l} の UDV を stab 境界ごとにスタックへ保存しておき、
安定化時は「新しい stab 枚ぶんを左積に足す + スタック上の右積と結合」
だけにする。

- QR 回数: 2L²/stab → 2L/stab（**L 分の1**）。gemm 回数: 2L²/stab → ~2L。
- sweep コスト: O(n³L²/stab) → **O(n³L)**。β²則 → β線形則。
- メモリ: (L/stab) × (U, D, T) × 2スピン。n=256, L=800, stab=4 で
  200 × 2n² × 8B × 2 ≈ 420 MB/replica。L=160 なら 84 MB — 問題なし
  （メモリが厳しい場合はスタック粒度を粗くして再計算と併用）。
- 期待効果: 16×16 β=8 (L=160) で sweep ~27–30s (stab=4) / ~50s (stab=2) → ~1.5–2s
  （**~15–30×**、Mac serial 外挿）。β=20, dtau=0.025 の 1D/2D 系ではさらに大きい。
- 副次効果: 安定化が安価になるので **stab=2 常用が現実的になる**
  （L4-U12 で起きた stab=8 汚染のような問題の恒久対策にもなる）。
- 変更範囲: `green.c` / `linalg.c` / `dqmc.c` / `profiler.c` / `field.c`。
  `test_green_init` / `test_sign_regression_slow` 等の既存テストに加え、
  UDV stack 専用テストで TDD 可能。目安 1–2 日。

### (2) Delayed update（遅延ランクk更新）

UDV スタック後は `green_update`（ランク1、メモリ帯域律速）が相対的に支配的
（~50%）になる。k=32–64 回ぶんの更新ベクトルを n×k 行列に溜めて dgemm で
一括適用する標準手法。比 R の計算に必要な対角要素だけ逐次更新する。
n=256 で 2–4× を期待。最低限の改善は u/v バッファの事前確保。
`dger_` 置換は BLAS/環境依存で、Mac/Accelerate の短い実測では手書きループ維持の方が速い。

### (3) 細かい実装改善（半日）

- **ホットパスの malloc/free 除去**: `green_update`（1 sweep に ~1.4万回
  malloc×2）、`green_wrap`、`udv_lmul`、`udv_inv_one_plus` が毎回確保している
  作業領域を `Green` / 共有 `LinalgWork` に持たせる（`UDV` は stack 保存のため
  factor-only を維持）。
- `green_build_B` の `exp()`: s=±1 の2値しかないので事前計算した2値を引く。
- `la_inverse`（dgetrf+dgetri）→ 右辺が既知の箇所は dgesv で solve に。

### (4) 半充填の粒子正孔対称性で down スピンを省略（2×）

二部格子・μ=U/2・λσs 型 HS では G↓ は G↑ から
G↓ = 1 − P G↑ᵀ P（P = 副格子パリティ (−1)^i）で厳密に得られる。
up だけ更新すれば全計算が半分になり、sign も恒等的に 1。
検証ラダー（ED/TPQ 照合、既存 two-spin 実装との一致）で確認してから採用。

### (5) 並列化

- **レプリカ MPI（実装済み）が主軸で正しい**。16×16 は統計誤差確保に
  多数レプリカが必要で、これは embarrassingly parallel。
- **ハイブリッド**: (1)(2) 後は時間のほぼ全てが n=256 の dgemm/dgeqrf に
  なるので、threaded BLAS（kugui なら MKL, `MKL_NUM_THREADS=2–4`）を
  レプリカ MPI と組むだけでコード変更なしに 2–3×。レプリカ OpenMP との
  ネスト過剰購読には注意（純 MPI レプリカ × MKL スレッドが安全）。
- **(β, replica) の2次元分割**: β リストを rank 間で分配する場合は
  コストが L²（スタック後は L）に比例する点で重み付けして負荷分散。
- GPU 化（delayed update は GPU 向き）は将来案。工数大なので当面見送り。

## 4. 見積もり: 16×16, U=4, β=8, dtau=0.05 (L=160)

（2026-07-02 レビュー反映: 現行値を stab=4 / stab=2 に分離。udv_lmul 実測
1.9ms/call からの外挿で、旧記載の「現行 ~50s」は stab=2 相当だった。）

| 段階 | 1 sweep (stab=4) | 1 sweep (stab=2) | 3300 sweep/replica (stab=4) |
|---|---|---|---|
| 現行 | ~27–30 s | ~50 s | ~25–27 h |
| + UDV スタック | ~1.5–2 s | ~2 s | ~1.4–1.8 h |
| + delayed update ほか | ~1.0 s | — | ~55 min |
| + PH 対称性 | ~0.5 s | — | ~27 min |

（Mac/Accelerate serial 1コアでの外挿。kugui/MKL は実装後に別途計測して記録する。
スタック後は stab 依存が QR 回数の線形差だけになるため stab=2 常用も低コスト。）
レプリカ 100 本の MPI で回せば十分実用的な計算になる。

## 5. 推奨着手順

1. UDV スタック（TDD、既存テスト活用）→ 効果測定（profile=1 で前後比較）
2. ホットパス malloc 除去 + exp キャッシュ（低リスク）
3. delayed update（または暫定 dger 化）
4. PH 対称性（検証ラダー通過後に採用判断）
5. kugui で threaded MKL ハイブリッドのスケーリング測定
