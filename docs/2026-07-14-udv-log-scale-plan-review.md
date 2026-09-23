---
date: 2026-07-14
datetime: 2026-07-14 11:30 JST
model: Claude Fable 5 (claude-fable-5)
summary: |
  `docs/2026-07-14-udv-log-scale-implementation-plan.md` の徹底検証レビュー。
  opt-in 方針・段階的 Phase・fail-fast 維持は妥当。一方、コア数値設計に重大懸念が3点:
  (1) global offset 方式は overflow を underflow / rank 欠損に置き換えるだけで headroom が薄い、
  (2) `udvlog_lmul_work()` / `udvlog_rmul()` で入力 Dlog を dense 行列に適用する方法が未設計、
  (3) 標準手法である two-sided Db/Ds 分割（Loh et al.）との比較検討がない。
  総合判定: 条件付き承認（Phase 0–2 は着手可、Phase 3 着手前に設計判断 3 点の解決を要求）。
---

# UDV log-scale 実装計画 レビュー

対象文書: `docs/2026-07-14-udv-log-scale-implementation-plan.md`

## 検証方法

- 計画書中のコード引用・主張を現行ソースと突き合わせた
  (`src/linalg.c`, `src/linalg.h`, `src/green.c`, `src/green.h`, `src/dqmc.c`,
  `src/dqmc.h`, `src/io.c`, `src/io.h`, `src/main.c`, `src/profiler.c`, `Makefile`,
  `tests/`, `LOG.md`)。
- log-scale combine / inv_one_plus の数式を独立に検算した。
- 目標条件 (L=4, U=16, dtau=0.025, beta=33.325) での scale spread を概算し、
  global offset 方式の定量的 headroom を評価した。

## 事実確認の結果（計画書の記述はコードと一致）

- overflow 箇所の引用 `C[i + j * n] = l->D[i] * (l->T * r->U)[i + j * n] * r->D[j]` は
  `src/linalg.c:619` と一致。診断メッセージ形式も `check_matrix_finite()`
  (`src/linalg.c:83`) の出力と一致。
- 再現 seed `14012418791647386686`, beta=33.325 の破綻記録は `LOG.md`
  （`sweep_count=10397`, `bin=93`, `meas=26` で `udv_combine stage=C row=0 col=0
  value=-inf`）と一致。
- 現行 `udv_inv_one_plus_work()` の Db/Ds 分割・`g = T^{-1} M^{-1} Db^{-1} U^T`・
  det sign 処理の引用は `src/linalg.c:734-845` と一致。
- 計画書の log 版 `M[i,j] = Db_sign[i] * exp(-Db_log[i]) * UT_Tinv[i,j]` は現行の
  `UT_Tinv[i,j] / Db[i]` と厳密に等価（1/sign = sign）。det sign 式
  `sign(det U) * prod(Db_sign) * sign(det M)` も現行実装のコメント
  (`src/linalg.c:794`) と整合。**この節の数式に誤りは見つからなかった。**
- log-scale combine の数式
  （`offset = max logC`, `Chat = signC * exp(logC - offset)`, `QR(Chat)`,
  `out.Dlog = offset + log|Rhat_ii|`, `out.T = Runit * r.T`, `out.U = l.U * Q`）は
  数学的に正しい。行列全体の正スカラー倍は Q を変えないため厳密。
- `udv_combine()` の production 呼び出しは `green_from_boundary_factors()`
  (`src/green.c:234`) の 1 箇所のみ（他はテスト）。
- Phase 6 の `make test` / `make test_mpi` / `make test_slow` は `Makefile` に実在。
- 入力パーサ (`src/io.c`) は unknown key / 不正値で FAIL する構造であり、
  Phase 1 の `udv_scale=linear|log` 追加・typo 拒否テストは既存の `sweep_order`
  検証パターン (`src/io.c:270-276`) と同型で実装可能。

## 妥当な点

- **opt-in 方針**: default `linear` 固定・既存経路無変更・rollback 容易。production
  を壊さない設計として適切。
- **段階的 Phase 構成**: Phase 1（オプションのみ）を最初の commit 単位にする判断は
  TDD 方針（`AGENTS.md`）とも整合し、リスクが小さい。
- **fail-fast の維持**: 既存の `check_*` 診断体系を log 経路にも適用する方針は正しい。
- **zero / rank loss の扱い**: `D[i]==0` fail-fast、全要素 zero を failure とする
  規定は現行 `check_vector_nonzero()` と整合。
- **リスク欄の自己認識**: global offset で rank 情報が落ちる可能性を自ら挙げている
  （ただし後述の通り「可能性」ではなく目標領域では「ほぼ確実」）。

## 重大な指摘（Phase 3 着手前に要解決）

### M1: global offset 方式は目標条件近傍で underflow → rank 欠損に置き換わるだけ

overflow が起きた条件は `max_i l.Dlog + max_j r.Dlog + log|TLUR| > log(DBL_MAX) ≈ 709`
を意味する。半充填・ph 対称では左右 factor の特異値スペクトルはほぼ対数対称
（spread ≈ 2 × max）なので、combine 時の `logC` の全体レンジは
`spread_l + spread_r ≈ 2 × (max_l + max_r) ≳ 1400` decades に達する。

global offset 後の `Chat` では、`l.Dlog[i]` が `max_l` より約 745
（`-log(DBL_MIN_denormal)`）以上小さい行は**行全体が exactly 0 に underflow** する。
行が 1 本でも 0 になると rank ≤ n−1 となり QR の `Rhat` 対角に 0 が出るため、
`check_vector_nonzero` 相当で fail-fast する。つまり:

- `udv_combine stage=C -inf` は消えるが、同じ run が
  「combine 出力 D の zero 検出」で止まるだけになる公算が大きい。
- prefix が長い（τ が大きい）combine ほど `spread_l ≈ 2cτ` が大きく、
  overflow 発生条件 `c·beta ≳ 709` の下では τ の大きい boundary で
  `spread_l > 745` をほぼ確実に超える。
- headroom は高々 `exp(745 − 709) ≈ e^36`、beta 換算で数 % 程度。

**対応要求**:

1. Phase 3 の前に、短い再現 run で combine ごとの `max/min Dlog`（左右別）を
   ログ出力する診断を先に入れ、global offset で足りるかを実測で判定すること
   （数時間で終わる計測であり、設計のやり直しコストより遥かに安い）。
2. 受け入れ基準 4 と Phase 6 の「`udv_combine stage=C -inf` で止まらない」は
   letter として弱すぎる。「再現 seed の run が**完走**し、observables が有限」を
   明示要件にすること（現行文言では失敗 stage が変わるだけでも合格になる）。

### M2: `udvlog_lmul_work()` / `udvlog_rmul()` の設計が欠落している

計画書は「初期段階では現行 UDV と同等の QR を使いつつ、QR diagonal を
`Dsign/Dlog` に格納する」とだけ書くが、これは**出力**の話であり、**入力**の
`Dlog` をどう dense 行列に適用するかが未定義。現行 lmul は
`M[i+j*n] = (B·U)[i,j] * D[j]`（`src/linalg.c:394-398`）であり、log 版で
`exp(Dlog[j])` を再構成すると `Dlog[j] > 709` で即 overflow する。

- 列 scaling を offset で逃がすと M1 と同じ underflow / rank 欠損の壁に当たる。
- 列 scaling は QR と厳密に可換（`QR(M·diag(s)) = Q·(R·diag(s))`）だが、
  それを使って `diag(D)` を QR の外に出すと、新しい unit-T factor に
  `exp(Dlog_j − Dlog_i)` 型の因子が入り、`Dlog` が降順に整列していない限り
  T の conditioning が壊れる（= 安定化の意味を失う）。整列を保証するには
  column-pivoted QR (`dgeqp3`) の導入が必要になるが、計画書に言及がない。

**対応要求**: Phase 2 の関数一覧に対し、「lmul/rmul の入力 Dlog 適用方法」を
設計項目として明記すること。当面 log mode の適用範囲を「combine と
inv_one_plus のみ log 化、lmul/rmul は linear D のまま（単一 factor の
D < e^709 が前提）」と明示的に限定するのも可。ただしその場合、log 化で
得られる headroom は「cross-product の壁 (≈354/side) → 単一 factor の壁 (≈709)」
の約 2 倍に留まることを計画書に定量的に書くべき。

### M3: 標準手法（two-sided Db/Ds 分割）との比較検討がない

今回の overflow の直接原因は「`prefix × suffix` を dense に combine してから
inv_one_plus する」構造にある。DQMC の標準文献手法（Loh et al. の数値安定化、
QUEST / ALF 系実装）は、**combine を行わず** 2 factor のまま Green を再構成する:

```text
A = (U_l D_l T_l)(U_r D_r T_r),  M = T_l · U_r
D_l = D_lb · D_ls,  D_r = D_rb · D_rs   （|D|>1 を b 側、それ以外を s 側）

1 + A = U_l D_lb [ (U_l D_lb)^{-1} (D_rb T_r)^{-1} + D_ls M D_rs ] D_rb T_r
G = (D_rb T_r)^{-1} H^{-1} (U_l D_lb)^{-1}
H = D_lb^{-1} U_l^T T_r^{-1} D_rb^{-1} + D_ls M D_rs
```

構成要素はすべて `|D_lb^{-1}|, |D_rb^{-1}|, |D_ls|, |D_rs| ≤ 1` で bounded、
`H` は well-conditioned。`l.D[i] × r.D[j]` の cross product を**一切作らない**ため、
今回の overflow はそのまま消える。さらに:

- rank 情報を落とさない（M1 の壁がない）。
- 既存 `UDV` 構造体のまま実装でき、`UDVLog` の並行保持・コード重複が不要。
- production 変更点は `green_from_boundary_factors()`（`src/green.c:227`、
  combine+inv の 1 箇所）に閉じる。`green_from_stack()` / alternating の
  backward 経路も同関数経由なので自動的にカバーされる。
- det sign も `sign(det U_l) · prod(D_lb_sign) · sign(det H) · prod(D_rb_sign)`
  で現行の `la_logdet_work` 機構がそのまま使える（`det T_r = 1`）。
- 単一 factor の D 保持の壁（e^709）は残るが、これは log-scale 案の
  lmul/rmul 未設計問題（M2）と同じ壁であり、log 案が優位な点ではない。

**対応要求**: 計画書の「方針」節に two-sided 分割を対案として追加し、
採否理由を明記すること。レビュアーとしては **two-sided 分割を第一候補**、
log-scale UDV は「単一 factor D の壁を超える必要が実測で確認された場合の
第二段」に格下げすることを推奨する。両者は直交するので併用も可能。

## 中程度の指摘

### S1: mode の所在と関数シグネチャの設計が不足

計画書は `Dqmc` に `udv_scale` を持たせるが、分岐が必要な関数の多く
（`green_from_scratch()`, `green_stack_build_*()`, `green_from_boundary_factors()`）
は `Green` / `GreenStack` レベルで動作し、`Dqmc` が見えない。さらに
`dqmc_record_stab_drift()` → `green_from_scratch()`（`src/dqmc.c:35-42`）という
経路もあるため、mode（と `UDVLog` 用 work 領域）は `Green` または `GreenStack`
に持たせる必要がある。Phase 5 に「シグネチャ変更一覧」を追加すべき。

### S2: `!use_ph` 経路の log 用フィールドが欠落

`Dqmc` 候補フィールドに `left_log_u` / `combined_log_u` はあるが、
非 ph 対称経路が使う `left_d` / `combined_d`（`src/dqmc.h:45-47`）の log 版がない。
log mode を ph 対称 run に限定するなら、その旨を入力検証（`parallel` 等と同様の
組み合わせチェック）として明示すること。

### S3: 受け入れテストの比較方法が未定義（QR 符号不定性・trajectory 分岐）

- UDV factor は QR の列符号分だけ不定なので、Phase 2/3 の「現行 `udv_combine()`
  と一致」は factor 単位でなく **積 `U·diag(D)·T` または Green/observables 単位**
  で比較すると明記すべき。
- Phase 6 の「`log`, beta=25, same seed: linear と一致」は成立しない。
  `exp(log(x))` の丸めで浮動小数点演算列が変わるため、同一 seed でも
  accept/reject の分岐から trajectory はカオス的に分離する。要件節にある通り
  「統計誤差内で一致」に統一すること（bit 一致を期待するテストを書かないこと）。

### S4: 出力メタデータの具体化

- `run_info.txt` は C コードの生成物ではない（リポジトリ内に生成箇所なし。
  HPC 投入スクリプト側の産物）。計画書の「生成系にも、必要なら」を
  「stdout ヘッダ `src/main.c:474` に `udv_scale=` を追加（必須）、
  スクリプト側 `run_info.txt` は別 PR」と具体化すべき。
- `replicas.csv` への列追加（`src/main.c:497-501`）は既存の下流パーサを壊しうる。
  列追加するなら解析スクリプト側の対応を同 PR で行うか、ヘッダコメント行に
  留めることを推奨。

### S5: `LinalgWork` の拡張が必要

log combine は `signC`/`logC` 用に追加の n×n scratch（および `Dsign` 用 int 配列）
を要する。既存 `LinalgWork`（`src/linalg.h:16-42`）の流用計画（どのバッファを
使うか、`w->failed` プロトコルへの参加）を Phase 2 に明記すべき。

### S6: column-wise offset を「拡張」ではなく最初から採用すべき（log 案を採る場合）

列単位 offset は QR と厳密に可換（`Chat = C·diag(exp(−offset_j))` →
`R = Rhat·diag(exp(offset_j))` は上三角のまま）なので、global offset に対する
**近似ではなく厳密な改良**であり、コストも同等。log 案を進めるなら最初から
column-wise を基本とすべき。ただし列内の行方向 spread（`l.Dlog` の spread）に
よる underflow は残るため、M1 の壁の根本解決にはならない点は変わらない。
また `out.T` 正規化で `exp(offset_j − offset_i)` が生じ得る点（column pivoting
なしでは非有界）の検討が必要。

## 軽微な指摘

- 計画書自体が `AGENTS.md` の文書作成ルール（frontmatter: 日時・モデル名・要約）
  に違反している。「作成日: 2026-07-14 JST」のみで model / summary がない。
- 関数名の `_work` サフィックスが不統一（`udvlog_lmul_work` vs `udvlog_rmul`）。
  現行 API の不統一を踏襲しているだけだが、新規 API では揃えられる。
- n=16 では combine 1 回あたりの n² 個の `exp/log/sign` 演算が QR (O(n³)≈2700 flops)
  と同オーダーになり、combine 単体は数倍遅くなり得る。combine は stab boundary
  毎なので全体への影響は小さいが、Phase 6 に「profile.csv で combine 時間比較」を
  1 項目追加しておくとよい。
- テスト補助として `udvlog_from_udv()` / `udv_from_udvlog()`（moderate scale 限定）
  の追加を推奨。Phase 2/3 の参照比較が単純になる。
- Phase 2 の「diagonal scale spread case」に具体値を推奨:
  `D ~ {e^{+400}, e^{+200}, 1, e^{-200}, e^{-400}}` 程度で linear combine が
  overflow し log が有限、という mini-case は容易に構成できる。

## Phase 別判定

| Phase | 判定 | 補足 |
|---|---|---|
| 0 | 承認 | 現状維持の確認のみ。 |
| 1 | 承認 | `sweep_order` パターンと同型。着手可。 |
| 2 | 条件付き | S3（比較方法）・S5（scratch 設計)・M2（lmul 入力 Dlog の扱い）を先に確定。 |
| 3 | 保留 | M1 の実測診断と M3 の対案比較を先に。設計判断次第で内容が変わる。 |
| 4 | 承認（設計は妥当） | 数式検算済み。one-sided inv_one_plus は log 化で任意 scale に耐える（唯一、壁のない部分）。 |
| 5 | 条件付き | S1・S2 のシグネチャ/フィールド設計を先に。 |
| 6 | 条件付き | M1 の指摘通り「再現 run 完走 + 有限 observables」を明示要件化。 |

## 総合判定

**条件付き承認**。opt-in・段階実装・fail-fast 維持・rollback 方針は健全で、
`udvlog_inv_one_plus_work()` の設計（Phase 4）は数式レベルで正しい。
一方、計画の中核である「global offset 付き log-scale combine」は、目標条件
（beta=33.325, U=16）近傍で overflow を rank 欠損 fail-fast に置き換えるだけに
終わる公算が大きく（M1）、lmul/rmul の log 化は未設計（M2）。combine 自体を
不要にする標準手法 two-sided Db/Ds 分割（M3）の方が、変更範囲が小さく
（`green_from_boundary_factors()` 1 箇所）、rank 情報も失わない。

**推奨アクション順序**:

1. Phase 0–1 はこのまま着手（`udv_scale` オプションの器は two-sided 案でも使える）。
2. combine 時の左右 `Dlog` max/min を記録する診断を入れ、再現 seed の短縮 run で
   spread を実測（M1 の判定材料）。
3. 実測を踏まえ、two-sided 分割 vs log-scale combine の採否を決定して計画書を改訂。
4. 受け入れ基準を「再現 run 完走・有限 observables」に強化。
