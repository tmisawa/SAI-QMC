---
date: 2026-07-02
datetime: 2026-07-02 JST
status: review
topic: DQMC UDV stack performance plan review
summary: |
  docs/2026-07-02-performance-analysis.md と
  docs/superpowers/plans/2026-07-02-dqmc-udv-stack-performance.md のレビュー。
  UDV 部分積スタックで green_from_scratch の O(n^3 L^2/stab) 支配を
  O(n^3 L) へ落とす方針は妥当。ただし、実装前に L % stab / L < stab の
  端数境界設計と、UDV workspace を stack entry へ持たせないメモリ設計を
  修正する必要がある。
---

# DQMC UDV スタック高速化計画レビュー

## 対象

- 性能分析: `docs/2026-07-02-performance-analysis.md`
- 実装計画: `docs/superpowers/plans/2026-07-02-dqmc-udv-stack-performance.md`
- 照合した主な現行実装:
  - `src/green.c` (`green_from_scratch`, `green_wrap`, `green_update`)
  - `src/linalg.c` (`udv_lmul`, `udv_inv_one_plus`, `la_inverse`, `la_logdet`)
  - `src/dqmc.c` (`dqmc_sweep`)
  - `src/profiler.{h,c}`
  - 既存テスト `tests/test_green_init.c`, `tests/test_green_wrap.c`,
    `tests/test_udv.c`, `tests/test_dqmc.c`

## 結論

改善方針そのものは妥当。現行コードでは `green_from_scratch(l0)` が各安定化点で
全 `L` 枚の B 行列を `udv_lmul` で積み直しており、`dqmc_sweep` の periodic
stabilization と sweep 末尾厳密化が O(n^3 L^2/stab) の支配項になっている。
右側部分積スタックを使って「更新済み左積 + 未更新右積」を結合する設計は、この支配項を
O(n^3 L) へ落とす標準的な解で、7/1 の `det_sign` 安定化も既存
`udv_inv_one_plus` を最終経路に使う限り維持できる。

ただし、現在の plan のまま実装すると、端数 time slice とメモリで実害のある問題が出る。
High 2件は実装前に plan へ反映すべき。

## Findings

### High: `L % stab != 0` と `L < stab` の境界設計が不十分

計画では `Nb = floor(L/stab)`、`tau_m = m*stab`、`S[Nb] = I` と定義している。
この定義だと `L=10, stab=4` の場合、最後の安定化境界 `tau=8` における右側積は
本来 `B_9 B_8` だが、`S[Nb] = I` になってしまう。`L < stab` の場合は
`Nb=0` となり、sweep 末尾の厳密化で全 slice を左積に入れる明示手順がない。

現行コードは `Ltr < stab` や `Ltr % stab != 0` でも sweep 末尾に必ず
`green_from_scratch(..., 0)` を走らせ、測定直前の Green と sign を厳密化している。
この意味論は stacked 版でも完全に残す必要がある。

修正案:

- block boundary を `b[j] = min(j*stab, L)`、`M = ceil(L/stab)` で定義する。
- 右スタックは `S[M] = I`、`S[j] = S[j+1] * B(b[j+1], b[j])` とする。
- periodic stabilization は `l+1` が `stab` 境界に達した時点で
  `tau = l+1` に対応する `S[j]` と結合する。
- sweep 末尾では、最後の periodic stabilization 後に残った tail block
  `B(L, tau_last)` を左 UDV に延長してから `udv_inv_one_plus` を呼ぶ。
- Task 4/5 に `L=5, stab=8` と `L=10, stab=4` の固定場比較を必須化する。

### High: `UDV` に workspace を直接追加するとスタックのメモリ見積もりが破綻する

現行の `UDV` は `U`, `D`, `T` の factor だけを持つ軽量構造体。計画では
`UDV` に `work_M`, `work_tau`, `work_qr`, `work_R`, `work_T` などを追加して
`udv_lmul` / `udv_inv_one_plus` の malloc を除去しつつ、`GreenStack` を
`UDV` 配列として持つことになっている。

このままだと stack の各 entry が factor だけでなく巨大な workspace も持つため、
計画中のメモリ見積もり
`(Nb+1) * (2n^2+n) doubles * 2 spins`
が成立しない。16x16, L=160, stab=4 で「約86 MB/replica」という見積もりは、
stack entry が factor-only である場合に限って正しい。

修正案:

- `UDV` を factor-only のまま維持し、workspace は別構造体に分離する。
  例: `UDVWork` / `LinalgWork` を `Dqmc`, `Green`, または呼び出し側 scratch として共有。
- あるいは stack 用に明示的な factor-only 型を導入し、作業用 UDV と保存用 UDV を分ける。
- plan の Task 1 は「UDV struct へ workspace を足す」ではなく
  「factor と workspace の分離」に書き換える。

### Medium: malloc 除去の対象に `la_inverse` / `la_logdet` 内部 allocation が含まれていない

`udv_inv_one_plus` の内部 malloc/free を取り除いても、その中で呼ぶ `la_inverse` は
毎回 `ipiv` と `work` を確保する。`det_sign` 計算で使う `la_logdet` も `ipiv` を
毎回確保する。`udv_inv_one_plus` は安定化ごとに呼ばれるため、stack 後も無視しにくい。

修正案:

- `la_inverse_work` / `la_logdet_work` のような workspace 受け取り版を追加する。
- 既存 `la_inverse` / `la_logdet` は簡便 API として残してよい。
- Task 1 の完了条件に「`udv_inv_one_plus` 経路の hot allocation が残っていないこと」を
  明示する。

### Medium: UDV プリミティブテストの不変条件が現行実装とずれている

Task 2 は `D の順序` を検証条件に入れているが、現行 `udv_lmul` は QR の `R` 対角を
そのまま `D` に入れており、ソートや絶対値順序を不変条件にしていない。ここをテストすると、
実装として不要な制約を追加するか、既存 `udv_lmul` と不整合になる。

修正案:

- `D` の順序は検証条件から外す。
- 検証すべき不変条件は以下に絞る:
  - `U * diag(D) * T` が参照積と一致する。
  - `U^T U = I` が成立する。
  - `T` が単位上三角である。
  - `udv_inv_one_plus` の `det_sign` が brute-force `sign(det(I+P))` と一致する。
- 成分相対誤差だけでなく、ゼロ近傍に強い `abs + rel` または Frobenius norm で見る。

### Medium: profiler region が不足しており、Task 7 の効果測定が粗くなる

現行 profiler には `green_from_scratch`, `udv_lmul`, `udv_inv_one_plus`,
`la_gemm`, `la_inverse` などはあるが、stack 版の主要 region がない。
stack 導入後に見るべきなのは、`green_stack_build`, `udv_rmul`, `udv_combine`,
`green_from_scratch_stacked`、および既存 `green_wrap` / `green_update` への律速移動。

修正案:

- `PROF_GREEN_STACK_BUILD`
- `PROF_GREEN_FROM_STACK`
- `PROF_UDV_RMUL`
- `PROF_UDV_COMBINE`

を追加し、Task 7 の判定を「`green_from_scratch` 占有率」ではなく
「stack/build/combine/wrap/update の内訳」に変更する。

### Low/Medium: 16x16 beta=8, L=160, stab=4 の現行 ~50s 見積もりは高め

性能分析の 16x16 beta=4, L=40, stab=8 実測から単純外挿すると、beta=8,
L=160, stab=4 の現行 sweep は約 27-30 s/sweep 程度になる。
stab=2 常用を想定すると約 50 s/sweep に近い。結論として stack が最優先である点は
変わらないが、見積もりは `stab=4` と `stab=2` を分けて書く方が正確。

修正案:

- `stab=4: ~27-30 s/sweep`
- `stab=2: ~50 s/sweep`
- stack 後の目標値も、Mac/Accelerate serial と kugui/MKL で分けて記録する。

### Low: `green_build_B` だけでなく `field_N` の exp cache も検討対象

計画は `green_build_B` の `exp()` 2値キャッシュを含めているが、提案比計算では
`green_flipN` 経由で `field_N` が各 site update ごとに `exp()` を呼ぶ。
UDV stack 後はこの種の小コストも見えやすくなる。

修正案:

- `field_N(sigma, s)` の 2値も `Green` または `Field` 側に cache する。
- ただし、主効果ではないので Task 6 の一部でよい。

## 検証計画への追加推奨

既存 plan の Task 4 は重要で、固定場で `from_scratch` と stacked の G / det_sign を
比較する方針は正しい。さらに以下を追加するとよい。

- `l0` 全点ではなく、少なくとも全 stabilization boundary と sweep 末尾 `l0=0` を含める。
- `L % stab != 0`, `L < stab`, `L == stab`, `stab == 1` を含める。
- `det_sign` は from_scratch 一致だけでなく、小さい n/L では brute-force
  `sign(det(I+P))` とも比較する。
- stacked 統合後の `dqmc_sweep` は「同一 RNG 軌道の bitwise 一致」を要求しない。
  比較対象は固定場の再構成一致と、統計量の誤差内一致にする。
- `make test_slow` に加えて、低温 stress は `Dqmc.status` が fail-fast せず、
  half-filling sign が 1 近傍に保たれることを明記する。

## 確認済み

- 現行 `make test` は全緑。
- 現行 `green_from_scratch(l0)` は `l0` から一周して `P <- B_l P` で左積するため、
  実際の積は `B_{l0-1} ... B_0 B_{L-1} ... B_{l0}`。stacked 版はこの循環シフトを
  厳密に再現する必要がある。
- `det_sign` を既存 `udv_inv_one_plus` から取得する設計は、7/1 の sign 安定化
  不変条件（stale sign を使わない、失敗時 `det_sign=0`、`Dqmc.status` fail-fast）と整合する。

## 実装前の推奨修正順

1. plan の block boundary を `ceil(L/stab)` ベースに修正し、端数/tail block を明文化する。
2. `UDV` factor と workspace を分離する設計に直す。
3. profiler region を追加対象へ入れる。
4. Task 2 の UDV 不変条件から `D` 順序を削除する。
5. Task 4/5 のテストケースに `L % stab != 0` と `L < stab` を必須追加する。

