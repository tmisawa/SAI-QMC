---
date: 2026-06-25
datetime: 2026-06-25 10:26 JST
model: Codex (GPT-5)
summary: |
  commit f815254 で第3回レビュー findings を反映した設計書・実装計画を第4回レビューした。
  VALIDATION テンプレート、設計書 §8、入力 key alias、frontmatter、self-bond 回避は概ね反映済み。
  残課題は H0/E_gc 表記の曖昧さ、縮退サイズの二部性判定、non-bipartite sign の扱い、
  strncpy 終端保証と bc alias の typo 検出である。
---

# 有限温度補助場 QMC 設計・実装計画 第4回レビュー

## 対象

- commit: `f815254 docs: Codex 第3回レビュー反映（文書整合性）`
- 設計書: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md`
- 実装計画: `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md`
- 第3回レビュー: `docs/reviews/2026-06-25-finite-T-aux-field-qmc-plan-third-review.md`

## 総評

第3回レビューの 5 findings は概ね反映されている。`VALIDATION.md` テンプレートと設計書 §8 の出力列は新 12 列に統一され、入力 key alias、PBC self-bond 回避、`VALIDATION.md` frontmatter も追加された。前回までの主要な物理・数値ブロッカーは見当たらない。

残っている問題は、実装前に潰しておくとよい仕様の曖昧さと入力安全性である。特に設計書 §5 の `H` / `H0` 表記は、過去に問題になった μ 二重カウントを再誘発しやすいので、早めに統一した方がよい。

## Findings

### Medium: 設計書 §5 に μ 二重カウントを再誘発する曖昧表記が残っている

該当箇所:

- 設計書 §5 `ED/TPQ との整合`

設計書では次のように書かれている。

```text
本実装のモデルは U n↑n↓ − μΣn
E_gc(=⟨H−μN⟩)
```

ここで `H` が μ を含む Hamiltonian なのか、μ を含まない `H0` なのかが曖昧である。計画書 Task 13 では `H0=K_hop+U n↑n↓` と明確に定義しているので、設計書も同じ記法に統一するべきである。

修正案:

```text
H0 = K_hop + U n↑n↓
DQMC の重みは exp[-β(H0−μN)], μ=U/2
E_hub = <H0>
E_gc  = <H0−μN>
E_ph  = <H0 − (U/2)N + (U/4)n_site>
```

### Medium: 縮退サイズで二部性判定がまだ誤る

該当箇所:

- 計画書 Task 3 `lattice_chain`
- 計画書 Task 3 `lattice_square`

PBC の self-bond 回避は入ったが、二部性判定はまだ実際に周期 bond が存在しない方向も見ている。

例:

- `lattice_chain(Lx=1, pbc=1)` は self-bond を回避して hopping なしになるが、`Lx` が奇数なので非二部扱いになる。
- `lattice_square(Lx=1, Ly=4, pbc=1)` は x 方向 bond が存在せず、実質 4-site chain PBC なので二部だが、`Lx` が奇数なので非二部扱いになる。

修正案:

```c
L->is_bipartite = (pbc && Lx>1 && (Lx%2!=0)) ? 0 : 1;

int odd = (pbc && Lx>1 && (Lx%2!=0)) ||
          (pbc && Ly>1 && (Ly%2!=0));
L->is_bipartite = odd ? 0 : 1;
```

テストにも `lattice_chain(1, ..., pbc=1)` と `lattice_square(1,4,...,pbc=1)` を追加するとよい。

### Low: non-bipartite 実行時の `sign` 列は絶対符号ではなく相対符号

該当箇所:

- 計画書 Task 10 `dqmc_init`
- 計画書 Task 10 `dqmc_sweep`

`D->sign` は初期値 `1.0` で、受理比が負のときに反転する設計である。しかし初期補助場配置の determinant sign を計算していないため、sign-free でない系では絶対的な sign を表していない。

本プロジェクトは半充填二部格子を主スコープにしているので、これは実装ブロッカーではない。ただし non-bipartite の警告を出す設計にした以上、`sign` 列が一般 sign として正しいと誤解される可能性がある。

修正案:

- 非二部・非半充填は sign 列未サポートと明記する。
- あるいは一般 sign をサポートするなら、初期配置の determinant sign を計算して `D->sign` を初期化する。

### Low: `strncpy` の null 終端保証がない

該当箇所:

- 計画書 Task 11 `params_read`
- 計画書 Task 14 `latfile`

`strncpy(p->lattice,val,15)` は入力が 15 文字以上の場合に null 終端を保証しない。Task 14 の `latfile` も同様の方針で追加予定である。

修正案:

```c
strncpy(p->lattice, val, sizeof(p->lattice)-1);
p->lattice[sizeof(p->lattice)-1] = '\0';
```

`latfile` も同様にする。

### Low: `bc` alias が typo を silent OBC にする

該当箇所:

- 計画書 Task 11 `params_read`

現状の `bc` alias は、`periodic` または `1` 以外をすべて `0` として扱う。例えば `bc=periodc` の typo が silent に OBC として走る。

修正案:

```c
if(!strcmp(val,"periodic") || !strcmp(val,"pbc") || !strcmp(val,"1")) p->pbc=1;
else if(!strcmp(val,"open") || !strcmp(val,"obc") || !strcmp(val,"0")) p->pbc=0;
else { fprintf(stderr,"ERROR: unknown bc=%s\n",val); return 1; }
```

## 第3回 Findings の反映状況

- `VALIDATION.md` 案の古い出力列: 反映済み。
- 設計書 §8 の古い出力仕様: 反映済み。
- 入力 key 名の不一致: alias 追加で反映済み。ただし typo 検出を強めたい。
- `square` の self-bond: self-bond 回避は反映済み。ただし二部性判定に縮退サイズの残課題あり。
- `VALIDATION.md` frontmatter: 反映済み。

## 結論

実装開始を妨げる critical/high finding はない。残る修正は、設計書の `H0` 表記統一、縮退サイズの二部性判定、入力文字列処理の安全性、non-bipartite sign の注記である。

これらを直したうえで実装に入れば、計画文書としては十分に堅い。
