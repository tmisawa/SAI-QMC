---
date: 2026-07-01
status: review
topic: DQMC sign stabilization implementation review
target: 案B DQMC sign stabilization implementation
reviewer: Codex
---

# DQMC Sign Stabilization Implementation Review

## Review Scope

対象:

- `src/green.h`
- `src/green.c`
- `src/dqmc.h`
- `src/dqmc.c`
- `src/linalg.h`
- `src/linalg.c`
- `src/replica_run.c`
- `src/main.c`
- `Makefile`
- `tests/test_dqmc.c`
- `tests/test_green_init.c`
- `tests/test_udv.c`
- `tests/test_sign_regression_slow.c`

照合元:

- `docs/superpowers/plans/2026-07-01-dqmc-sign-stabilization.md`
- `docs/reviews/2026-07-01-dqmc-sign-stabilization-plan-review.md`

観点:

- `Dqmc.sign` が初期化・periodic 安定化・sweep 末尾再安定化で厳密 determinant sign に戻るか
- `green_from_scratch` / UDV sign 計算の失敗時に stale sign を使わないか
- `Dqmc.status` が replica 実行経路へ伝播し、壊れた sign/bin を下流へ流さないか
- slow regression が通常 `make test` から分離されているか
- serial / MPI / OpenMP / hybrid のビルド・テストが壊れていないか

## Findings

重大な問題は見つからなかった。

案Bの中核実装は計画どおりで、前回 plan review の High 指摘も反映されている。
特に以下を確認した。

- `Dqmc.sign` は初期化時、periodic 安定化後、sweep 末尾の `green_from_scratch(0)` 後でリセットされる。
- `Dqmc.status` は `dqmc_sweep` の no-op 化と `replica_run` の fail-fast に使われ、数値破綻を bin 集計へ持ち込まない。
- `Green.det_sign` は失敗時に `0` sentinel になり、前回値は使われない。
- determinant sign は `la_logdet(G)` ではなく UDV factor から取得する実装になっており、低温・強結合でより堅い方向に改善されている。
- slow regression は `tests/test_*_slow.c` として通常 `make test` から除外され、`make test_slow` で実行される。

## Implementation Notes

`udv_inv_one_plus` は `g = (1 + U diag(D) T)^-1` を作る既存処理に加え、
`det_sign` を返すようになった。

符号は次の分解から計算されている。

```text
sign(det(1 + U D T)) = sign(det U) * sign(det Db) * sign(det M)
```

ここで `M = Db^-1 U^T T^-1 + Ds`。既存の `g` 再構成は保たれており、
`tests/test_udv.c` と追加ストレスで brute-force `sign(det(I+P))` と一致することを確認した。

## Verification

ローカルで実行した確認:

```text
make test                                      OK
make test_slow                                 OK
make dqmc                                      OK
make dqmc_mpi                                  OK
make test_mpi                                  OK
make dqmc_omp && make test_omp                 OK
make dqmc_hybrid && make test_hybrid           OK
git diff --check                               OK
```

追加確認:

- UDV determinant sign のランダム小行列ストレス 5000 ケース:
  - brute-force `la_logdet(I+P)` との mismatch は 0。
- `./dqmc input/1d_L4_U4.txt`:
  - 全温度で `sign=1`。

## Residual Risk

- HPC 実機の compiler / MPI / BLAS 差分まではこのローカルレビューでは未確認。
- `LOG.md` や HPC scripts/data など、今回のレビュー対象外の既存 dirty worktree 差分は確認対象外。
- `tests/test_sign_regression_slow.c`、この implementation review、plan review、計画書は untracked 状態なので、コミット対象なら明示的に add する必要がある。

## Conclusion

案Bの実装は正しく入っていると判断する。

sign reset の本命である sweep 末尾 `green_from_scratch(0)` 後のリセット、periodic 安定化後の防御的
リセット、stale sign を使わない fail-fast、slow regression の分離が揃っている。
ローカルの serial / MPI / OpenMP / hybrid 検証も通っており、実装を進めてよい状態。
