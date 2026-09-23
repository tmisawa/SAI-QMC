---
date: 2026-07-14
datetime: 2026-07-14 23:32 JST
model: Claude Fable 5 (claude-fable-5)
status: review
topic: Phase 6 single-factor countermeasure design review
summary: |
  `docs/2026-07-14-udv-phase6-single-factor-countermeasure-design.md` の徹底レビュー。
  数値実験により、設計動作点 chunk_beta_max=8 (spread~290) では augmented dense solve
  の Green が「有限だが完全に不正確」になること、受け入れゲート(対角 analytic case・
  正規化残差)がそれを検出できないことを実証した。精度要件は per-chunk spread <= 30-40
  (chunk_beta ~ 1) を強制し、その m では dense O((mN)^3) は L=4 でも成立しない。
  総合判定: 条件付き差し戻し。chain 表現(6.0)は続行可、6.1 は solver とゲートの
  再設計が必要。併せて、既知 seed (beta=33.325) だけなら centered scalar offset で
  chain なしに完走見込み(実測 spread 1355.8 < 容量 ~1400)という安価な対案を提示する。
---

# Phase 6 設計レビュー: bounded UDV chunk chain + augmented cyclic solver

対象文書: `docs/2026-07-14-udv-phase6-single-factor-countermeasure-design.md`

## 0. 総合判定

**条件付き差し戻し(major revision)。このままの数値パラメータと solver 選定では
「所望の性能」(正しい Green を実用時間で得る)は出ない。**

方向性は正しい: 単一 factor への collapse 禁止、chunk chain 表現、standalone
gate 先行、fail-fast、opt-in、two_sided 意味保存はすべて健全である。しかし
中核数値仮定に致命的問題が 3 点あり(F1–F3)、それぞれ数値実験で実証した。
同時に、設計が「745 の壁」を根拠に却下した offset 系のうち **centered scalar
offset は却下理由が成立しておらず**、既知 seed (beta=33.325) 単独なら chain
なしで完走見込みである(A1)。

| 論点 | 設計の主張 | レビュー結果 |
|---|---|---|
| chunk_beta_max=8 (spread<=600 gate) | ln(DBL_MAX) から十分安全 | 表現可能だが**精度が全損**(F1)。精度要件は spread<=30-40 |
| augmented dense solve | 5.3 のゲートで成立性判定可能 | ゲート設計に穴があり偽合格または自己停止(F2) |
| コスト O((mN)^3), m<=5 | L=4 で十分小さい | 精度補正後 m~34。L=4 でも 10^2-10^3 倍で不成立(F3) |
| log-D offset の却下 | spread>745 で rank 欠損 | max 正規化規約でのみ成立。centered なら容量 ~1400(A1) |
| det(K)=det(I+P) | unit test で固定予定 | **数値検証で確認済み・正しい**(m=2-5、負符号含む) |
| chunk 分割不変性 gate | 合格条件の一つ | **本当に判別力がある**。残すべき(誤差が分割に依存するため) |

## 1. 検証方法

- 設計書の現行コードに関する記述を全数照合した(別途コード調査:
  `src/linalg.c` の `udv_lmul_work` stage=B_U_D (`src/linalg.c:394-399`)、
  `udv_rmul` stage=D_T_B (`src/linalg.c:512-517`)、`udv_combine`
  (`src/linalg.c:617-621`)、Db/Ds 分割済み `udv_inv_one_plus_work`
  (`src/linalg.c:734-742`)、two_sided (`src/linalg.c:868-1093`)、
  `GreenStack`/`left_u` (`src/green.c`, `src/dqmc.c`)。設計の現状認識は正確)。
- 中核仮定を数値実験で独立検証した。スクリプト:
  `docs/2026-07-14-udv-phase6-review-experiment.py`
  (numpy 2.2.6 + mpmath 500 桁参照、seed 20260714。n=4, m=4 chunk、
  回転混合 chunk = 直交行列 × graded diagonal × 直交行列)。
- `LOG.md` の実測診断と突き合わせた: 平衡成長率 max_logD ≈ 20.0-20.5/unit-beta
  ±25 揺らぎ、平衡 spread ≈ 1086-1191 (beta=30)、破綻点実測
  `left_max_logD=708.146`, `left_spread_logD=1355.8` (beta=33.325, tau=1320)。

## 2. 実験結果(中核データ)

augmented cyclic system K を dense LU で解いた Green の相対誤差
(mpmath 参照比、median of 3 trials):

| per-chunk spread_logD | 誤差(回転混合) | 誤差(equilibration 付き) | 誤差(対角 chunk) | 設計 gate 4 残差 | rcond(K) |
|---:|---:|---:|---:|---:|---:|
| 10 | 7.8e-16 | 3.1e-15 | 1.0e-25 | 6.3e-17 | 5.1e-04 |
| 20 | 1.5e-13 | 1.6e-13 | 4.2e-22 | 2.6e-17 | 3.6e-06 |
| 30 | 4.3e-12 | 6.8e-12 | 0 | 1.2e-17 | 7.5e-09 |
| 40 | 1.5e-10 | 2.8e-10 | 5.1e-29 | 3.9e-17 | 6.1e-11 |
| 60 | 1.1e-08 | 1.4e-08 | 1.9e-34 | 1.2e-17 | 1.1e-15 |
| 80 | **6.2e+00** | 2.1e+00 | 3.7e-40 | 1.5e-16 | 4.3e-19 |
| 120 | 2.4e+34 | 5.5e+33 | 0 | 3.8e-17 | 9.9e-21 |
| 320 (gate 上限 600 の半分) | 9.0e+206 | 4.2e+206 | 4.3e-109 | 1.9e-17 | 2.3e-20 |

読み取り:

- 誤差は per-chunk spread に対して指数的に増大し、**spread ≈ 70-80 で全損**。
  1e-11 が必要なら spread <= 30、1e-9 なら spread <= 45-50。
- equilibration(dgesvx 相当の行/列スケーリング)は**救済しない**。
  これはスケーリングで除去できない本質的条件数(κ(K) ~ e^{spread/2})である。
- 対角 chunk はどの spread でも合格する(K がモード毎に 2x2 に分離し
  完全条件良好のため)。**設計 gate 3 の analytic 対角/直交 case は
  この故障モードを構造的に検出できない。**
- 設計 gate 4 の正規化残差 `||KX-RHS||/(||K|| ||X|| + ||RHS||)` は分母に
  ||K|| ~ e^{max_logD} が入るため、誤差 1e+206 の解でも 2e-17 を返す。
  **恒真ゲートであり無効。**
- rcond だけは正しく反応する(~e^{-spread})が、その帰結は F2 参照。

補助検証(いずれも設計の主張どおり正しいことを確認):

- `det(K) = det(I+P)`: m=2,3,4,5、負 determinant 込みで符号一致。
- 副産物: 解ブロック x[k] = B(tau_k,0)·G は time-displaced Green で有界。
  診断や将来の unequal-time 測定に流用可能(設計に追記推奨)。

## 3. 致命的問題

### F1: chunk gate (max 300 / spread 600) は表現可能性ゲートであり精度ゲートではない

設計 3.1 の chunk_beta_max=8 は spread ~ 290-320 (実測レート 36-41/unit-beta)
を生む。表 2 のとおりこの領域の augmented dense solve は誤差 ~1e+200 級の
「有限だが無意味」な Green を返す。overflow は消えるが、設計 5.4 が自ら
禁じた「overflow が消えたが信用できない値」そのものになる。

根本原因は 2 つあり、どちらも solver を替えても消えない:

1. dense materialization の情報喪失: F = U D T を dense に作った時点で、
   `max_logD - ln(1/eps) ≈ max_logD - 36` より下の特異値情報は丸めで破壊される。
   chunk が保持するはずだった下位モードを chunk 自身が失う。
2. 条件数の壁: κ(K) ~ e^{spread/2}(対称 chunk の解析で厳密、実験と整合)。
   forward error ~ eps·κ は backward stable な solver でも避けられない。

**要求**: chunk gate を精度基準 `spread_logD <= 30-40` に変更する
(fail-fast 方針は維持)。U=16 実測レートでは chunk_beta_max ≈ 0.75-1.0、
すなわち stab_interval=4, dtau=0.025 なら chunk = 8-10 stabilization blocks。
beta=33.325 で m ≈ 34-45、beta=100 で m ≈ 100-135。レートはパラメータ依存
なので、固定値でなく「実測 spread からの自動決定 + gate」で規定すること。

### F2: 受け入れゲート群は故障を検出できないか、全 trajectory を拒否するかの二択

表 2 より:

- gate 3(対角/直交 analytic case)は常に合格 → 偽陰性。
- gate 4(正規化残差)は常に合格 → 恒真・無効。
- gate 1-2(dense 参照比較)は「dense 参照が安全に作れる」moderate scale に
  限定されており、壁の領域を試せない。
- 唯一 rcond ゲートだけが正しく反応するが、chunk_beta=8 のままなら
  rcond ~ 1e-20 以下で**全正常 trajectory を拒否**し、設計 5.4 の stop gate 2
  「rcond 判定が正常 trajectory を拒否する」が発火して Phase 6 は自己停止する。

つまり現行ゲート構成での Phase 6.1 の帰結は、(a) rcond 閾値を緩めれば
偽合格して garbage が Phase 6.2-6.3 に流入(beta=30-32 統計比較まで発覚しない)、
(b) 締めれば自己停止、のどちらかしかない。

**要求**:

1. gate 3 を回転混合 graded chain(直交 × graded diagonal × 直交、参照は
   mpmath 等で offline 生成した固定テーブル)に差し替える。対角 case は
   「解けて当然の smoke」に格下げ。
2. gate 4 の残差指標を廃止または補助に格下げし、主ゲートを forward error
   (参照比較)+ chunk 半割り交差検証(現 gate 6 の強化版)にする。
3. rcond は「故障検出」でなく「chunk が大きすぎる」信号として R1 の
   spread gate と連動させる。

### F3: 精度を満たす m では dense augmented は L=4 ですら成立しない

コスト再見積もり(L=4, N=16, beta=33.325, boundary rebuild ~334 回/sweep×2 spin、
現行 two_sided sweep 実測 ~9 ms/sweep = Phase 5 の 160 s / 18000 sweeps):

| 構成 | m | K の次元 | LU flops/rebuild | flops/sweep | 対現行比(概算) | 数値精度 |
|---|---:|---:|---:|---:|---:|---|
| 設計のまま (chunk_beta=8) | 5 | 80 | ~3.4e5 | ~2.3e8 | ~2-5x | **全損** (F1) |
| 精度補正後 (spread<=40) | 34 | 544 | ~1.1e8 | ~7e10 | **~10^2-10^3x** | OK |
| beta=100 (T=0.01) | ~100 | 1600 | ~2.7e9 | ~2e12 | 論外 | OK |

**dense augmented には精度と速度を両立する動作点が存在しない。** 設計 5.4 の
stop gate「L=4 で two_sided の 10 倍以内」は、精度側を直せば必ず発火する。
設計 8 が「large-L では block structure 利用の O(m N^3) solver が必要になる
可能性」と書いた事項は、可能性でも large-L 限定でもなく、**L=4 の時点で必須**。

**要求**: Phase 6.1 の production 候補を最初から構造化消去 O(m N^3)
(block cyclic reduction + 各段の QR 再直交化。数学的には two-point BVP の
structured QR と同型 [要確認: S.J. Wright 1992, de Hoog & Mattheij 系])に置き、
dense augmented + LU は小規模テストオラクル専用と明記する。構造化版の概算:
m=34, N=16 で ~1.4e6 flops/rebuild → sweep 比 ~10-20x。beta=100 で ~30-60x。
これが T=0.01 の正直な価格であり、設計に明記して合意を取ること。
なお F1 の spread 上限は条件数由来なので構造化版でも同じく必要。

## 4. 見落とされている安価な対案 (A1): centered scalar offset

設計 2/4 は「beta=30 で spread 1086-1191 が double の dense 表現幅 ~745 を
超えるため、`D = exp(logD - offset)` の単一 offset では小さい側が underflow
して rank を失う」として offset 系を却下する。しかし **745 という値は
「max を 1 に正規化する」規約(offset = max_logD)の下でのみ成立する**。
offset を spread の中心に置けば(centered)、使用可能幅は
±709(両側 normal)= **~1400** に倍増する。

実験 Q4(スクリプト同上): D spread = 1200 / 1330 / 1400 の単一 factor
G = (I + U D T)^{-1} を、既存実装と同型の Db/Ds 分割
(`src/linalg.c:734-742` 相当)で plain double のまま反転 → いずれも有限、
mpmath 参照比誤差 ~1e-15。underflow も rank 欠損もない
(max 正規化では spread 800 で 8 モード中 1 個、1200 で 3 個が exact 0 に
underflow することも確認 = 設計の 745 主張はその規約でのみ正しい)。

現行コードへの適用は小さい:

- `UDV` に `double log_offset` を 1 本追加(D の総スケールを外に出す)。
- `udv_lmul_work`/`udv_rmul` の QR 再分解後に D を再中心化して offset に吸収。
- Db/Ds 分割(one-sided/two_sided とも)を実効 log = `log|D| + log_offset` の
  符号で行う。G の式に入るのは Db^{-1} と Ds (いずれも <=1) だけなので、
  offset は解析的に相殺され、指数化は常に in-range。
- det sign 機構・既定値・two_sided の意味は不変。

適用限界も正直に書く: 破綻点実測 `left_spread=1355.8` に対し容量 ~1400 で
**余裕は ~4%(beta 換算で +1-1.5)**。beta=33.325 の既知 seed は完走見込みだが
beta ≈ 35 が限界で、production グリッドの下限 T=0.01 (beta=100、予測 spread
~3600-4100) には全く届かない。**よって chain (Phase 6) の否定ではなく、
既知 seed を先に unblock する Track A としての追加提案である。**
着手前に、既存 `udv_scale_file` TSV で「centered 換算で in-range か」を
既知 seed の短縮 run で確認すれば、実装前に成否を予測できる。

## 5. その他の設計修正要求

- **R6 (粒度の不整合)**: 3.1 は chunk = 320 slices、6.1 は block = 1 stab
  block (4 slices) 単位の格納で、solver に渡す粒度が曖昧。per-boundary で
  block 列を chunk へ naive に再結合すると O(M N^3)/boundary(現行の ~333x)
  の隠れコストになる。R1 補正後は chunk = 8-10 stab blocks なので、
  「chunk 単位の増分キャッシュ(通過済み chunk のみ再構築)」を 6.2 に明記
  すること。
- **R7 (目標の明確化)**: acceptance target が beta=33.325 のみなら Track A で
  足りてしまい、Phase 6 の投資判断が変わる。roadmap の真の目標
  (production T グリッド下限 = T=0.01, beta=100 [要確認: どこまで実運用で
  必要か])を Phase 6 の goal 節に明記し、m~100・rebuild ~30-60x の価格と
  セットで合意を取ること。
- 6.2 の「accepted flips により通過済み block をその場で再構築」は現行
  semantics と整合しており妥当。
- 診断 TSV (`udv_chain_file`) 設計は妥当。列に per-chunk の実効条件数指標
  (spread_logD で代用可)と κ(T) 系列を足すことを推奨(unpivoted `dgeqrf`
  で graded 行列を扱う現行構成の理論的リスク監視。文献は QRCP/`dgeqp3` を
  推奨するが、beta<=32 実績では問題は出ていない [要確認: spread>1200 域])。
- `dgesvx` は現行コード未使用(inversion は `dgetrf_`+`dgetri_`)。導入自体は
  可能だが、F2 のとおり rcond/equilibration は主ゲートにならない。
- long double は kugui (x86, 80-bit, exp range ±11356) では応急策になり得るが、
  Apple Silicon では double と同一のため不可搬。プロトタイプ検証専用と
  明記する場合のみ可。
- 設計 2 の「representable width 約 745」には規約(max 正規化)を明記すべき。
  数値としては denormal 下限 ln(DBL_TRUE_MIN) ≈ -744.4 由来で、centered 規約
  では ~1417 (normal のみ) が正しい上限。

## 6. Phase 別判定

| Phase | 判定 | 条件 |
|---|---|---|
| 6.0 (UDVChain 表現) | 承認 | gate 数値を R1 (spread<=30-40) に差し替え |
| 6.1 (standalone solver) | 差し戻し | F2 のゲート再設計 + F3 の構造化 solver への変更。dense augmented はテストオラクルに格下げ |
| 6.2-6.5 | 保留 | 6.1 再設計後に再レビュー。R6 の粒度・キャッシュ設計を 6.2 に追記 |
| (新) 6A: centered scalar offset | 追加推奨 | 6.0 と並行可。既知 seed の unblock 最短経路。着手前に TSV で in-range 確認 |

## 7. 「所望の性能が出るか」への直接回答

1. **現設計のまま**: 出ない。chunk_beta_max=8 では Green が数値的に無意味
   (F1)、ゲートはそれを見逃すか全拒否する(F2)、精度側を直すと dense solver
   が L=4 でも 10^2-10^3 倍で stop gate 発火(F3)。dense augmented に有効な
   動作点はない。
2. **修正後 (R1: spread<=30-40 + R2: 構造化 O(m N^3) solver)**: beta=33.325 は
   達成可能と判断する。ただし boundary rebuild コスト ~10-20x (L=4) を許容する
   こと。beta=100 は m~100 で ~30-60x。メモリは O(m N^2) work + O(M N^2)
   storage に収まり、L=16 の 2 GiB gate は dense K を作らない限り問題ない。
3. **既知 seed だけを最短で通すなら**: Track A (centered scalar offset) が
   実装数日で到達見込み(実測 spread 1355.8 < 容量 ~1400、実験で誤差 1e-15 を
   確認)。ただし余裕 ~4% で beta~35 が限界。Phase 6 chain は T<0.03 の
   production 拡張に対する本命として、上記修正の上で継続する価値がある。
