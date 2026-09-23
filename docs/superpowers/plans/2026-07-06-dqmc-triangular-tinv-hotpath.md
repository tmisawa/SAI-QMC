---
date: 2026-07-06
datetime: 2026-07-06 JST
status: plan
topic: DQMC hotpath improvement - udv_inv_one_plus の T^{-1} を LU 逆行列から三角行列処理へ置換
summary: |
  UDV スタック導入後の hotpath である udv_inv_one_plus_work について、
  単位上三角因子 T の逆行列を汎用 LU 逆行列 la_inverse_work で計算している箇所を
  LAPACK dtrtri に置き換える短期実装計画。初回実装は数式経路を変えず
  T^{-1} 生成だけを差し替え、効果と回帰を測ってから dtrsm 化を別段階で検討する。
---

# DQMC `udv_inv_one_plus` T^{-1} 三角化 Implementation Plan

## Goal

`src/linalg.c` の `udv_inv_one_plus_work` で、UDV 因子 `T` が単位上三角であるにもかかわらず
`la_inverse_work` (`dgetrf` + `dgetri`) で `T^{-1}` を作っている箇所を、
LAPACK の三角行列ルーチンへ置換する。

初回の到達点は **`dtrtri` で `T^{-1}` を作る最小差分**とする。既存の
`U^T T^{-1}`、`M`、`M^{-1}`、`g = T^{-1} ...` の数式経路は変えない。
これにより、速度・精度の改善を狙いつつ、回帰原因を「T の逆行列生成方法」に限定する。

## Scope

この plan が扱うもの:

1. `dtrtri_` の Fortran LAPACK 宣言追加。
2. `udv_inv_one_plus_work` 内の `la_inverse_work(n, s->T, Tinv, w)` を
   `dtrtri("U", "U")` ベースへ置換。
3. 既存テストと低温・悪条件 UDV ケースでの回帰確認。
4. `profile=1` による `udv_inv_one_plus` / `green_from_stack` の効果測定。

この plan では扱わないもの:

- `T^{-1}` を明示生成しない `dtrsm` 化。
- `M^{-1}` の solve 化。
- UDV 分解方式そのものの変更。
- `udv_rmul` の安定化改修。

`dtrsm` 化は、`dtrtri` 版を安定基準として保存してから別コミットで検討する。

## Current Hotpath

対象コード:

- `src/linalg.c`: `udv_inv_one_plus_work`
- 現行処理:
  - `Tinv = inverse(T)` を `la_inverse_work` で計算。
  - `UT_Tinv = U^T Tinv`。
  - `M = Db^{-1} U^T T^{-1} + Ds`。
  - `Minv = inverse(M)`。
  - `g = T^{-1} Minv Db^{-1} U^T`。

問題点:

- `T` は UDV の規約上、単位上三角。
- 汎用 LU 逆行列は pivoting と一般密行列処理を含み、三角構造を捨てている。
- UDV スタック後は `green_from_stack` が頻繁に `udv_inv_one_plus_work` を呼ぶため、
  ここは直接 hotpath に効く。

## Design

### Step 1: `dtrtri_` を追加

`src/linalg.c` の LAPACK extern 宣言群に追加する。

```c
extern void dtrtri_(const char *, const char *, const int *, double *,
                    const int *, int *);
```

使う指定:

- `uplo = 'U'`: upper triangular。
- `diag = 'U'`: unit diagonal。
- `n`: 行列サイズ。
- `lda = n`。

### Step 2: `Tinv` 生成だけを置換

現行:

```c
if (la_inverse_work(n, s->T, Tinv, w) != 0) {
    PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
    return 1;
}
```

置換案:

```c
memcpy(Tinv, s->T, sizeof(double) * (size_t)n * (size_t)n);
int info_t = 0;
const char uplo = 'U';
const char diag = 'U';
dtrtri_(&uplo, &diag, &n, Tinv, &n, &info_t);
if (info_t != 0) {
    PROF_END(prof, PROF_UDV_INV_ONE_PLUS, t_udv_inv_one_plus);
    return 1;
}
```

`diag='U'` なので、対角が厳密に 1.0 で格納されていることに依存しない。ただし既存 UDV
不変条件では `T` の対角は 1.0 であり、`tests/test_udv_stack.c` でも確認済み。

### Step 3: 数式経路は維持

以下は初回差分では変更しない。

- `la_gemm(n, 1, 0, 1.0, s->U, Tinv, 0.0, UT_Tinv)`
- `M` の構築。
- `la_inverse_work(n, M, Minv, w)`
- `tmp`, `gp`, `g` の構築。
- det sign 計算。

理由:

- 初回の回帰範囲を最小化する。
- `dtrsm` 化では左右・転置・行列配置の間違いが起きやすい。
- `dtrtri` 版を基準にすれば、次段階の `dtrsm` 化の正否を比較しやすい。

## Tests

### Required

- [ ] `make test`
- [ ] `tests/test_udv_stack` が det sign と UDV product を維持すること。
- [ ] `tests/test_green_stack` が stack 経路と scratch 経路の一致を維持すること。
- [ ] `tests/test_dqmc_stab_drift` が従来閾値内に収まること。

### Recommended Additions

- [ ] `tests/test_udv_stack.c` に悪条件 UDV ケースを追加する。
  - 既存の `S1`, `Mix`, `S2` ケースに加え、`T` が非自明な単位上三角になる構成を使う。
  - `udv_inv_one_plus_work` の `det_sign` を dense direct `la_logdet(1 + product)` と比較する。
  - `G` については direct inverse との Frobenius 相対誤差を確認する。
- [ ] 低温 slow test を可能なら実行する。
  - `make test_slow` または `tests/test_green_stack_lowtemp_slow`。

期待値:

- bit 完全一致は要求しない。
- `det_sign` は一致必須。
- `G` 差分は既存許容範囲内。
- observable は既存統計誤差内。

## Benchmark

最低限:

- [ ] `profile=1` で `udv_inv_one_plus` の total sec と call count を比較。
- [ ] `green_from_stack` の total sec を比較。

推奨ケース:

1. 小さなローカル smoke:
   - 8x8 または 16x16。
   - `beta=4`, `dtau=0.1`, `stab=4`。
2. hotpath が見える低温ケース:
   - 16x16, `beta=16`, `dtau=0.1`, `stab=4`。
3. production 近傍:
   - 2D 6x6, `U=4`, `dtau=0.025`, `beta=2,4,8`, `stab=4`。

判定:

- `udv_inv_one_plus` が同等または短縮していること。
- `green_from_stack` が悪化しないこと。
- `dqmc_sweep` 全体では改善が小さくても可。まず hotpath の局所改善を確認する。

## Risks

- **LAPACK availability**: `dtrtri` は標準 LAPACK なので Accelerate/MKL/OpenBLAS で利用可能なはず。
- **Fortran symbol mismatch**: 既存コードと同じ `dtrtri_` 形式で宣言する。
- **数値差分**: LU pivoting から三角逆行列に変わるため、最終 bit は変わり得る。
  ただし `T` が単位上三角である限り、数学的にはこちらが正しい専用経路。
- **隠れた UDV 不変条件破れ**: `T` の下三角や対角が壊れている場合、`dtrtri` はそれを前提として進む。
  既存 `test_udv_stack` の `check_unit_upper` を維持し、必要なら production 前に debug 診断を追加する。

## Follow-up: `dtrsm` 化

`dtrtri` 版でテストとベンチが通った後、次段階で `Tinv` 明示生成を避ける。

候補:

- `UT_Tinv = U^T T^{-1}` は、右側三角 solve として `X T = U^T` を解く。
- `g = T^{-1} gp` は、左側三角 solve として `T g = gp` を解く。

これは `dtrsm` の `side`, `uplo`, `trans`, `diag` 指定を間違えやすいため、
本 plan の初回実装には含めない。

## Acceptance Criteria

- [ ] `dtrtri_` 経路が実装され、`la_inverse_work(n, s->T, ...)` が消えている。
- [ ] `make test` が通る。
- [ ] 追加または既存の UDV/Green stack テストで det sign と `G` の整合が確認されている。
- [ ] 少なくとも 1 ケースで `profile=1` の before/after が保存されている。
- [ ] 結果を `LOG.md` または対応 docs に記録する。
