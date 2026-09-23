---
date: 2026-07-14
datetime: 2026-07-14 16:04 JST
model: GPT-5 Codex
summary: |
  Phase 2.1 として、既存 `udv_scale_file` 診断を単一 factor D の成長 site に
  小さく拡張する計画。
  既存 TSV schema と production stdout は維持し、end-of-sweep left UDV と
  stack build 内 lmul/rmul 直前の scale を記録する。
  plan review の C1-C3 を反映し、単一 factor 行の right sentinel、tau の意味、
  direction label の前方一致衝突を明確化した。
---

# UDV Scale Diagnostics Phase 2.1 Plan

対象: `udv_scale_file` 診断の追加記録 site

## 背景

Phase 2 では、`green_from_stack()` / `green_from_boundary_factors()` に入る直前の
prefix/suffix pair を記録した。その結果、L=4, U=16, beta=33.325 の再現 seed で
`cross_max_logD=710.68162208088245` が記録され、直後に
`udv_combine stage=C` overflow で停止した。

Phase 2 review では、さらに `left_max_logD=708.14631542658333` が
`ln(DBL_MAX) ~= 709.78` に近く、full beta では単一 factor `D` 自体も double 範囲を
超える可能性が高いと判断された。

Phase 2.1 の目的は、two-sided 実装前後で次に問題になる単一 factor site を
小さい変更で直接測れるようにすること。

## Scope

やること:

- 既存 `udv_scale_file` に追加 row を出す。
- 既存 TSV schema は維持する。
- production default stdout は変更しない。
- `parallel=serial` 制限は維持する。
- 単位はすべて natural log units とする。

やらないこと:

- two-sided solver は実装しない。
- log-D / multi-factor storage は実装しない。
- `replicas.csv` の列は増やさない。
- 長 run 用の間引き option は今回は入れない。

## TSV Policy

既存 header は変えない。

```text
beta_index Ltr replica_id seed U dtau stab_interval sweep_count tau boundary
spin direction left_min_logD left_max_logD left_spread_logD
left_finite_count left_zero_count left_nonfinite_count
right_min_logD right_max_logD right_spread_logD
right_finite_count right_zero_count right_nonfinite_count cross_max_logD
```

単一 factor row では、対象 factor を left 側に入れる。

- `left_*`: 対象 UDV factor の `D` 統計。
- `right_min_logD`, `right_max_logD`, `right_spread_logD`: `nan`。
- `right_finite_count`, `right_zero_count`, `right_nonfinite_count`: `0`。
- `cross_max_logD`: `nan`。

実装上は単一 factor 用の emit 分岐を作り、right 側を明示的に `nan` / `0` で
書く。既存 `udv_scale_stats(NULL)` の sentinel
（`right_nonfinite_count=1`）は使わない。`1` だと「right factor に非有限値が
あった」と誤読されるため。

理由:

- 既存 parser / notebook が列数で壊れにくい。
- pair 診断と single-factor 診断を同じ file に時系列で並べられる。
- `direction` に site label を入れれば、新しい列を足さずに区別できる。

## New Direction Labels

既存:

- `forward`: forward sweep の boundary pair。
- `backward`: alternating backward sweep の boundary pair。

追加:

- `left_udv_forward`: forward end-of-sweep `green_from_left_udv()` 直前。
- `left_udv_backward`: alternating backward end-of-sweep `green_from_left_udv()` 直前。
- `stack_suffix_pre_rmul`: `green_stack_build_suffix()` 内の `udv_rmul()` 直前。
- `stack_prefix_pre_lmul`: `green_stack_build_prefix()` 内の `udv_lmul_work()` 直前。

`sweep_count` は既存と同じく「完了済み sweep 数」。第 1 sweep 中は `0`。
`direction` は完全一致で filter する。新規 label は既存 `forward` / `backward` と
前方一致しないようにする。

## Recording Sites

### 1. Forward end-of-sweep left UDV

場所:

```c
green_from_left_udv(&D->Gu, &D->left_u);
green_from_left_udv(&D->Gd, &D->left_d);
```

直前に記録する。

- spin `u`: `D->left_u`
- spin `d`: `D->left_d` only when `!D->use_ph`
- `tau=Ltr`
- `boundary=D->stack_u.M`
- `direction=left_udv_forward`

目的:

- full-beta prefix の単一 factor `D` headroom を測る。
- two-sided で boundary combine が通った後、end-of-sweep 単一 factor が残るか確認する。

### 2. Backward end-of-sweep left UDV

場所:

```c
green_from_left_udv(&D->Gu, &D->left_u);
```

直前に記録する。

- spin `u`
- `tau=0`
- `boundary=0`
- `direction=left_udv_backward`

目的:

- alternating backward 経路の単一 factor headroom を測る。

### 3. Stack suffix build pre-rmul

場所:

```c
udv_copy(&st->S[j], &st->S[j + 1]);
/* record st->S[j] here */
udv_rmul(&st->S[j], G->B, &G->work);
```

記録:

- `tau=st->b[j]`
- `boundary=j`
- `direction=stack_suffix_pre_rmul`
- spin `u` / `d` は caller context から渡す。

`tau` / `boundary` は destination boundary としてラベルする。suffix の pre-rmul
時点で factor 自体は `st->S[j + 1]` 由来の後続区間を被覆しているが、行の label は
これから作る `st->S[j]` の boundary に合わせる。

目的:

- suffix stack build 中の単一 factor `D` が `udv_rmul stage=D_T_B` に入る直前の scale を測る。

### 4. Stack prefix build pre-lmul

場所:

```c
udv_copy(&st->S[j], &st->S[j - 1]);
/* record st->S[j] here */
udv_lmul_work(&st->S[j], G->B, &G->work);
```

記録:

- `tau=st->b[j]`
- `boundary=j`
- `direction=stack_prefix_pre_lmul`
- spin `u`

`tau` / `boundary` は destination boundary としてラベルする。

目的:

- alternating prefix stack build 中の単一 factor `D` が `udv_lmul_work stage=B_U_D`
  に入る直前の scale を測る。

## Implementation Shape

`green.c` に file I/O や `Dqmc` 依存は入れない。

推奨する小変更:

1. `Green` に optional scale observer を持たせる。

```c
typedef int (*GreenScaleObserver)(void *ctx,
                                  const Green *G,
                                  const char *direction,
                                  int tau,
                                  int boundary,
                                  const UDV *factor);
```

2. `Green` に以下を追加する。

```c
GreenScaleObserver scale_observer;
void *scale_observer_ctx;
const char *scale_observer_spin;
```

3. `green_alloc()` / `green_free()` で null 初期化・解除する。

4. `green_stack_build_suffix()` / `green_stack_build_prefix()` で observer があれば
   pre-rmul / pre-lmul の直前に呼ぶ。

5. observer が nonzero を返した場合は `G->work.failed = 1` として loop を止める。

6. `Dqmc` 側で `D->Gu` / `D->Gd` に observer を設定する。

7. observer 実体は `dqmc.c` に置き、既存 `DqmcUdvScaleDiag` と同じ append helper を使う。

この形なら:

- `green.c` は generic callback だけ知る。
- `dqmc.c` が beta/seed/sweep_count/file path を持ったまま書ける。
- 既存 `green_stack_build_*()` の public API を変えずに済む。
- diagnostics が無効なら observer は null で runtime overhead は branch だけ。

## Tests

### Parser / default

Phase 2 の parser tests はそのまま維持。新しい input key は追加しない。

### DQMC unit test

`tests/test_dqmc.c` の既存 `udv_scale_file` test を拡張する。

条件:

- chain L=4
- Ltr=6
- stab=2
- 1 sweep
- `dqmc_enable_udv_scale_diag()` enabled

確認:

- 既存 boundary pair row が残る。
- `stack_suffix_pre_rmul` が 1 回以上出る。
- `left_udv_forward` が 1 回出る。
- file は 25 列を維持する。
- `right_*` が単一 factor row で `nan` / `0` になる。
- 全行の tab 数が 24 である。

### Runtime smoke

小さい serial `dqmc` 入力で:

- stdout は従来 header / observable のまま。
- `udv_scale_file` に追加 direction label が出る。

再現 seed の短縮 run では:

- combine overflow で止まる場合も、stack suffix rows と boundary rows が残る。
- `left_udv_forward` は combine overflow 前に到達しない場合があるため、再現 seed で必須にしない。

## Acceptance Criteria

Phase 2.1 完了条件:

1. `make test` が通る。
2. `make dqmc_mpi` が通る。
3. `mpirun -np 1 ./tests/test_dqmc_mpi` が通る。
4. `git diff --check` が通る。
5. default stdout は変わらない。
6. `udv_scale_file` の列数は既存 25 列のまま。
7. 追加 direction labels が TSV に出る。
8. observer file write failure は fail-fast で `D.status != 0` になる。

## Risks

- Stack build row が増えるため、診断 file はさらに大きくなる。
  - Phase 2.1 は短い diagnostic run 用と明記し、間引きは後続 option とする。
- `Green` observer が残ったまま別 run に流用されると誤記録になる。
  - `green_free()` で null に戻し、`dqmc_enable_udv_scale_diag()` で明示設定する。
- Phase 2.1 でも lmul/rmul の失敗直前を完全には取れない場合がある。
  - pre-rmul/pre-lmul row は「直前の factor scale」を測る。実際の dense multiply 内の
    `B` 由来 O(1) factor は含まない。

## Next After Phase 2.1

Phase 2.1 のデータで単一 factor wall が確認できた場合:

- Phase 3 two-sided solver は予定どおり進める。
- Phase 6 single-factor countermeasure の候補を、log-D と multi-factor storage の
  どちらに寄せるか判断する材料にする。
