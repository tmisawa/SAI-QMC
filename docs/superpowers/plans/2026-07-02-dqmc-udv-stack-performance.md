---
date: 2026-07-02
datetime: 2026-07-02 15:20 JST
revised: 2026-07-02 15:35 JST (独立レビュー反映)
model: Claude Fable 5
status: plan
topic: DQMC 高速化 — UDV 部分積スタックによる安定化コスト O(L²/stab)→O(L) 化 + ホットパス改善
summary: |
  16×16 (n=256) 対応のための高速化実装計画。実測プロファイル
  (docs/2026-07-02-performance-analysis.md) で sweep 時間の 80–87% が
  green_from_scratch（安定化のたびに L 枚全部を QR で掛け直す）と特定済み。
  本計画の主軸は UDV 部分積スタックの導入で、sweep コストを O(n³L²/stab) から
  O(n³L) に落とす（kugui で実測された「β²則」を β 線形化）。副次タスクとして
  ホットパスの malloc 除去・exp 2値キャッシュ・green_update の実測ベース改善を含む。
  delayed update（ランクk一括更新）と半充填 PH 対称性は別スコープとして後続に回す。
---

> **Revision note (2026-07-02 15:35 JST):** 独立レビュー
> [`docs/reviews/2026-07-02-dqmc-udv-stack-performance-plan-review.md`](../../reviews/2026-07-02-dqmc-udv-stack-performance-plan-review.md)
> の High×2 / Medium×3 / Low×2 を全て反映済み。主な変更:
> (1) block boundary を **`b[j] = min(j·stab, L)`, `M = ceil(L/stab)`** ベースに修正し、
> `L % stab != 0` の tail block と `L < stab` の意味論（末尾厳密化の維持）を明文化、
> (2) `UDV` は **factor-only を維持**し workspace は `LinalgWork` に分離
> （スタックのメモリ見積もりを守る）、
> (3) `la_inverse_work` / `la_logdet_work` を Task 1 に追加し
> `udv_inv_one_plus` 経路の hot allocation ゼロを完了条件化、
> (4) Task 2 の不変条件から「D の順序」を削除（現行 `udv_lmul` は非ソート）、
> 誤差判定を abs+rel / Frobenius に、
> (5) profiler region 4種を追加し Task 7 の判定を内訳ベースに変更、
> (6) 現行コスト見積もりを stab=4 (~27–30s) / stab=2 (~50s) に分離、
> (7) `field_N` の exp 2値キャッシュを Task 6 に追加、
> (8) Task 4/5 の必須テストケースに `L % stab != 0` / `L < stab` / `L == stab` /
> `stab=1` と brute-force det_sign 照合を追加。

# DQMC UDV スタック高速化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans または
> subagent-driven-development でタスク単位に実装する。各ステップは `- [ ]` チェックボックスで追跡する。

**Goal:** `dqmc_sweep` の安定化コストを O(n³·L²/stab) → O(n³·L) に落とし、
16×16 (n=256), β=8 (L=160) で 1 sweep を現行 ~27–30s (stab=4) / ~50s (stab=2) から
~1.5–2s へ短縮する（~15–30×。数値は Mac/Accelerate serial 外挿、kugui/MKL は別途計測）。
物理結果（E(T), sign, 検証ラダー）は統計誤差の範囲で不変であること。
stdout の出力フォーマットは変えない。

**Architecture:** 右側部分積 B(β,τ_k) の UDV を stab 境界ごとにスタックへ事前計算し、
sweep 中の安定化を「左積 UDV の1ブロック延長 + スタック右積との結合」に置き換える。
既存 `green_from_scratch` は初期化・リファレンス実装・クロスチェック用に温存する。

**Tech Stack:** C11, GNU Make, LAPACK/BLAS（`dgemm_`/`dgeqrf_`/`dorgqr_`）,
既存 `tests/test_*.c` standalone テスト方式。

**Reference:**
- 性能分析: `docs/2026-07-02-performance-analysis.md`（実測プロファイルと複雑度分析）
- 現行コード: `src/dqmc.c` (`dqmc_sweep`), `src/green.c`
  (`green_from_scratch`/`green_wrap`/`green_update`), `src/linalg.c` (`udv_*`)
- 実測整合: `LOG.md` 2026-07-02「2d4: β=16 実測 5.9h が β²則予測と一致」
  — この β² は本計画が除去する O(L²/stab) の署名。
- 手法: QUEST / ALF 等の標準的な ASGF スタック安定化（大塚博士論文 付録A の
  安定化枠組みと整合。新しい物理は導入しない）。

---

## Scope Check

この plan が扱うもの:
1. **UDV 部分積スタック**（主タスク、効果最大）
2. **ホットパスの malloc/free 除去**（`green_update`/`green_wrap`/`udv_lmul`/`udv_inv_one_plus`）
3. **`green_build_B` の exp() 2値キャッシュ**
4. **`green_update` の u/v malloc 除去**（`dger_` 置換は実測で速い場合のみ採用）

含めない（別スコープ・後続計画）:
- **Delayed update（ランクk一括更新）**: 効果 2–4× だが実装が重い。本計画の
  効果測定後に別 plan を起こす（スタック導入後に相対比重が上がるため順序はこれで正しい）。
- **半充填 PH 対称性による down スピン省略（2×）**: 物理仮定（二部格子・μ=U/2）に
  依存するため、汎用コードパスと分けた設計判断が要る。別 plan。
- **交互（上り/下り）スイープ化**: スタック再構築を省けるが MC 軌道が変わり
  検証コストが上がる。まず Option A（上りのみ + sweep 冒頭で右スタック再構築）で実装。
- HS 分解方式・格子・観測量の変更、GPU 化。

Commit trailer は実行モデルに合わせる（`AGENTS.md` Git ルール）。本 plan は
Claude Fable 5 セッションで作成。別モデルが実装する場合は `Co-Authored-By` を置換する。

---

## Background: 現行実装のコスト構造（実測で確定済み）

1 sweep あたり、`green_from_scratch` は periodic `floor(L/stab)` 回 + 末尾無条件 1 回、
各呼び出しが **L 回の `udv_lmul`（QR O(n³) + gemm ×2–3）** を実行する:

| 項目 | 回数/sweep | 小計 |
|------|-----------|------|
| 局所更新 (green_update) | ~n·L·2·acc | O(n³L) |
| wrap (gemm×2) | L×2スピン | O(n³L) |
| **from_scratch** | `(floor(L/stab)+1)×2` | **O(n³L²/stab)** ← 支配項 |

実測（16×16, β=4, L=40, stab=8, Mac serial）: 1 sweep 1.19s のうち
from_scratch 80%（udv_lmul 76%）、green_update 16%、wrap 3.5%。
β=20, dtau=0.025 (L=800, stab=4) の production では 1 sweep に QR ~32万回。

---

## Design: UDV 部分積スタック（Option A: 上りスイープ維持）

### 記法（レビュー High #1 反映: ceil ベースの境界定義）

block boundary を **`b[j] = min(j·stab, L)`（j = 0..M, M = ceil(L/stab)）** で定義する。
最後のブロック（tail block）は長さ `L − (M−1)·stab`（1..stab）になる。
B(τ2,τ1) = B_{τ2-1}···B_{τ1}。現行と同じく G(τ) = [1 + B(τ,0)·B(β,τ)]^{-1}
（`green_from_scratch(l0)` の循環積 B_{l0-1}···B_0·B_{L-1}···B_{l0} と同値）。

境界ケースの意味論（現行 `dqmc_sweep` と完全一致させる）:
- `L % stab != 0`: periodic 安定化は `(l+1) % stab == 0` の点（= b[1]..b[M-1]）でのみ
  発火。tail block は periodic では扱わず、**sweep 末尾の厳密化で左 UDV に延長**する。
  右スタックは `S[M] = I`, `S[j] = S[j+1] ⊗ B(b[j+1], b[j])` なので、
  最後の periodic 点 b[M-1] における右積 S[M-1] は tail block を正しく含む。
- `L < stab`（M=1）: periodic は一度も発火しない。sweep 末尾で全 L slice を
  左 UDV に延長して厳密化（現行の無条件 `green_from_scratch(0)` と同じ保証）。
- `L == stab` / `stab == 1` も同じ枠組みで特別扱いなしに成立すること（テストで担保）。

### スイープの流れ

1. **sweep 冒頭**: 現在の場から右スタックを構築（右から左へ）:
   `S[M] = I`, `S[j] = S[j+1] ⊗ P_j`（P_j = B(b[j+1], b[j]) の稠密積、⊗ は udv_rmul）。
   コスト: L 回の gemm + M 回の QR（現行の from_scratch **1回分**と同等）。
2. **ブロック j の局所更新 + wrap**（現行どおり、変更なし）。
3. **periodic 安定化点 τ = b[j+1]（j+1 < M、つまり tail を除く）**:
   - 更新済みの場から直前ブロックの稠密積 P を作り（ブロック長−1 回 gemm）、
     左 UDV に `udv_lmul(P)` で延長（QR 1回）。
   - `udv_combine(left, S[j+1])` で単一 UDV に結合（gemm 3回 + QR 1回）→
     既存 `udv_inv_one_plus` で G と det_sign を厳密再構成。
   - 場の整合性: 左積はこの sweep で更新済みの slice 0..τ-1、右スタックは
     未更新の slice τ..L-1 から構築済み → **常に厳密**（現行 from_scratch と同値）。
4. **sweep 末尾**: 最後に左 UDV へ取り込んだ境界 `tau_left` から `L` までの
   残り `B(L, tau_left)` を常に左 UDV に延長する。これは `L % stab != 0` の tail block、
   `L < stab` の all block、`L % stab == 0 && L > stab` の最後の full block を全て含む。
   その後、左 UDV は全 L slice（全て更新済み）を含む → そのまま `udv_inv_one_plus` で
   G(0) と sign を再構成。
   現行の「末尾無条件 from_scratch(0)」と同じ意味論を保つ（測定直前の厳密化、
   sign リセット 2026-07-01 plan の不変条件を維持）。

### 新規 linalg プリミティブ

- `udv_rmul(UDV *s, const double *P)`: (U D T)·P → UDV。
  X = D·(T·P) を作り QR: X = U' R → U ← U·U', D ← diag(R), T ← D^{-1}R。
  gemm 2回 + QR 1回（lmul と同コスト）。
- `udv_combine(const UDV *L, const UDV *R, UDV *out)`:
  C = D_L·(T_L·U_R)·D_R（gemm 1回 + 行/列スケール）を QR → C = U_C D_C T_C。
  out.U = U_L·U_C, out.D = D_C, out.T = T_C·T_R（gemm 2回）。
  結合後は正規の UDV なので **det_sign 計算を含む既存 `udv_inv_one_plus` をそのまま再利用**
  （2026-07-01 sign 安定化の枠組みは無変更で成立）。

### コスト比較（1 sweep、QR = dgeqrf+dorgqr 換算）

| | 現行 | スタック (Option A) |
|---|---|---|
| QR 回数 | 2·(M+1)·L | 2·(M + 2M + 1) ≈ 6M |
| gemm 回数 | ~2.5·2·(M+1)·L | ~2·(L + M·(stab+4)) ≈ 4L |

L=160, stab=4 (M=40): QR 13,120 → 240（**~55×減**）。
支配項が「安定化の QR」から「wrap/更新/スタック構築の gemm ~O(n³L)」へ移る。

現行 sweep 時間の外挿（16×16, β=8, L=160, Mac/Accelerate serial, udv_lmul 実測
1.9ms/call 基準）: **stab=4 で ~27–30s、stab=2 で ~50s**（レビュー指摘反映）。
スタック後の目標 ~1.5–2s は Mac serial の値であり、kugui/MKL では Task 7 で別途計測する。

### メモリ

スタック (M+1) 段 × **factor-only** (2n²+n) doubles × 2スピン
（High #2 反映: `UDV` は U/D/T のみの軽量構造体を維持し、作業領域は共有 `LinalgWork` に
分離する。stack entry に workspace を持たせるとこの見積もりが破綻する）。
- 16×16, L=160, stab=4: ~86 MB/replica — 問題なし。
- 1D 小格子, L=800 (β=20, dtau=0.025): n が小さいので数 MB — 問題なし。
- 将来 n=256 かつ L=800 級では ~420 MB/replica → スタック粒度を粗くする
  （q ブロックおきに保存し中間は再計算）オプションで対処。本計画では上限警告のみ。

### 数値安定性の注記

- ブロック内 stab 枚の稠密積は wrap 安定性と同じ前提（stab の選定基準そのもの）。
  現行 from_scratch は 1 枚ごとに QR しており、稠密ブロック積はそれよりわずかに緩い。
  → 検証ラダーの安定化残差チェックと Task 6 のクロスチェックで担保。
  懸念が出た場合のフォールバックとして「ブロック内も 1 枚ごと udv_lmul」を
  コンパイル時 or 入力オプションで残す。
- スタック化により**安定化が安価になる**ため、`stab=2` 常用のコスト増が
  QR 2倍程度で済むようになる（L4-U12 型の stab 汚染への恒久対策としても機能）。

---

## File Map

- Modify: `src/linalg.h` / `src/linalg.c`
  - **`UDV` は factor-only（U/D/T のみ）のまま維持**（スタックに大量保存するため）。
  - `LinalgWork` struct（QR 用 M/tau/work、逆行列用 ipiv/work、combine/inv_one_plus 用
    テンポラリ n×n 数枚）を新設。`linalg_work_init/free` で一括確保。
    呼び出し側（`Green`/`Dqmc`）が 1 個持ち、スタック段数に依らず定数個。
  - `udv_lmul` / `udv_rmul` / `udv_combine` / `udv_inv_one_plus` は `LinalgWork *`
    を受け取る形にする（既存シグネチャを残す場合は内部で work 版へ委譲）。
  - `la_inverse_work` / `la_logdet_work`（workspace 受け取り版）を追加し、
    `udv_inv_one_plus` 経路から呼ぶ。既存 `la_inverse`/`la_logdet` は簡便 API として残す。
  - `udv_rmul(UDV *s, const double *P, LinalgWork *w)` 追加。
  - `udv_combine(const UDV *l, const UDV *r, UDV *out, LinalgWork *w)` 追加。
- Modify: `src/green.h` / `src/green.c`
  - `green_build_Bblock(const Green *G, int l_begin, int len, double *out, double *tmp)`:
    slice l_begin..l_begin+len-1 の B 稠密積（gemm len−1 回）。
  - `GreenStack` struct（factor-only UDV 配列 M+1 段 + 境界配列 b[]）と
    `green_stack_alloc/build/free`。`green_stack_build` は現在場から右スタックを構築。
    境界は `b[j] = min(j*stab, L)`, `M = ceil(L/stab)`（tail block を正しく含む）。
  - `green_from_scratch_stacked(Green *G, GreenStack *st, UDV *left, int m)`:
    設計 §3 の結合再構成（戻り値・det_sign の意味論は `green_from_scratch` と同一）。
  - `green_build_B`: exp() を 2値キャッシュ化（s=±1）。`Model` 側に事前計算値を持たせるか
    `Green` に持たせるかは実装時判断（λ, dtau, U 固定なので init 時に確定）。
  - `green_update`: u/v バッファを `Green` に事前確保。`dger_` 置換は環境別に実測し、
    遅い場合は既存の手書き二重ループを維持する。
  - `green_wrap`: B/Binv/tmp バッファを `Green` に事前確保。
- Modify: `src/dqmc.h` / `src/dqmc.c`
  - `Dqmc` に `GreenStack stack_u/stack_d`、左 UDV、`udv_combine` 出力用の
    factor-only UDV scratch（`combined_u/combined_d`）を追加。
  - `dqmc_sweep`: 冒頭で `green_stack_build`、periodic 安定化と末尾再構成を
    stacked 版に置換。**fail-fast (`status`) と sign リセットの意味論は現行を完全維持**。
  - `dqmc_init` は現行 `green_from_scratch` のまま（リファレンス経路として温存）。
- Modify: `src/profiler.h` / `src/profiler.c`（レビュー Medium 反映）
  - region 追加: `PROF_GREEN_STACK_BUILD`, `PROF_GREEN_FROM_STACK`,
    `PROF_UDV_RMUL`, `PROF_UDV_COMBINE`（実装タスクと同時に登録）。
- Modify: `tests/`（新規: `test_udv_stack.c`, `test_green_stack.c`。既存も更新）
- Modify: `Makefile`（`TESTS` は wildcard なので新規 test は自動収載のはず。要確認）
- Modify（Task 7）: `VALIDATION.md`, `LOG.md`

---

## Task 0: ベースライン計測の固定

- [ ] `input/` に計測用入力を追加: `bench_2d_L8.txt`（8×8, β=4, profile=1）と
      `bench_2d_L16.txt`（16×16, β=4, profile=1。nwarm/nmeas は数分で終わる規模）。
- [ ] 現行コードで `profile.csv` を取得し、`docs/2026-07-02-performance-analysis.md` の
      実測値（sweep 1.19s, from_scratch 80% など）と整合することを確認。
      結果は LOG.md に記録（最適化後の比較基準）。
- [ ] Commit: `bench: add 2D profiling inputs for optimization baseline`

## Task 1: LinalgWork の導入と hot allocation 除去（factor/workspace 分離）

> **レビュー High #2 / Medium #1 反映:** `UDV` に workspace を足すのではなく、
> factor-only `UDV` と共有 `LinalgWork` に分離する（スタック保存時のメモリ膨張防止）。

- [ ] `src/linalg.h`: `LinalgWork` struct（QR 用バッファ、ipiv、n×n テンポラリ数枚）と
      `linalg_work_init(LinalgWork *, int n)` / `linalg_work_free` を追加。
      **`UDV` は U/D/T のみの factor-only を維持**。
- [ ] `la_inverse_work` / `la_logdet_work`（workspace 受け取り版）を追加。
      既存 `la_inverse` / `la_logdet` は簡便 API として残す（内部で一時 work を使う現行動作）。
- [ ] `udv_lmul` / `udv_inv_one_plus` を `LinalgWork *` 受け取りに変更し、内部 malloc/free と
      `la_inverse`/`la_logdet` 呼び出し（→ `_work` 版）を置換。数値結果は不変。
      呼び出し側（`green.c`）は `Green` が持つ `LinalgWork` を渡す。
- [ ] **完了条件: `udv_inv_one_plus` 経路（`la_inverse`/`la_logdet` 含む）に
      hot allocation が残っていないこと**を目視 + プロファイルで確認。
- [ ] `make test` 全緑（特に `test_udv`, `test_linalg`, `test_green_*`）。
- [ ] Commit: `perf(linalg): split factor-only UDV from shared LinalgWork, remove hot-path malloc`

## Task 2: udv_rmul と udv_combine（TDD）

- [ ] `tests/test_udv_stack.c`（新規）: n=3–6 のランダム行列列で以下の不変条件を検証
      （**レビュー Medium #2 反映: 「D の順序」は検証しない** — 現行 `udv_lmul` は
      R 対角を非ソートで D に入れており、順序は実装の不変条件ではない）:
      (a) `udv_rmul` 後の U·diag(D)·T が素直な行列積と一致、
      (b) `udv_combine(L,R)` の U·diag(D)·T が (U_L D_L T_L)(U_R D_R T_R) と一致、
      (c) UᵀU = I、T が単位上三角、
      (d) `udv_inv_one_plus` の det_sign が brute-force `sign(det(I+P))` と一致、
      (e) **悪条件ケース**: D のレンジが 1e±10 に広がる合成 UDV でも (a)(b) が安定に成立。
      誤差判定は成分ごとの **abs+rel 複合**（ゼロ近傍対策）または Frobenius ノルム比で行う。
- [ ] `src/linalg.c` に `udv_rmul` 実装（設計 §「新規 linalg プリミティブ」どおり、
      `LinalgWork` 使用、`PROF_UDV_RMUL` 計測を登録）。
- [ ] `src/linalg.c` に `udv_combine` 実装（同上、`PROF_UDV_COMBINE` 登録）。
      `out` は `l` / `r` と alias しないことを前提にし、テストでも alias しない scratch
      を使う（左積を右スタック込みに汚染しないため）。
- [ ] `make test` 全緑。
- [ ] Commit: `feat(linalg): add udv_rmul and udv_combine for partial-product stack`

## Task 3: ブロック稠密積と右スタック構築（TDD）

- [ ] `tests/test_green_stack.c`（新規）:
      (a) `green_build_Bblock(l, len)` が `green_build_B` を len 回 gemm した積と一致。
      (b) `green_stack_build` 後の各 `S[j]` について、U·diag(D)·T が
      素直に計算した B_{L-1}···B_{b[j]} と一致（小さい n, L=8, stab=2,4 で）。
      (c) **境界ケース必須**（レビュー High #1 反映）: `L=10, stab=4`（tail block 長 2、
      S[M-1] が B_9 B_8 を含むこと）、`L=5, stab=8`（M=1、periodic なし）、
      `L=8, stab=8`（L==stab）、`stab=1`。
- [ ] `src/green.c` に `green_build_Bblock` 実装（呼び出し側提供の tmp バッファ使用）。
- [ ] `src/green.c` に `GreenStack` + `green_stack_alloc/build/free` 実装
      （境界配列 `b[j]=min(j*stab,L)`, `M=ceil(L/stab)`、`S[M]=I` から `udv_rmul` で
      右から左へ。`PROF_GREEN_STACK_BUILD` 計測を登録）。
- [ ] `make test` 全緑。
- [ ] Commit: `feat(green): B-block products and right-side UDV stack`

## Task 4: stacked 再構成 green_from_scratch_stacked（TDD）

- [ ] `tests/test_green_stack.c` に追加: 固定 HS 場・固定 seed、n=8–16, L=8–16 で、
      **全 stabilization boundary τ = b[1]..b[M-1] と sweep 末尾相当 τ=0（l0=0）**について、
      `green_from_scratch_stacked` の G と det_sign が **既存 `green_from_scratch(τ)`
      の結果と一致**（G は abs+rel 複合 <1e-10、det_sign は完全一致）。
      パラメータは U=4 / U=12（悪条件）× `stab=2,4` に加え、**境界ケース
      `L % stab != 0` / `L < stab` / `L == stab` / `stab=1` を必須**とする。
- [ ] 小さい n/L（n≤6, L≤6）では det_sign を from_scratch との一致だけでなく
      **brute-force `sign(det(I + B_{L-1}···B_0))` とも照合**（レビュー検証推奨反映）。
- [ ] `src/green.c` に `green_from_scratch_stacked` 実装:
      左 UDV を `udv_lmul`(ブロック稠密積) で延長 → `udv_combine` →
      既存 `udv_inv_one_plus`（det_sign 含む）。失敗時は既存と同じ fail-fast 規約
      （det_sign=0, 非0 return）。`PROF_GREEN_FROM_STACK` 計測を登録。
- [ ] `make test` 全緑。
- [ ] Commit: `feat(green): stacked Green reconstruction (O(L) per sweep stabilization)`

## Task 5: dqmc_sweep への統合

- [ ] `src/dqmc.c`: sweep 冒頭で `green_stack_build`（両スピン）、periodic 安定化と
      末尾再構成を stacked 版へ置換。左 UDV は sweep 開始時に単位で初期化し
      ブロック完了ごとに延長。`udv_combine` の出力は専用 scratch UDV に書き、
      `left` や stack entry と alias させない。**末尾では `tau_left` から `L` までの
      残り suffix（tail / all block / 最後の full block）を左 UDV に延長してから厳密化**
      する（設計 §スイープの流れ 4）。
      **sign リセット位置・fail-fast (`status`) の意味論は現行と同一に保つ**
      （2026-07-01 plan の不変条件）。
- [ ] `tests/test_dqmc.c` 更新: 同一 seed で「旧経路（green_from_scratch を直接呼ぶ
      参照実装）」と「新経路」の G・sign を数スイープにわたり比較するテストを追加。
      注意: 丸めが変わるため**軌道のビット単位一致は要求しない**。比較は
      「同一の場配置を与えたときの再構成 G と det_sign の一致」に限定する
      （場を固定して from_scratch と stacked を突き合わせる形にする）。
- [ ] sweep 統合テストにも境界ケースを含める: `Ltr % stab != 0`、`Ltr < stab`、
      `Ltr % stab == 0 && Ltr > stab` の
      入力で、各 sweep 後の G / sign が旧経路と一致（現行 dqmc_sweep が保証する
      「periodic が一度も走らなくても測定直前は厳密」の維持を直接検証）。
- [ ] 既存テスト全緑: `make test`（特に `test_dqmc`, `test_green_*`,
      `test_sign_regression_slow` は `make test_slow`）。
- [ ] `make test_omp` / （可能なら）`make test_mpi` も緑。
- [ ] Commit: `perf(dqmc): use UDV stack for sweep stabilization`

## Task 6: green ホットパスの仕上げ

- [ ] `green_update`: u/v を `Green` に事前確保し、既存 `test_green_update` で回帰確認。
      `dger_` 置換は Mac/Accelerate では短い profile で遅かったため、速い環境でのみ採用する。
- [ ] `green_wrap`: B/Binv/tmp を `Green` に事前確保。
- [ ] `green_build_B`: exp(±λσ+c) の 2値を init 時に事前計算して参照。
      `test_green_buildb` で回帰確認。
- [ ] `field_N` の exp(∓2λσ) も 2値キャッシュ化（レビュー Low 反映。site 更新提案
      ごとに exp() が走っている。`Field` init 時に事前計算し、`test_field` で回帰確認）。
- [ ] `make test` 全緑。
- [ ] Commit: `perf(green): preallocate update buffers and cached exponentials`

## Task 7: 効果測定と検証ラダー

- [ ] Task 0 と同一入力で profile 取得、before/after を比較。
      判定は「`green_from_scratch` 占有率」ではなく **新 region の内訳**
      （`green_stack_build` / `green_from_stack` / `udv_rmul` / `udv_combine` /
      `green_wrap` / `green_update`）で行い、律速が安定化 QR から
      wrap/update/stack-build の gemm 系へ移ったことを確認する（レビュー Medium 反映）。
      期待: 16×16 β=4 で sweep ~1.19s → ~0.3s 以下。
- [ ] 計測は Mac/Accelerate serial と kugui/MKL を分けて記録する（見積もりの環境差を明示）。
- [ ] 検証ラダー再実行（`VALIDATION.md` 準拠）:
      (a) U=0 解析解一致、(b) 1D L=4 U=4 の ED 照合が統計誤差内、
      (c) 安定化残差チェック、(d) 半充填 sign=1（`test_sign_regression_slow` 含む）。
- [ ] 低温ストレス: 1D L=4, U=12, β=4, stab=2,4,8 で新旧の E(T) が統計誤差内で一致
      （stab 汚染チェックの流用）。**加えて `Dqmc.status` が fail-fast しないこと・
      half-filling sign が全 run で 1 近傍に保たれることを明記して確認**（レビュー推奨）。
- [ ] β スケーリング確認: 同一系で β=4,8,16 の sweep 時間を測り、**β² → β 線形**に
      なっていることを確認（kugui 実運用の律速が解消される直接証拠）。
- [ ] `VALIDATION.md` に stacked 安定化の検証結果を追記、`LOG.md` に before/after を記録。
- [ ] Commit: `docs: record UDV-stack speedup measurements and validation`

---

## リスクと対処

| リスク | 対処 |
|---|---|
| ブロック稠密積の悪条件化（大 U・大 stab） | Task 2(d)/Task 4 の U=12 検証 + 検証ラダー安定化残差。フォールバック（1枚ごと lmul）を残す |
| 丸め変化で旧結果とビット不一致 | 期待どおり。検証は「固定場での再構成一致」+「統計誤差内の物理量一致」で行う |
| スタックのメモリ（将来 n 大 × L 大） | 本計画では警告のみ。粒度を粗くするオプションは後続 |
| sign 安定化 (2026-07-01) との干渉 | `udv_inv_one_plus` を無変更で再利用するため det_sign 経路は同一。Task 4/5 で明示検証 |

## 後続計画（本 plan 完了後に判断）

1. **Delayed update**（ランク k=32–64 一括 dgemm）— スタック後の支配項対策、2–4×。
2. **半充填 PH 対称性**で down スピン省略 — 2×、要検証ラダー。
3. **kugui で MPI レプリカ × threaded MKL** のスケーリング測定（コード変更なし）。
4. 交互スイープ化（スタック再構築の省略）、スタック粒度オプション。
