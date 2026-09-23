---
date: 2026-06-25
datetime: 2026-06-25 09:24 JST
model: Codex (GPT-5)
summary: |
  AF_QMC の有限温度補助場 QMC 設計書・実装計画をレビューした。
  全体方針は妥当だが、補助場 flip 更新の N の符号、ED/TPQ 比較の ensemble、
  エネルギー定義と出力、Trotter 誤差の扱いに実装前修正が必要。
---

# 有限温度補助場 QMC 設計・実装計画レビュー

## 対象

- 設計書: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md`
- 実装計画: `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md`
- 関連情報: `AGENTS.md`, `LOG.md`, `references/格子上の電子系における乱れ及び相互作用の効果【大塚雄一】[博士論文_200203].md`

## 総評

設計の大枠は妥当である。半充填 Hubbard 模型に限定し、古典的 BSS/DQMC を C11 と BLAS/LAPACK で実装し、U=0 解析解から ED/TPQ 照合へ進む検証ラダーも自然である。行列格納を column-major に統一し、Green 関数規約 `g_ij = <c_i c_j†>` を明示している点もよい。

一方で、実装計画に含まれるコード断片には、そのまま実装すると U>0 の結果を壊す可能性が高い箇所がある。特に補助場 flip の `N_{imσ}` の使い方は critical で、受理率と rank-1 更新の両方に影響する。加えて、ED/TPQ 比較の ensemble、エネルギー出力、Trotter 誤差の扱いは、検証計画として修正が必要である。

## Findings

### Critical: 補助場 flip の `N` の符号が逆

該当箇所:

- `plans/...finite-T-aux-field-qmc.md` の Task 5 `field_N`
- Task 7 `green_ratio`
- Task 7 `green_update`

参照式 A.92-A.93 では、場を `s -> -s` に反転するときの差分行列を

```text
N_{imσ} = exp(-2 λ σ s_{im}) - 1
```

としている。ここでの `s_{im}` は反転前の補助場である。したがって、現在の場がまだ反転前である `green_ratio()` では `field_N(..., s_old)` を使う必要がある。

計画書では次の形になっている。

```c
double N=field_N(G->f,G->sigma,(signed char)(-s_old));  /* 反転後 */
```

また、`green_update()` は呼び出し側で field を反転済みにした後に呼ぶ設計だが、そのまま `s_now` を渡している。

```c
signed char s_now=G->f->s[G->cur_l*n+i];   /* 反転後 */
double N=field_N(G->f,G->sigma,s_now);
```

これはどちらも逆である。反転後の場 `s_now = -s_old` しか参照できない場合は、`field_N(..., -s_now)` とする必要がある。

修正案:

```c
double green_ratio(const Green *G,int i){
    int n=G->n;
    signed char s_old=G->f->s[G->cur_l*n+i];
    double N=field_N(G->f,G->sigma,s_old);
    return 1.0 + (1.0 - G->g[i+i*n])*N;
}

void green_update(Green *G,int i){
    int n=G->n;
    signed char s_now=G->f->s[G->cur_l*n+i];
    double N=field_N(G->f,G->sigma,(signed char)(-s_now));
    ...
}
```

この修正に合わせて、Task 7 のテストも `Nflip=field_N(..., f.s[...])` に直すべきである。

### High: ED/TPQ 比較で canonical と grand-canonical を同一視している

該当箇所:

- 計画書 Task 13 の ED/TPQ 比較
- `canonical(ED/TPQ, 固定 N=半充填) と grand canonical(QMC) は...一致する` という記述

DQMC は `μ=U/2` の grand-canonical ensemble である。粒子正孔対称点で `<N>=N_site` になることと、固定粒子数 `N=N_site` の canonical ensemble と有限温度・有限サイズで同一になることは別である。

小クラスタの ED/TPQ と比較する場合、この差は無視できない可能性がある。特に本プロジェクトの検証対象は 1D L=4-6 や 2D 2x2 であり、有限サイズ効果が大きい。

修正案:

- ED 側も grand-canonical trace を計算し、同じ `μ=U/2` を含む Hamiltonian で比較する。
- canonical ED/TPQ を使う場合は、DQMC 側の grand-canonical 結果と直接一致するとは書かず、差があり得る検証項目として扱う。
- 検証文書では `比較対象は grand-canonical ED を原則とする` と明記する。

### High: エネルギー定義と出力が比較要件を満たしていない

該当箇所:

- 設計書 §5 `ED/TPQ との整合`
- 計画書 Task 9 `measure_sample`
- 計画書 Task 11 `main`

設計書では、比較時に規約変換できるように次を個別出力するとしている。

- `<K_hop>`
- `U * Σ<n_up n_down>`
- `Σ<n_up + n_down>`

しかし計画書の `MeasSample` には粒子数がなく、main の出力も `T E dE doublon dD sign` だけである。このままでは、次のエネルギーを明確に比較できない。

- hopping + interaction の物理 Hubbard エネルギー
- `U n_up n_down - μN` を含む grand-canonical Hamiltonian のエネルギー
- 粒子正孔対称形 `U(n_up-1/2)(n_down-1/2)` のエネルギー

修正案:

`MeasSample` を拡張する。

```c
typedef struct {
    double E, doublon, ekin, eint;
    double ntot;
    double emu;
    double eph;
} MeasSample;
```

測定では次を出す。

```text
T E_hub dE_hub E_gc E_ph ntot doublon sign
```

少なくとも `ekin`, `eint`, `ntot` は個別に出力する。

### High: ED との許容差が Trotter 誤差を無視している

該当箇所:

- 設計書 §6 `Δτ→0 外挿`
- 計画書 Task 13 `|E_qmc - E_ED| < 2*dE_qmc`

DQMC の有限 `dtau` には Trotter 誤差があり、計画書自身も `O(dtau^2)` 外挿を検証ラダーに入れている。にもかかわらず、ED との判定条件が統計誤差だけになっている。

`U=4,8` では、統計誤差を小さくすると Trotter 誤差が支配的になる可能性がある。この状態で `2*dE_qmc` だけを許容差にすると、正しい実装でも検証失敗になる。

修正案:

- ED 比較は `dtau=0.2, 0.1, 0.05` などの `dtau^2` 外挿後に行う。
- 外挿前の比較では、判定条件を `統計誤差 + Trotter 系統誤差` と明記する。
- Task 13 の許容差を `外挿後の E(dtau->0) が ED と誤差内で一致` に変更する。

### Medium: 「半充填なら符号問題なし」の前提を lattice 生成側が保証していない

該当箇所:

- 設計書 §1 `二部格子・粒子正孔対称`
- 計画書 Task 3 `lattice_chain`, `lattice_square`

odd-length chain の PBC や、奇数サイズを含む square PBC は二部格子ではない。計画書の lattice 実装は機械的に `bipart` を割り当てるだけで、二部性を検証していない。

修正案:

- `Lattice` に `int is_bipartite` を追加する。
- `chain` では `pbc && Lx % 2 != 0` を非二部として扱う。
- `square` では PBC 方向のサイズが奇数なら非二部として扱う。
- half-filling sign-free 検証では、非二部格子をエラーまたは警告にする。

### Medium: 2x2 PBC の二重結合規約が ED 比較と衝突し得る

該当箇所:

- 設計書 §6 の 2D 2x2 検証
- 計画書 Task 3 の `2x2 PBC` コメント

計画書は 2x2 PBC で同じ隣接ペアへの hopping を重複加算する方針を採っている。これは規約としてはあり得るが、ED/TPQ 側が同じ hopping matrix を使わないと比較がズレる。

修正案:

- ED/TPQ 入力を DQMC の hopping matrix から出力・生成する。
- あるいは、2x2 PBC は検証対象から外し、2D は 4x4 など二重結合の曖昧さが出ない系にする。
- 2x2 を使うなら、`t_ij=-2` になる bond を明示した reference ED を用意する。

### Medium: 任意格子ファイル入力が計画に入っていない

該当箇所:

- 設計書 §1 `任意格子のファイル入力`
- 計画書 Task 11 `lattice=chain|square`

設計書は任意 hopping matrix のファイル入力をスコープに入れているが、実装計画には対応タスクがない。Self-review の `設計書 §3 モジュールを全実装` は過大評価である。

修正案:

- Task 14 として `lattice=file` を追加する。
- ファイル形式は edge list か dense matrix のどちらかに固定する。
- ED/TPQ 比較との整合を考えると、hopping matrix の dump 機能も併せて入れるとよい。

### Medium: `beta/dtau` 丸め後の実効温度が出力とズレる

該当箇所:

- 計画書 Task 11 `Ltr=(int)lround(beta/p.dtau)`
- 同じ箇所で `T=1.0/beta`

`beta/dtau` が整数でない場合、実際に計算される逆温度は `beta_eff = Ltr * dtau` である。しかし出力は入力された `beta` から `T` を計算している。

修正案:

- `fabs(beta/p.dtau - round(beta/p.dtau))` が閾値を超えたらエラーにする。
- または `beta_eff = Ltr * p.dtau` を採用し、出力にも `1/beta_eff` を使う。

### Low: git add のパスが repository root と合っていない

該当箇所:

- 計画書の各 commit 手順

`AF_QMC` 自体が git repository であるため、`cd AF_QMC` 後に `git add AF_QMC/src/...` を実行するとパスが合わない。

修正案:

```bash
git add Makefile src/linalg.h src/linalg.c tests/test_linalg.c
```

のように、repository root からの相対パスに直す。

### Low: `main.c` の include 不足が本文で後追いになっている

該当箇所:

- 計画書 Task 11 `main.c`

`strcmp` を使うのに `<string.h>` がコード断片に含まれておらず、直後の注で追加するように書かれている。実装者がコードブロックをそのまま貼ると警告またはエラーになる。

修正案:

コードブロック本体に最初から `#include <string.h>` を入れる。

## 追加で強化したいテスト

### U>0 の Green 更新テストを符号修正後に必ず実行する

Task 7 のテストは有効だが、現在の `N` の定義が間違っているため、まず期待値側を修正する必要がある。修正後は次を確認する。

- `green_ratio()` が直接 determinant 比と一致する。
- field flip + `green_update()` が `green_from_scratch()` と一致する。
- up/down の両スピンで同じテストを行う。

### `B_l` の明示テストを追加する

`green_build_B()` について、1 site または 2 site の手計算可能なケースで

```text
B_lσ = exp(-dtau K) * diag(exp(lambda σ s_il - dtau U/2))
```

の列スケールが正しいことを確認する。ここは column-major と右掛け対角行列の取り違えが起きやすい。

### 粒子数 `<N>` の half-filling テストを追加する

`μ=U/2` で `<N> = N_site` になることを、U=0 と U>0 の小サイズで確認する。ED/TPQ 比較の前提確認として有用である。

### 二部性チェックのテストを追加する

次を明示的にテストする。

- 4-site chain PBC: bipartite
- 3-site chain PBC: non-bipartite
- 4x4 square PBC: bipartite
- 3x4 square PBC: non-bipartite

## 実装前の優先修正リスト

1. Task 7 の `N` の符号を修正する。
2. Task 13 の ED/TPQ 比較を grand-canonical ED 原則に直す。
3. `MeasSample` と出力に `ntot`, `ekin`, `eint`, 必要なら `E_gc`, `E_ph` を追加する。
4. ED 比較の判定条件を `dtau^2` 外挿後に変更する。
5. lattice の二部性チェックを追加する。
6. `beta/dtau` の整数性チェックまたは `beta_eff` 出力を追加する。
7. 任意格子ファイル入力を別タスクとして追加するか、設計書のスコープから外す。

## 結論

この計画は、DQMC の初期実装に必要なモジュール分割と検証ラダーをかなり具体的に押さえている。ただし、現状のまま実装を開始すると、補助場更新の符号ミスにより U>0 の物理結果が誤る可能性が高い。また、ED/TPQ との比較条件が ensemble とエネルギー定義の面で曖昧であり、正しい実装でも検証に失敗する余地がある。

最初に Task 7, Task 9, Task 13 を修正し、その後に TDD 実装へ進むのが安全である。
