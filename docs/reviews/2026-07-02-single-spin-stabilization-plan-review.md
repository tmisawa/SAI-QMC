---
date: 2026-07-02
datetime: 2026-07-02 22:04 JST
model: Claude Fable 5
status: review
topic: single-spin stabilization performance plan のレビュー
summary: |
  docs/superpowers/plans/2026-07-02-dqmc-single-spin-stabilization-performance.md
  のレビュー。全体構成（stab scan 先行、固定場での prefix/suffix stack 証明 →
  交互スイープ opt-in、低リスク linalg 削減、rank/thread scan）は妥当で、
  交互スイープの「未証明」とされた部分は本レビューで数理的に検証し設計として
  正しいことを確認した（有効性窓の論証）。High 1件: PH モードでは sign≡1 が
  構造的に恒等なので、Task 1 の stab scan 検証で sign=1 を安定性の根拠に
  使ってはならない（歴史的な stab 汚染の検出チャネル sum_sign=0 は PH で消滅）。
  Medium 3件（ED 照合の追加、carried stack のメモリ見積もり、invalidation
  規則の明文化）、Low 3件。
---

# single-spin stabilization plan レビュー

## 対象

- 計画: `docs/superpowers/plans/2026-07-02-dqmc-single-spin-stabilization-performance.md`
  （2026-07-02 22:05 JST, GPT-5 Codex）
- 照合したコード/データ: `src/dqmc.c`（PH 経路の現行 sweep 構造）、
  `src/green.c`（wrap/delay/stack）、`src/linalg.c`（udv_rmul/udv_combine の
  既存実装）、`tests/test_green_stack_lowtemp_slow.c`（存在確認済み）、
  PH kugui 検証データ（baseline 数値の出典）。

## 結論

**構成・タスク分解・decision gate の設計は妥当。実装に進んでよい。**
特に「交互スイープをいきなり実装せず、固定場で prefix/suffix stack の
正しさを証明してから stochastic sweep に入る」という構成は正しいリスク管理。
baseline 分解（安定化系 63% = 0.945 s/sweep）は kugui 検証レビューの数値と
一致しており、期待効果（stack_build 消去で上限 1.23×）の算術も正しい。

High 1件は Task 1 の検証設計に関わるため**実装前に plan へ反映すべき**。

## 交互スイープ設計の数理検証（plan が「未証明」とした部分）

plan §Design 2 の carried boundary stacks を独立に検証した。結論: **設計は正しい**。
鍵は各スナップショットの「有効性窓」の論証:

1. **forward sweep 中の prefix スナップショット** P[j] = B(b_j, 0):
   取得時点で slice 0..b_j−1 は forward 更新済みの最終値。これらの slice は
   forward sweep の残り（l ≥ b_j のみ更新）でも、次の backward sweep が
   b_j より下に降りるまでも変更されない。→ backward sweep が境界 b_j で
   必要とする prefix と厳密に一致 ✓
2. **backward sweep 中の suffix 蓄積** R = B(L, τ): 降順に更新された block を
   `udv_rmul` で右から延長。境界 b_j 到達時点で slice b_j..L−1 は全て
   backward 更新済み → G(b_j) = [1 + P[j]·R]⁻¹ は現在場に対して厳密 ✓
3. **backward 中の suffix スナップショット**が次の forward sweep の右 stack に
   なる論証も (1) と対称 ✓
4. **鮮度**: prefix/suffix とも毎 sweep 反対方向のパス中に単位元から
   蓄積し直されるため、sweep をまたいで古い因子が再利用されることはない
   （現行 stack_build と同等の数値品質）。
5. **backward wrap と更新の局所性**: slice l の更新は G(l) の対角のみ必要で
   訪問順に依存しない。cur_l 遷移（0 → L−1 → … → 0、wrap 数は forward と
   同じ L 回）、delayed flush（wrap 前）、tail block の扱い（降順では最初に
   消化）も plan の記述で整合 ✓

したがって Task 3 の固定場テストは「設計の証明」ではなく「実装の検証」として
機能する。設計リスクは当初想定より低い。

## Findings

### High: PH モードでは sign=1 が stab 健全性の証拠にならない（Task 1 の検証設計）

Task 1 の出力検証に「half filling `sign=1`」があるが、**PH 経路では
D.sign = 1.0 が構造的に固定**されており（R = e^{2λs}R↑² ≥ 0 の厳密恒等）、
stab がどれほど劣化しても sign は 1 のまま。歴史的に stab=8 汚染
（L4-U12）を検出したチャネルは「ドリフトした R の偽負 → sum_sign=0」
だったが、**この canary は PH モードでは消滅している**。`dN=0` も同様に
恒等で、stab 健全性とは無関係。

修正案:

- Task 1 の acceptance から sign=1 / dN=0 を「stab 健全性の根拠」としては
  外す（PH 経路の sanity としては残してよいが、判定には使わない）。
- 判定は次のいずれか（両方が望ましい）に置き換える:
  1. **観測量バイアス検査の参照を stab=2（最細）にする**（現記述は
     「stab=4 基準」— stab=4 自体が疑わしいケースで無力）。U=12 stress は
     production stabscan と同じ「stab=2 参照 + 十分な統計」で行う。
  2. **wrap ドリフト残差の直接計測**を scan に追加する:
     安定化直前の wrapped G と厳密再構成 G の差
     ‖G_wrap − G_rebuild‖∞ を profile/debug フラグで記録する。
     これは VALIDATION.md の「安定化残差」ラダーの定量化で、
     sign canary の代替として機能する。
  3. 補助的に、U=12 stress を two-spin 参照（テスト経由 `ph_symmetric=0`）
     でも走らせれば sign canary が復活する。

### Medium-1: Task 5 の統計検証に ED 照合を追加すべき

「short old/new output rows differ only within jackknife error」は
nmeas が小さいと検出力が弱い。交互スイープは MC 軌道を変えるため、
**絶対参照との照合**が最強の検証になる:

- 1D L=4, U=4 の E(T) を ED 値と比較（既存検証ラダーの流用、1D なので安価）。
  `sweep_order=alternating` で `input/1d_L4_U4.txt` 相当を回し、
  forward と同じ精度で ED に一致することを Task 5 の acceptance に追加する。

### Medium-2: carried stack のメモリ見積もりが plan にない

交互スイープは prefix + suffix の **2 系統**の boundary stack を持つ
（PH なので Gu のみ）:

- 16×16, L=160, stab=4: 2 × 41 × (2n²+n) × 8B ≈ **86 MB/replica**
  （現行 PH の 1 stack ≈ 43 MB から倍増、pre-PH two-spin と同水準 — 許容）。
- ただし**将来の 16×16 β=20, dtau=0.025 (L=800, M=200) では
  ≈ 420 MB/replica** となり、120 rank/node では ~50 GB 級。
  i2cpu のメモリ容量では収まる見込みだが、plan に見積もりと
  「スタック粒度を粗くする fallback」への言及を追加しておくべき
  （UDV stack plan のメモリ節と同じ扱い）。

### Medium-3: carried stack の invalidation 規則を明文化すべき

File Map に「valid flag を管理」とあるが、無効化の条件が列挙されていない。
最低限:

- 初回 sweep（または dqmc_init 直後）は両 stack invalid。
- `D.status != 0`（数値破綻 → fail-fast）で両 stack invalid。
- 再構成失敗からのリカバリ経路があるなら同様。
- （将来）field の外部変更・checkpoint 復元時は invalid。

Task 4 のチェックボックスに「invalidation 条件の単体テスト
（invalid 時に build 経路へフォールバックし結果が変わらないこと）」を
追加することを推奨。

### Low-1: 境界再構成は既存 profiler region を再利用する

`green_from_boundary_factors` は `PROF_GREEN_FROM_STACK` を使う
（新 region を増やさない）ことを File Map に明記すると、Task 5 の
profile acceptance（steady-state で stack_build ≈ 0）が現行の region 集計
スクリプトのまま読める。

### Low-2: dtrtri の unit-diag フラグ

`T` は単位上三角なので `dtrtri_("U", "U", ...)`（unit-diagonal 指定）を使う。
Accelerate / MKL とも提供あり。back substitution 自作より先に試すのが低リスク。

### Low-3: stab=16 は scan に含めるが採用想定から外してよい

stab=16 では block 稠密積の条件数が e^{16Δτ·W} 級になり、High の
ドリフト残差計測でほぼ確実に劣化が見える。scan のデータ点としては
有用だが、Expected Outcome 表に候補として載せる必要はない（現表は
stab=8 までなので整合している — 現状維持でよい）。

## 検証計画への追加推奨（まとめ）

1. Task 1: sign/dN を判定基準から外し、stab=2 参照 + wrap ドリフト残差計測に
   置き換える（High）。
2. Task 5: ED 照合（1D L4 U4）を acceptance に追加（Medium-1）。
3. Task 4: invalidation 条件の列挙とフォールバック単体テスト（Medium-3）。
4. Design/File Map: carried stack のメモリ見積もりと L=800 級への注記
   （Medium-2）。

## 実装前の修正順

1. Task 1 の検証基準を修正（High — scan の判定設計なので最初に）。
2. Medium-2/3 を plan の Design/File Map/Task 4 に追記。
3. Low は実装時に反映で足りる。

## 付記

- 交互スイープ設計の数理は本レビュー冒頭のとおり検証済みで、
  「未証明のため固定場証明を先行させる」という plan の慎重な構成は
  そのまま維持してよい（テストの位置づけが「証明」から「実装検証」に
  変わるだけで、テスト自体は全て必要）。
- baseline 分解・期待効果の算術・decision gate・PH 限定 opt-in・
  デフォルト不変の方針はいずれも妥当。Task 6 の LU 融合と dtrtri は
  低リスクで、det_sign fail-fast 不変条件の維持も明記されている。
- Task 7 の「replicas/hour で判断」は正しい指標設定
  （serial sweep 時間では帯域競合を見誤る）。
