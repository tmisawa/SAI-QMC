---
date: 2026-06-25
datetime: 2026-06-25 11:04 JST
model: Codex (GPT-5)
status: review
topic: 有限温度補助場 QMC 設計・実装計画の実装前最終レビュー
summary: |
  commit bc9d82a の設計書・実装計画を実装前に再レビューした。
  Critical/High は見当たらないが、green_from_scratch の非可換積テスト、
  入力 parser の厳格化、UDV stress test、2D 検証記述など、
  実装前に計画へ追記するとよい Medium/Low findings を整理した。
---

# 有限温度補助場 QMC 設計・実装計画 第7回レビュー

## 対象

- commit: `bc9d82a docs: capture sixth DQMC review input-safety fixes`
- 設計書: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md`
- 実装計画: `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md`
- 第6回レビュー: `docs/reviews/2026-06-25-finite-T-aux-field-qmc-plan-sixth-review.md`

## 総評

Critical/High は見当たらない。物理定式化、`H0/E_gc`、補助場 `N` の符号、
non-bipartite の実行エラー化、file hopping の対角拒否は整理されている。

一方、実装前に追記しておくと回帰検出力と入力安全性が上がる Medium/Low findings が残る。
特に `green_from_scratch` の積順序は U=0 テストだけでは検出できないため、非可換な `B_l` で
独立検証を追加した方がよい。

## Findings

### Medium: `green_from_scratch` の積順序を非可換な `B_l` で独立検証していない

該当箇所:

- 計画書 Task 6 `green_from_scratch`
- 計画書 Task 6 `tests/test_green_init.c`

Task 6 の実装では、巡回積
`B_{l0-1}...B_0 B_{L-1}...B_{l0}` の順序が正しさの核心になる。
しかし現在のテストは U=0 で全 `B_l` が同一なので、行列積が可換になり、順序バグを検出できない。
Task 7/8 は `from_scratch` との自己整合を見ているため、`from_scratch` 自体の順序違いを独立には潰せない。

修正案:

- Task 6 に U>0 の小系テストを追加する。
- `green_build_B` で各 `B_l` を作り、素朴な明示積で `P = B_{l0-1}...B_{l0}` を構成する。
- `g_ref = (I+P)^{-1}` と `green_from_scratch(l0)` を比較する。
- `l0=0` だけでなく `l0=1` も試し、巡回順の off-by-one を検出する。

### Medium: 入力 parser が未知 key と不正数値をまだ黙って受け流す

該当箇所:

- 計画書 Task 11 `params_read`

`lattice` と `bc` の typo は検出する計画になったが、未知 key はまだ無視される。
また、`atoi`/`atof` は `Lx=abc` や `dtau=foo` を 0 として処理するため、入力ミスが
別の物理モデルや invalid parameter として静かに流れる可能性がある。

修正案:

- 未知 key は `ERROR: unknown key ...` として返す。
- 整数・実数は `strtol`/`strtod` と `endptr` で全体が数値として読めたことを確認する。
- `Lx>0`, `Ly>0`, `dtau>0`, `U>=0`, `stab_interval>0`, `nwarm>=0`, `nmeas>=nbin`,
  `pbc in {0,1}`, `beta_list` の各 `beta>0` を検証する。
- `beta_list` が 64 個を超えた場合も黙って切り捨てずエラーにする。

### Medium: UDV の stress test が足りない

該当箇所:

- 計画書 Task 2 `tests/test_udv.c`

現在の UDV テストは中程度条件数の行列で、素朴逆行列との一致を見る。これは配線確認として有用だが、
低温で問題になる強いスケール分離、`D_b/D_s` 分割、`diag(1/Db)` の列作用のバグは露出しにくい。

修正案:

- 対角スケール `1e±k` を含む積、または `B = Q diag(exp(a_i)) Q^T` 型の積で stress test を追加する。
- 素朴逆行列と直接比較できない場合は、`(I+P)g ≈ I` の残差を評価する。
- 低温想定の `k` を複数段階にし、`stab_interval` 調整時の基準にも使えるようにする。

### Medium: 2D 検証対象の説明に古い `4×4` 記述が残っている

該当箇所:

- 計画書 Task 3 の 2×2 PBC 注
- 計画書 Task 13 `2D の注意`
- 設計書 §6 `ED/TPQ 照合`

Task 3 の注には「2D の定量検証は 4×4 など」とあるが、後段では grand-canonical ED は小 OBC 系に限定し、
`4×4` は ED には大きすぎ TPQ 向けと整理済みである。実装者が検証対象を誤解しないよう、
Task 3 の注も後段の方針へ揃えた方がよい。

修正案:

- Task 3 の注を `2×2/2×3/2×4 OBC` などの小 OBC 系へ変更する。
- `4×4` は TPQ・将来検証候補であり、grand-canonical ED の定量照合対象ではないと明記する。

### Low: `lattice_from_file` の malformed file エラー経路で `L` が leak する

該当箇所:

- 計画書 Task 14 `lattice_from_file`

`alloc_lat(L,n)` 後、行列要素の `fscanf` が途中で失敗すると `fclose(fp)` だけで返る。
短命 CLI では実害は小さいが、失敗経路でも `lattice_free(L)` を呼ぶ方が堅い。

修正案:

```c
if(fscanf(fp,"%lf",&v)!=1){
    fclose(fp);
    lattice_free(L);
    return 1;
}
```

### Low: `nmeas` が `nbin` で割り切れない場合に測定を黙って捨てる

該当箇所:

- 計画書 Task 11 `main`

`per = nmeas / nbin` として各 bin に同じ測定数を割り当てているため、
`nmeas % nbin != 0` の余りは使われない。統計上の大事故ではないが、再現性と入力意図の明確さのため、
仕様として扱いを決めた方がよい。

修正案:

- 単純にするなら `params_read` で `nmeas % nbin == 0` を要求する。
- 余りを使うなら、bin ごとのサンプル数を可変にし、重み付き平均または bin サイズを揃える設計にする。

## 結論

実装着手を止める Critical/High はない。実装前に計画へ追記する優先順は次の通り:

1. `green_from_scratch` の非可換積テスト
2. 入力 parser の厳格化
3. UDV stress test
4. 2D 検証記述の修正
5. `lattice_from_file` の失敗経路 cleanup
6. `nmeas % nbin` の扱い明確化

これらを反映すれば、実装フェーズでの silent wrong model、行列積順序バグ、低温安定化バグの検出力が上がる。
