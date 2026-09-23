---
date: 2026-07-14
datetime: 2026-07-14 15:59 JST
model: Claude Fable 5 (claude-fable-5)
summary: |
  Phase 2.1 計画（udv_scale_file の単一 factor site 拡張）のレビュー。
  判定は承認。schema 維持・observer 設計・記録サイト選定はいずれも妥当で、
  Phase 2 の既存行と合わせると production sweep 中の全 dense 形成箇所の
  入出力が観測可能になることを検証した。実装前に反映すべき明確化 3 点
  （right 側カウントと既存 helper の NULL sentinel の不整合、単一 factor 行の
  tau の意味、direction label の前方一致衝突）を指摘する。
---

# udv_scale_file Phase 2.1 計画レビュー

対象:

- `docs/2026-07-14-udv-scale-diagnostics-phase2-1-plan.md`
- `docs/2026-07-14-udv-green-rebuild-roadmap.html`（Phase 2.1 行・Phase 5/6 改訂・
  decision gates 追加）

## 総合判定: 承認（実装前に明確化 3 点の反映を推奨）

計画は小さく、既存 schema・production stdout・serial 制限をすべて維持しつつ、
Phase 2 レビューが指摘した「次に失敗が現れる site」を過不足なくカバーする。
roadmap の改訂（Phase 5 gate の分割、Phase 6 の必須化、guard 撤去 gate、
Phase 3 テスト指針）も前回レビューの提案と正確に一致していることを確認した。

## 妥当性の検証結果

### 記録サイトの網羅性（レビュアーによる検証）

Phase 2 の boundary pair 行は「各 boundary の lmul **後**の left_u」を記録して
いるため、次の boundary の lmul への**入力**は 1 boundary 前の行で既に観測できる。
これに Phase 2.1 の 4 site を加えると:

| dense 形成箇所 | 入力の観測手段 | 出力の観測手段 |
|---|---|---|
| sweep 中 boundary の `udv_lmul_work` | 1 つ前の pair 行（既存） | 当該 pair 行（既存） |
| end-of-sweep 最終 `udv_lmul_work` | 最終 pair 行（既存） | `forward_left_udv`（新規） |
| `green_stack_build_suffix` の `udv_rmul` | `stack_suffix_pre_rmul`（新規） | 次 iteration の pre 行 / pair 行の right 側（既存） |
| `green_stack_build_prefix` の `udv_lmul_work` | `stack_prefix_pre_lmul`（新規） | 同上 |
| backward 系 | 同構造（`backward_left_udv` 含む） | 同上 |

つまり Phase 2.1 完了後は、production sweep 中に単一 factor D が double 範囲を
超え得る全箇所の直前・直後スケールが TSV から読める。**site 選定は完全**。
（範囲外は `green_from_scratch()`（初期化・stab_drift 参照経路）のみで、
これは診断対象外として妥当。）

### 設計の妥当性

- **schema 維持 + direction label 方式**: 列数固定で既存 notebook が壊れない。
  単一 factor 行の right 側を「不在マーカー」にする方針も、
  counts の合計が実 factor では必ず n ≥ 1 になるため機械判別可能で良い。
- **observer callback**: `green.c` に file I/O / `Dqmc` 依存を入れない判断は
  正しい。null default で既存挙動不変、失敗は `G->work.failed` latch 経由で
  既存 UDV 失敗と同じ経路により `D.status != 0` へ届く（`udv_rmul` /
  `udv_lmul_work` が `w->failed` で early-return するため後続も自然に止まる）。
- **テスト期待値**: L=6, stab=2 では suffix build の pre-rmul は j=2,1 の 2 回、
  `forward_left_udv` は 1 回 — 計画の「1 回以上 / 1 回」と整合。
- **リスク認識**: ファイル増大・observer 残留・B 由来 O(1) 因子の非包含、
  いずれも実害と緩和策が正しく書かれている。

## 実装前に計画へ反映すべき明確化（3 点）

### C1: right 側カウント仕様と既存 helper の NULL sentinel が不整合

計画は単一 factor 行の right カウントを `0/0/0` と規定するが、既存
`udv_scale_stats()` は NULL 入力で `nonfinite_count=1` を sentinel として返す
（`src/dqmc.c`）。「既存 append helper をそのまま使う」（Implementation Shape
7 項）と右側が `nonfinite_count=1` になり、**「right factor に非有限値があった」
と誤読される**。単一 factor 用の emit 分岐（right 列を明示的に `nan`/`0` で
書く）を計画に一行明記すること。`0/0/0` 仕様自体は正しいので維持してよい。

### C2: 単一 factor 行の `tau` の意味を定義する

- `forward_left_udv` の `tau=0` は「full [0,L) product」を表すが、tau で
  ソート・プロットすると原点に落ち、spread-vs-tau の解析を乱す。
  `tau=Ltr` とする（backward は再アンカー先の `tau=0` のままで自然）か、
  少なくとも解析上の注意を計画に明記すること。
- `stack_*_pre_*` 行の `tau=st->b[j]` は**行き先** boundary であり、記録時点の
  factor は suffix なら `b[j+1]..L` を被覆している。「pre 行は destination
  boundary でラベルする」と一行明記すること。

### C3: direction label の前方一致衝突

`forward_left_udv` / `backward_left_udv` は既存 `forward` / `backward` と
前方一致する。完全一致フィルタは安全だが、notebook の `startswith` /
`grepl("^forward", ...)` 型フィルタは新行を silent に巻き込む。
label を `left_udv_forward` / `left_udv_backward` に反転するか、
「フィルタは完全一致で行うこと」を計画の TSV Policy に明記すること。

## 軽微な提案

- DQMC unit test の「25 列維持」は、全行のタブ数 = 24 の assertion として
  実装すると回帰検出が確実（header だけでなく全 row を検査）。
- 受け入れ基準に `mpirun -np 1 ./tests/test_dqmc_mpi`（Phase 2 と同じ検証手順）
  を加えると、MPI ビルドでの diagnostics off 経路の無害性も固定できる。
- 行量は再現条件で pair 165 + pre-rmul 165 + left_udv 1 ≈ 331 行/sweep と
  ほぼ倍増する。計画の「短い diagnostic run 用」の明記で十分だが、
  Phase 2 レビューで挙げた `udv_scale_every=N` 間引きの優先度が上がる点は
  記憶しておくこと。

## roadmap 改訂の確認

- Phase 5 exit criteria: 「combine overflow の解消 or 単一 factor site への
  失敗移動」+「壁の内側 beta=30–32 の完走・統計一致」に分割 — 前回提案どおり。
- Phase 6: 「Required for beta=33.325 target」に格上げ、log-D / 有界
  multi-factor 保持の設計選択を exit criteria 化 — 前回提案どおり。
- decision gates に guard 撤去（Phase 4）と「beta=33.325 完走は Phase 6 の
  受け入れ目標」を追加 — 前回提案どおり。
- Phase 3 exit criteria に「片側大 scale / 両側大 scale」「g / det sign 単位比較」
  を追加 — 前回申し送りどおり。

矛盾は見つからなかった。

## 結論

Phase 2.1 は実装に進んでよい。着手前に C1–C3 を計画文書に反映すること
（いずれも一行〜数行の修正）。実装後のレビュー時には、再現 seed の診断 run で
`stack_suffix_pre_rmul` 行の suffix 成長率が平衡化とともに prefix 側
（0.54–0.57/slice）へ近づくかを確認したい — これが Phase 6 の設計選択
（log-D vs 有界 multi-factor 分割）の直接の判断材料になる。
