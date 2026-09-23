---
date: 2026-07-14
datetime: 2026-07-14 17:36 JST
model: Claude Fable 5 (claude-fable-5)
summary: |
  Phase 3（udv_inv_one_plus_two_sided_work()）の実装レビュー。判定は承認。
  数式・buffer 割当・det sign・fail-fast 規約を独立検算し、さらにレビュアー側の
  追加数値検証で (A) graded e^{±250} での combine 経路との 1e-16 一致、
  (B) cross=800 で combine が fail-fast する一方 two_sided は有限、
  (C) det_sign=-1 ケースの dense 一致、を実測確認した。
  テストへの追加推奨 3 件（graded 比較・combine 差別化・負符号 det）を挙げる。
---

# udv_inv_one_plus_two_sided_work() (Phase 3) 実装レビュー

対象:

- `src/linalg.h` / `src/linalg.c`: `udv_inv_one_plus_two_sided_work()`
- `tests/test_udv_two_sided.c`（新規、Makefile の wildcard で自動登録）
- `docs/2026-07-14-udv-green-rebuild-roadmap.html`（Phase 3 done）、`LOG.md`

## 総合判定: 承認

実装は数学的に正しく、既存規約（fail-fast、`w->failed` latch、local work
fallback、エラーメッセージ形式）に完全に整合している。レビュアー側の追加数値
検証（後述）でも、graded scale・overflow 差別化・負符号 det のすべてで正しい
挙動を実測確認した。**Phase 4 の Green 統合に進んでよい。**

## 数式検証（独立再導出との突き合わせ）

実装を逐行検算した:

- `Dlb/Drb`: `|D|>1 ? D : 1`（符号を big 側に保持）— 改訂版計画・現行
  one-sided と同一の分割規則 ✓
- `TLUR = T_l · U_r` ✓、`Tinv = T_r^{-1}`（`dtrtri` upper/unit）✓、
  `ULTinv = U_l^T · T_r^{-1}`（`la_gemm(1,0)`）✓
- `H[i,j] = ULTinv[i,j]/Dlb[i]/Drb[j] + dls[i]·TLUR[i,j]·drs[j]`
  = `D_lb^{-1} U_l^T T_r^{-1} D_rb^{-1} + D_ls M D_rs` ✓
- g の組み立て（右から）: `Hinv·D_lb^{-1}`（列 scaling）→ `·U_l^T`
  （`la_gemm(0,1)`）→ `D_rb^{-1}·`（行 scaling）→ `T_r^{-1}·`
  = `T_r^{-1} D_rb^{-1} H^{-1} D_lb^{-1} U_l^T` ✓
- det sign: `sgn`（両側の `|D|>1 && D<0` で flip）× `sign(det U_l)` ×
  `sign(det H)`、`det T_r = 1` ✓ — 恒等式
  `I + LR = U_l D_lb H D_rb T_r` と整合
- overflow 安全性: H の第 1 項は `/Dlb/Drb` で縮小のみ、第 2 項は
  `|dls|,|drs| ≤ 1`。**`D_l[i]·D_r[j]` の cross product はどこにも現れない** ✓

buffer 割当も検証: `H=w->M`, `Hinv=w->Dmat`, `det_tmp=w->R` は分離しており、
`la_inverse_work` / `la_logdet_work` が内部で使うのは `w->ipiv`/`w->work` のみ
のため aliasing なし。入力 NULL / サイズ不一致 / work fallback / `w->failed`
latch の各経路も one-sided と同型。

## レビュアー側で独立に再現・追加した数値検証

`make test` 全通過（`test_udv_two_sided` 含む）に加え、scratch harness で
実装版 `linalg.c` に対し以下を実測した:

| 検証 | 条件 | 結果 |
|---|---|---|
| (A) graded 大 scale での経路間一致 | `D = {e^{+250}, 1, e^{-250}}` 両側（cross=500、combine も可動域内） | two_sided と combine+one_sided の g が **rel 1e-16 で一致**、det sign 一致 |
| (B) combine 不能域での差別化 | `D = {e^{+400}, 1, e^{-400}}` 両側（cross=800 > 709.78） | `udv_combine` は `w.failed=1` で fail-fast、**two_sided は rc=0・g 有限・det ±1** |
| (C) 負符号 det | `D_l = {-3, 0.5, 2}`, `D_r = {1.5, 0.4, 2.5}` | `det_sign = -1` が dense logdet の符号と一致、g は dense inverse と rel 1e-16 で一致 |

(B) が本フェーズの存在意義の直接実証である: 再現 seed の破綻条件
（cross_max=710.7）と同型のケースで、two_sided は combine が原理的に
通れない領域を通過する。

## テストへの追加推奨（実装は正しいがカバレッジに 3 つの穴）

現行 `test_udv_two_sided.c` の scale ケースは **D が一様**（`e^600` ×3、
`e^300` ×3）のため、`|D|>1` 分岐しか通らず Db/Ds 層別化がほぼ働かない
（`dls` が全て 1）。moderate ケースが混在 D をカバーするが小 spread のみ。
上の (A)(B)(C) をそのままテスト化することを推奨する:

1. **T1 (graded 比較)**: `{e^{+250},1,e^{-250}}` 両側で combine 経路と比較
   （`compare_combine=1`、dense 比較は skip）。層別化ロジックの回帰検出。
2. **T2 (combine 差別化)**: `{e^{+400},1,e^{-400}}` 両側で
   `udv_combine → w.failed==1`、`two_sided → rc==0` かつ g 有限。
   roadmap Phase 3 の意図（combine が通れない領域を通す）を仕様として固定する。
3. **T3 (負符号 det)**: 負の big D を片側奇数個含むケースで
   `det_sign==-1` を dense と照合。現行テストは det=+1 のみで、
   `sgn` flip ループが未検証。

いずれもレビュアー側 harness で通ることを確認済みのため、移植のみでよい。

## 軽微な指摘

- プロファイラ region が one-sided と同じ `PROF_UDV_INV_ONE_PLUS` を共有する。
  Phase 4 以降で combine vs two_sided の性能比較をするなら専用 region の追加を
  検討（現時点では不要）。
- `check_vector_nonzero(Dlb/Drb)` は構成上ゼロになり得ず defensive のみ — 無害。
- テストの dense 参照は一様 D だから成立している（κ(A) が小さい）。graded で
  dense 比較ができない理由（catastrophic cancellation）を一行コメントしておくと
  将来の誤った「dense 比較追加」を防げる。
- commit 分割（fail-fast / Phase 1 / 2 / 2.1 / 3 が working tree に混在）—
  再掲。diff は既に 20 files 超であり、Phase 4 統合前の分割を重ねて推奨。

## Phase 4 への申し送り

- `green_from_boundary_factors()` の分岐は
  「`combine` mode: 現行 `udv_combine + udv_inv_one_plus_work`（bit-identical）/
  `two_sided` mode: `udv_inv_one_plus_two_sided_work(prefix, suffix, ...)`」。
  `combined` 引数は two_sided では未使用になるが API 互換のため残してよい
  （改訂版計画どおり）。
- mode の所在は `Green.rebuild_mode`（改訂版計画）。`dqmc_record_stab_drift()`
  → `green_from_scratch()` は単一 factor 経路のため mode 分岐不要（現行
  one-sided のまま）だが、設定漏れで stab drift 参照だけ別 mode になる事故を
  防ぐため、mode は `green_alloc()` 後に一括設定すること。
- main.c の runtime guard 撤去（roadmap decision gate）を忘れないこと。
- 統合後の検証は改訂 roadmap の Phase 5 gate に従う: 再現 seed では失敗が
  単一 factor site（`udv_lmul_work stage=B_U_D` 等）へ移ることの確認、
  beta=30–32 では完走と combine との統計一致。
