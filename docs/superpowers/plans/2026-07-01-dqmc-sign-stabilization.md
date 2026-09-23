---
date: 2026-07-01
datetime: 2026-07-01 10:47 JST
revised: 2026-07-01 11:20 JST (Codex レビュー反映)
model: Claude Opus 4.8 (1M context)
status: plan
topic: DQMC 符号(sign)の安定化再構成 — 案B の TDD 実装計画
summary: |
  強結合(U=8/12)で `sum_sign=0` により「invalid bin」中断する不具合の恒久修正。
  各 `green_from_scratch`(安定化)で ↑↓の行列式符号を厳密に再計算し、
  スイープ間の高速更新で蓄積する偽の符号反転をリセットする。
  符号リセットは periodic 安定化後と「毎スイープ末尾の無条件 green_from_scratch(0) 後」の
  両方で行い（測定直前に必ず厳密化）、再構成/logdet 失敗時は stale sign を使わず fail-fast する。
  追加計算コストは O(n)/安定化 で無視可能。TDD の小タスクに分解する。
---

> **Revision note (2026-07-01):** 独立レビュー
> [`docs/reviews/2026-07-01-dqmc-sign-stabilization-plan-review.md`](../../reviews/2026-07-01-dqmc-sign-stabilization-plan-review.md)
> の High×2 / Medium×2 / Low×1 を反映済み。主な変更:
> (1) 符号リセットを **periodic 安定化後**と**既存の sweep 末尾 `green_from_scratch(0)` 後**の両方に明記、
> (2) 再構成/`la_logdet` 失敗時に stale `det_sign` を使わず **fail-fast**（`det_sign=0` sentinel + 伝播）、
> (3) `la_logdet(G)` 堅牢性の表現を緩和し stress test を追加、
> (4) 回帰テストの再現条件を診断済み値へ固定し fast unit と slow diagnostic を分離、
> (5) Cost 表の call count に sweep 末尾分を加算、
> (6) 旧 Task 5（測定点の追加安定化）は**現コードに既存**のため削除。

# DQMC Sign Stabilization (案B) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans または subagent-driven-development でタスク単位に実装する。各ステップは `- [ ]` チェックボックスで追跡する。

**Goal:** 半充填で理論上 `sign=+1` に保たれるべき符号が、強結合で偽反転を蓄積して `sum_sign=0`→ジョブ異常終了する不具合を、**符号を安定化点で厳密に再計算してリセット**することで恒久的に解消する。stdout の物理計算・出力フォーマットは変えない。

**Architecture:** 各スピンの Green 関数に「直近の安定化で確定した行列式符号 `det_sign`」を持たせ、`green_from_scratch` で厳密計算する。`Dqmc.sign` は初期化時と各安定化で `Gu.det_sign * Gd.det_sign` にリセットし、安定化間は従来どおり局所反転で追従する。これによりドリフト由来の偽反転が全 run に蓄積することを防ぐ。

**Tech Stack:** C11, GNU Make, 既存 LAPACK/BLAS (`dgetrf_`/`dgetri_`), 既存 `tests/test_*.c` standalone テスト方式 (`tests/test_util.h`)。

**Reference:**
- 不具合診断（i2cpu 再現実験, 2026-07-01, job 840456）: `L=4,U=12,dtau=0.1,β=4,stab=8` で production と同一の
  `ERROR: invalid bin ... sum_sign=0` を再現。`stab=2/1` または細かい `dtau` で平均 sign=1 に回復することを確認済み。
- 関連コード: `src/dqmc.c` (`dqmc_sweep`), `src/green.c` (`green_from_scratch`/`green_wrap`),
  `src/replica.c` (`replica_bin_values`/`replica_bin_add`), `src/linalg.c` (`la_logdet`)。

---

## Scope Check

この plan は **符号の安定化再構成（案B）のみ**を扱う。以下は含めない/別スコープ:
- 観測量の測定点ドリフトは**既に対処済み**（毎スイープ末尾の無条件 `green_from_scratch(0)` で
  測定時 G は厳密）。旧 Task 5 は不要のため削除（レビュー High #1）。
- `replica_bin_values` の hard-abort を graceful-skip にする頑健化は任意タスク Task 6。
  ※本 plan では数値失敗を `Dqmc.status` 経由で fail-fast させ、`sum_sign=0` を下流に持ち込まない。
- HS 分解方式・μ 設定・格子の変更は対象外（半充填は現状のまま正しい）。

Commit trailer は実行モデルに合わせる（`AGENTS.md` Git ルール）。本 plan は
`Claude Opus 4.8 (1M context)` セッションで作成。別モデルが実装する場合は
`Co-Authored-By` を実際のモデルへ置換する。

---

## Background: 根本原因（診断確定済み）

1. 半充填・二部格子＋スピンチャネル HS 分解では、単一スピン反転の Metropolis 比
   `R = Ru·Rd` は解析的に `≥0`。よって真の配置符号は常に `+1`（一般にはドープ時に
   `sign = sign(det M↑ · det M↓)`、`M_σ = 1 + B_σ(L-1)…B_σ(0)`）。
2. `dqmc_sweep` は `R` を**安定化間で高速更新（wrap）されたドリフトした Green 関数**から計算し、
   `R<0` で `D->sign` を反転する（`src/dqmc.c:46`）。
3. 強結合(U=8/12)ほど B 行列が悪条件でドリフトが大きく、`R` が**偽に負**と評価され符号が偽反転する。
4. `green_from_scratch` は G を UDV で厳密再構成するが、**符号は再計算しない**
   （`src/dqmc.c:54-58` は G だけ再構成）。偽反転は補正されず全 run に**蓄積**する。
5. ビンの `sum_sign` がちょうど 0 になると `replica_bin_values` がエラーを返し
   （`src/replica.c:66`）、`main.c` が「invalid bin」で MPI ジョブ全体を異常終了させる。

**修正方針（案B）:** `sign(det M_σ) = sign(det G_σ)`（`G_σ = M_σ^{-1}` より）。
各 `green_from_scratch` の直後に、厳密再構成された `G_σ` から `la_logdet` で符号を取得して
`Green.det_sign` に格納し、`D->sign` を `Gu.det_sign * Gd.det_sign` にリセットする。これでドリフト蓄積が断ち切られる。

**リセット位置（重要）:** 現 `dqmc_sweep` は periodic 安定化
（`(l+1)%stab==0` ブロック, `src/dqmc.c:54-58`）に加えて、**ループ後に毎スイープ無条件で
`green_from_scratch(&Gu,0)`/`green_from_scratch(&Gd,0)` を呼ぶ**（`src/dqmc.c:61-62`）。
測定は各スイープ直後に `D.sign` を読む（`src/replica_run.c` 測定ループ）ため、
- **本命は末尾 `green_from_scratch(0)` 直後の sign リセット**。ここで必ずリセットすれば、
  `Ltr < stab` で periodic 安定化が一度も走らない場合や `Ltr % stab != 0` の場合でも、
  **測定時の符号は常に厳密**になる。
- periodic ブロック直後にもリセットを入れる（スイープ内で `D.sign` を意味のある値に保つ／防御的）。
なお末尾 `green_from_scratch(0)` により**測定時の G 自体も既に厳密**なので、観測量の
測定点ドリフトは現状で既に無い（旧 Task 5 は不要）。

---

## Cost Analysis（なぜほぼ無コストか）

1 スイープ(L スライス)あたり。現コードの `green_from_scratch` 呼び出しは
**periodic `floor(L/stab)` 回 ＋ 末尾無条件 1 回**（`src/dqmc.c:61-62`）なので、
1 スイープの from_scratch 回数は `floor(L/stab) + 1`。

| 項目 | 回数/sweep | 単価 | 小計 |
|------|-----------|------|------|
| 局所更新+wrap | L | O(n³) | O(L·n³) |
| from_scratch（安定化） | `floor(L/stab)+1` | O(L·n³) | O((L²/stab + L)·n³) |
| **追加: la_logdet ×2(↑↓)** | `2·(floor(L/stab)+1)` | O(n³) | **O((L/stab + 1)·n³)** |

- 追加分は from_scratch 本体 `O(L·n³)` に対し `1/L` 倍のオーダ（L=数十）で、**from_scratch を数%増やす程度**。
- from_scratch 頻度は不変なので総 run 時間は実質不変（案A: `stab` 縮小＝支配項 O(L²/stab) を数倍化、とは対照的）。

（最適化: 符号は `udv_inv_one_plus` 内の 2 回の `la_inverse`(=`dgetrf`) と UDV の U/D/T の符号から
`O(n)` で相乗り取得も可能。まずは実装が単純で低リスクな `la_logdet(G)` 方式を採用し、
プロファイルや後述の stress test で必要になれば後で置換する。）

**数値堅牢性の注記（誇張しない）:** `la_logdet(G)` は単純で妥当な第一段階だが、
低温・強結合では `M = I+B…B` の特異値レンジが極端に広がり、`G=M^{-1}` も非常に小さい
特異値を持ち得るため、LU pivot の符号が常に安定とは限らない。したがって
**`la_logdet` の戻り値（`info`）を必ず検査し、失敗時は stale sign を使わず fail-fast する**
（Task 1/2）。高 U・低温・大 β の stress test を validation ladder に追加する（下記）。

---

## File Map

- Modify: `src/green.h`
  - `Green` struct に `int det_sign;`（直近の安定化で確定した `sign(det(1+B…B))`。
    有効値は ±1、**0 は「無効/失敗」sentinel**）を追加。
  - `green_from_scratch` の戻り値を `void` → **`int`**（0=成功, 非0=失敗）に変更。
- Modify: `src/green.c`
  - `green_from_scratch`: `udv_inv_one_plus` の戻り値を検査。成功時のみ G のコピーに対し
    `la_logdet` を呼び、その `info` も検査して `G->det_sign` に ±1 を設定。
    **`udv_inv_one_plus` または `la_logdet` が失敗したら `G->det_sign = 0` を設定して非0を返す**
    （`la_logdet` は in-place 破壊のため必ずコピーを渡す。G は測定に使うので保持）。
  - `green_alloc`/init 経路で `det_sign` を初期化（既定 +1）。
- Modify: `src/dqmc.h` / `src/dqmc.c`
  - `Dqmc` に **`int status;`**（0=正常, 非0=数値失敗）を追加。`dqmc_init` で 0 初期化。
  - `dqmc_init`: 初期 `green_from_scratch` の戻り値を検査。成功なら
    `D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);`（現行 `D->sign = 1.0;` を置換）、
    失敗なら `D->status = 1`。
  - `dqmc_sweep`:
    - periodic 安定化ブロック（`src/dqmc.c:54-58`）で両スピン再構成後に
      戻り値を検査し、成功なら `D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);`、失敗なら `D->status=1`。
    - **ループ後の末尾 `green_from_scratch(&Gu,0)`/`green_from_scratch(&Gd,0)`（`src/dqmc.c:61-62`）
      の直後にも同じ sign リセットを必ず入れる**（測定直前の厳密化）。失敗なら `D->status=1`。
    - 局所反転 `if (R<0) D->sign = -D->sign;` は維持（安定化間の追従）。
    - `D->status != 0` なら以降の作業を早期 return してよい（無駄な計算を避ける）。
- Modify: `src/replica_run.c`
  - warmup ループ後・各測定スイープ後に `D.status` を検査。非0なら当該 replica を
    **失敗として扱い**（`result->status` を失敗値にし、明確なメッセージ付きで非0 return）、
    `sum_sign=0` を下流の `replica_bin_values` まで持ち込まない。
- Modify: `tests/test_green_init.c`（または新規 `tests/test_green_sign.c`）
  - 小さい n/L で `G->det_sign` がブルートフォース `sign(det(I+B…B))` と一致することを検証（低 U/高 U）。
  - 既存 `green_from_scratch` 呼び出し（戻り値無視）は `int` 化後もそのままコンパイル可（C は戻り値無視可）。
- Modify: `tests/test_dqmc.c`
  - 既存 `CHECK_CLOSE(D.sign,1.0,1e-12)` を維持しつつ、**強結合×大 stab** で
    各スイープ後の `D.sign` が「その時点の別途厳密再構成 `Gu.det_sign*Gd.det_sign`」と一致することを検証。
- Modify: `tests/test_integration.c`
  - 回帰（slow）: 診断で確定した最小再現条件で「invalid bin」を起こさず sign≈1 を検証（Task 3）。
- Modify: `Makefile`
  - 新規テストを追加した場合のみ `TESTS`/ターゲットに反映（wildcard なら不要）。

---

## Task 1: 安定化からの厳密符号を Green に持たせる（失敗は fail-fast）

- [ ] `src/green.h`: `Green` に `int det_sign;`（±1 有効, **0=無効 sentinel**）を追加。
      `green_from_scratch` の宣言を `int green_from_scratch(Green *G, int l0);` に変更。
- [ ] `src/green.c` `green_from_scratch`:
      - `udv_inv_one_plus` の戻り値を受け、**非0（特異/失敗）なら `G->det_sign = 0` にして非0を返す**。
      - 成功時のみ `G->g` のコピーに `la_logdet` を適用。**`la_logdet` の `info` も検査**し、
        失敗なら `G->det_sign = 0` にして非0を返す。成功なら `G->det_sign = sgn`(±1)、0 を返す。
      - **前回値の据え置きはしない**（stale sign を絶対に採用しない）。
- [ ] `src/green.c` の割り当て/初期化で `det_sign = 1` を既定に。
- [ ] `tests/test_green_sign.c`（新規, deterministic）: `n=2,3`, `L=2,3`, 固定 HS 場に対し
      `B` 積 `M = I + B_{L-1}…B_0` を素直に構成して LAPACK で `sign(det M)` を計算し、
      `green_from_scratch` 後の `G->det_sign` と一致することを `CHECK`（低 U/高 U 両方）。
      戻り値 0（成功）も併せて確認。
- [ ] `make test`（該当テスト）で `OK`。
- [ ] Commit: `feat(green): return status and expose stabilized determinant sign in green_from_scratch`

## Task 2: Dqmc.sign を安定化で厳密リセット（2 箇所 + fail-fast 伝播）

- [ ] `src/dqmc.h`: `Dqmc` に `int status;` を追加。
- [ ] `src/dqmc.c` `dqmc_init`: `status=0` 初期化。初期 `green_from_scratch`（`src/dqmc.c:17-18`）の
      戻り値を検査し、成功なら `D->sign = (double)(D->Gu.det_sign * D->Gd.det_sign);`
      （現行 `D->sign = 1.0;` を置換）、失敗なら `D->status = 1`。
- [ ] `src/dqmc.c` `dqmc_sweep`: **2 箇所**で sign リセットを入れる（どちらも戻り値検査 → 失敗で `status=1`）:
      1. periodic 安定化ブロック（`src/dqmc.c:54-58`）の両スピン再構成後。
      2. **ループ後の末尾 `green_from_scratch(&Gu,0)`/`green_from_scratch(&Gd,0)`（`src/dqmc.c:61-62`）の直後**
         ← 測定直前の厳密化。ここは `Ltr<stab` や `Ltr%stab!=0` でも必ず通るので**最重要**。
      局所 `if(R<0) D->sign=-D->sign;` は維持。**`dqmc_sweep` 冒頭に `if (D->status) return;` を必須**
      （一度壊れたら以降 no-op）。
- [ ] `src/replica_run.c`（fail-fast の検査点を固定）:
    - **`dqmc_init` 直後（`src/replica_run.c:34` の直後）に `D.status` を検査**し、非0なら
      warmup へ入らず cleanup して非0 return（初期 `green_from_scratch` 失敗を warmup 前に止める）。
    - warmup ループ後・各測定スイープ後にも `D.status` を検査し、非0なら明確なメッセージ付きで
      非0 return（`replica_failed[]` 経由で失敗が伝播。`sum_sign=0` を下流に持ち込まない）。
- [ ] `tests/test_dqmc.c`: 既存の弱結合 `D.sign==1` を維持。追加で
      `U=12, dtau=0.1, β=4, stab=8` を数十スイープ回し、各スイープ後の `D.sign` が
      その時点の `Gu.det_sign*Gd.det_sign`（別途厳密再構成）と一致することを `CHECK`。
      `Ltr<stab`（periodic が一度も走らない）ケースも 1 つ含め、末尾リセット経路を検証する。
- [ ] `make test` で `OK`。
- [ ] Commit: `fix(dqmc): reset MC sign from stabilized determinant (periodic + end-of-sweep) and fail-fast on breakdown`

## Task 3: 回帰テスト（決定論的 fast + 実機 slow を分離）

再現条件は診断（job 840456）で `sum_sign=0` を確認済みの値へ固定する:
`lattice=chain, Lx=4, pbc=1, t=-1, U=12, dtau=0.1, beta_list=…,4.0, stab=8,`
`nwarm=2000, nmeas=8000, nbin=40, seed=246813579`（seed 固定なので決定論的）。

- [ ] **fast unit（決定論・CI 向け, `tests/test_dqmc.c` 等）**: 自然な数値 drift の発生に
      依存させない（環境差で flaky になり得るため）。代わりに **意図的に `D.sign` を壊してから
      1 スイープ回し、末尾 `green_from_scratch(0)` 後のリセットで `D.sign` が厳密
      `Gu.det_sign*Gd.det_sign` に戻ること**を `CHECK` する。例:
      `dqmc_init` 後に `D.sign = -12345.0;` と破壊 → `dqmc_sweep(&D);` →
      `CHECK_CLOSE(D.sign, (double)(D.Gu.det_sign*D.Gd.det_sign), 0.0);`（半充填なら +1）。
      統計揺らぎを含まず決定論的。
- [ ] **slow regression（別ターゲット）**: 診断固定 seed の直列 run で
      (a) 全ビンで `replica_bin_values` 成功（`sum_sign≠0`）、(b) 平均 sign が 1 に十分近い、を検証。
      `E_hub` は **緩い sanity（有限・符号・大きさが妥当）に留める**。厳密な統計整合は Task 4 実機へ分離。
      **配置:** 現 Makefile は `TESTS=$(wildcard tests/test_*.c)`（`Makefile:24`）で `test_*.c` は
      自動的に `make test` に入る。slow を通常 test から外すため **`tests/test_*_slow.c` 命名 + Makefile で
      `SLOW_TESTS` を別定義し `TESTS` から `filter-out`**、`test_slow` ターゲットを追加する。
- [ ] 修正前で slow regression の (a) が**確実に赤くなる**ことを一度確認してから緑にする
      （nwarm/nmeas を診断値まで確保しないと再現しない点に注意）。
- [ ] `make test` で fast unit が `OK`。slow は `make test_slow`。
- [ ] Commit: `test: deterministic corrupt-then-reset sign unit test + fixed-seed slow regression`

## Task 4: ビルド/全テスト/実機検証

- [ ] `make clean && make dqmc && make test`（serial）で全緑。
- [ ] `make dqmc_mpi`（macOS 不可なら kugui）でビルド通過、`make test_mpi` 緑。
- [ ] kugui i2cpu で job 840456 と同じ診断スキャン(`diag_driver.sh`)を修正版で再実行し、
      **`stab=8` でも U=12 全 β で sign=1、invalid bin 無し**を確認（`FI_PROVIDER=psm3` 等は
      `AGENTS.md` HPC メモ準拠）。
- [ ] `LOG.md` に診断→修正→検証を逆時系列で追記（frontmatter は共通ルール準拠、日時は `date` で確認）。

## Task 5（削除・不要）: 測定点で符号/G を厳密化

> **削除理由（レビュー High #1）:** 現 `dqmc_sweep` は**毎スイープ末尾で無条件に
> `green_from_scratch(&Gu,0)`/`green_from_scratch(&Gd,0)` を呼んでいる**（`src/dqmc.c:61-62`）。
> よって測定時の `G` は既に厳密安定化済みで、旧 Task 5 の「測定点の追加安定化」は不要。
> 代わりに **Task 2 でこの末尾 `green_from_scratch(0)` 直後に sign リセットを入れる**ことで、
> 測定時の符号も厳密になる（本来ここが本命の修正点）。

## Task 6（任意・別スコープ）: 1 ビン縮退で全体を落とさない

- [ ] `replica_bin_values` が `sum_sign==0` のビンを返した場合、MPI ジョブ全体を
      abort せず当該ビンを除外集計する（警告ログ + `nbin_effective`）。案B で `sum_sign=0`
      は基本的に消えるが、防御的措置として。
- [ ] Commit: `feat(replica): skip degenerate zero-sign bins instead of aborting run`

---

## Validation Ladder

1. **単体（det 符号）**: 小 n/L で `G->det_sign` == ブルートフォース `sign(det(I+B…B))`（Task 1）。
2. **単体（sign リセット）**: 高 U×大 stab で `D.sign` == 厳密再構成の逐次一致、`Ltr<stab` 経路も（Task 2）。
3. **回帰（決定論）**: 固定 seed の drift-sign fast unit（Task 3 fast）。
4. **stress（数値堅牢性）**: 高 U・低温・大 β（例 U=12, dtau=0.1, β=8〜12）で
   `green_from_scratch` が `det_sign=0`(失敗) を返さず ±1 を返すこと、または返した場合に
   **fail-fast すること**を確認（`la_logdet` の LU pivot 符号が壊れないかの検証）。
5. **回帰（slow）**: 診断ケースで invalid bin 消滅・sign≈1（Task 3 slow）。
6. **全テスト**: serial/MPI `make test*` 全緑（Task 4）。
7. **実機**: i2cpu 診断スキャン再実行で `stab=8` U=12 全 β sign=1（Task 4）。
8. **物理**: 既存 ED/TPQ 照合（`VALIDATION.md`）に対し弱結合の値が不変であること。

## Risks / Notes

- `la_logdet` は引数行列を LU で in-place 破壊するため、**必ず G のコピーを渡す**（G は測定に使うので保持必須）。
- **stale sign を絶対に採用しない**: `udv_inv_one_plus` / `la_logdet` の失敗時は `det_sign=0` sentinel を
  立て、`Dqmc.status` 経由で `replica_run` まで伝播して当該 replica を fail-fast させる。
  「符号 collapse を隠して別の物理量破綻として残す」ことを避ける（レビュー High #2）。
- 半充填では `Gu.det_sign*Gd.det_sign` は常に +1 のはず。もし厳密再構成が -1 を返すなら
  それは wrap ドリフトではなく `green_from_scratch`/UDV 自体の破綻を示す — その場合は
  `stab_interval` をさらに小さくするか UDV の再直交化頻度を見直す（`AGENTS.md` 固有メモ）。
  低温・強結合では `M` の特異値レンジが極端に広く `la_logdet(G)` の LU pivot 符号が
  常に安定とは限らないため、stress test（Validation 4）で監視し、必要なら将来 `udv_inv_one_plus` +
  UDV の `U/D/T` からの `O(n)` 相乗り方式へ置換する。
- 逐次(serial)と MPI で符号定義・集計(`replica_mpi.c` の pack/unpack)は共通。符号は
  `sum_sign` 経由で集計されるので MPI 側の変更は不要。
- `green_from_scratch` を `int` 化しても、戻り値を無視している既存テスト呼び出しは
  そのままコンパイル可能（C は int 戻り値の無視を許容）。
- 出力フォーマット（`# T E_hub … sign`）と物理量の定義は不変。弱結合の既存結果は変わらないこと。

## Implementation Notes (as-built, 2026-07-01)

計画では第一段階として `la_logdet(G)`（= `sign(det G)`）で符号を取る方式を採用予定だったが、
**実装・検証の結果この方式は不採用**とした。理由と最終形:

- **`la_logdet(G)` は診断ケースで実際に fail-fast した**: `U=12, dtau=0.1, β=4, stab=8` の warmup 中に
  `green_from_scratch` が `det_sign=0` を返して停止。`G=(1+P)^{-1}` は低温で極小特異値を持ち、
  `dgetrf` がゼロピボットを踏む（レビュー Medium #1 の懸念が現実化）。
- **採用した堅牢方式**: 符号を**安定化済み UDV 因子**から計算する。
  `1 + U D T = U · Db · M · T` より
  `sign(det(1+P)) = sign(det U) · sign(det Db) · sign(det M)`。
  ここで `T` は単位上三角なので `det T = 1`、`U` は直交（`det=±1`）、`M` は良条件。
  `U`/`M` の符号は `la_logdet`（良条件なので堅牢）、`Db` は対角符号の積で `O(n)`。
  実装は `udv_inv_one_plus(const UDV*, double *g, int *det_sign)` に出力引数を追加し、
  `green_from_scratch` はその値を `G->det_sign` に格納するだけ（`G` からは符号を取らない）。
- これはレビュー Medium #1 が「将来の相乗り方式」として挙げたものを**最初から採用**した形。
  失敗時は `*det_sign=0`＋非0 return で fail-fast（High #2）を維持。
- 変更ファイル（as-built）: `src/linalg.h`, `src/linalg.c`（`udv_inv_one_plus` 署名+符号計算）,
  `src/green.h`, `src/green.c`（`det_sign`, `int` 化）, `src/dqmc.h`, `src/dqmc.c`
  （`status`, 2 箇所 sign reset, 冒頭 early return）, `src/replica_run.c`（3 点 fail-fast 検査）,
  `tests/test_udv.c`・`tests/test_green_init.c`・`tests/test_dqmc.c`（det_sign/決定論 reset 検証）,
  `tests/test_sign_regression_slow.c`（新規）, `Makefile`（`SLOW_TESTS`/`test_slow`）。
- 検証: `make test`（serial 全緑）, `make test_mpi`（MPI 全緑）, `make test_slow`（診断ケース OK, ~3.3s）,
  警告なし（`-Wall -Wextra`）。**実機 i2cpu 再検証は未実施（承認待ち）**。
