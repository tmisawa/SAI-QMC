---
date: 2026-07-03
datetime: 2026-07-03 18:22 JST
model: Claude Opus 4.8 (1M context)
topic: Plan A（acceptance rate 出力）実装レビュー
summary: |
  2026-07-03-acceptance-and-shifted-spin-hst-plan.md の Plan A に沿って実装された
  acceptance rate 出力を、コード読解・全ビルド・全テスト・serial/MPI 数値一致で
  徹底検証した。バグは検出されず、仕様どおり・数値的に正しいと確認。設計上の注意点
  （sign 非重み診断量、jackknife の resampling 単位）を非ブロッキング補足として記録する。
---

# Acceptance Rate 出力（Plan A）実装レビュー

## 対象

- 計画: [docs/2026-07-03-acceptance-and-shifted-spin-hst-plan.md](2026-07-03-acceptance-and-shifted-spin-hst-plan.md) の **A. acceptance rate を測定・出力する**
- 変更ファイル（working tree、未コミット）:
  - `src/dqmc.c`, `src/dqmc.h`
  - `src/replica.c`, `src/replica.h`
  - `src/replica_mpi.c`, `src/replica_mpi.h`
  - `src/replica_run.c`
  - `src/main.c`
  - `tests/test_dqmc.c`, `tests/test_replica.c`, `tests/test_mpi_replica.c`
  - `scripts/*`（summary header と awk 取り込みの末尾 2 列対応）

## 結論

**バグは検出されなかった。** Plan A は仕様どおりに実装され、数値的にも正しい。
B（`shifted_spin`）着手前提として問題ない状態。

## 検証項目と結果

### 1. カウンタ増加ロジック（正しい）

- forward sweep [src/dqmc.c:241-243](../src/dqmc.c#L241-L243)、backward sweep
  [src/dqmc.c:416-419](../src/dqmc.c#L416-L419) の両方で、
  `accept_attempts++` を Metropolis 判定の前に無条件、
  `accept_accepted++` を採択時のみ増やす。
- two-spin 経路（`!use_ph`）と PH single-spin 経路（`use_ph`）の双方で
  local proposal ごとに 1 attempt。alternating mode の forward/backward も各
  `L×n` attempts で一貫。
- Plan A の定義 `acceptance = accepted_flips / attempted_flips` に一致。

### 2. warmup 除外（正しい）

- [src/replica_run.c:124-144](../src/replica_run.c#L124-L144)：measurement ループが
  各 sweep 前後で `D.accept_*` の差分を取り、`replica_bin_add_acceptance` で bin に加える。
- warmup sweep（[src/replica_run.c:96-98](../src/replica_run.c#L96-L98)）は累積カウンタを
  進めるが bin には一切加算されない。Plan A の「warmup は production output に混ぜない」を満たす。

### 3. bin acceptance と div-by-zero（正しい）

- [src/replica.c:60-74](../src/replica.c#L60-L74)：`accept_accepted / accept_attempts`。
  `attempts==0` で `0.0` を返すガードあり。

### 4. MPI pack/unpack round-trip（正しい）

- `REPLICA_MPI_BIN_DOUBLES` を 6→8 に更新（[src/replica_mpi.h](../src/replica_mpi.h)）。
- pack/unpack（[src/replica_mpi.c:97-99](../src/replica_mpi.c#L97-L99),
  [src/replica_mpi.c:121-123](../src/replica_mpi.c#L121-L123)）、
  `main.c` の全 `calloc`（local/all values）、`replica_mpi_gatherv_layout` すべてで
  マクロを一貫使用。バッファ長・変位の不整合なし。
- `unsigned long long → double → unsigned long long` の丸めは、実運用の attempts 数
  （`L×n×nmeas` 程度）が 2^53 以内のため厳密。

### 5. 出力形式（正しい）

- ヘッダ [src/main.c:445](../src/main.c#L445) と printf [src/main.c:161-162](../src/main.c#L161-L162)
  が両方 14 列。末尾に `acceptance dAcceptance` を追加し、既存列順は不変。
- `dAcceptance` は bin ごとの acceptance に対する jackknife error
  （[src/main.c:159](../src/main.c#L159)）。

### 6. jackknife の扱い（他 observable と一貫）

- [src/measure.c](../src/measure.c) の `jackknife` は bin ごとの値配列に対する
  素朴な leave-one-out resampling。acceptance も他 observable と同じ流儀で処理。
- 各 bin の attempts は `per = nmeas/nbin` で等しいため、単純平均は
  pooled `Σaccepted / Σattempts` に一致する。

## ビルド・テスト・数値確認

| 項目 | 結果 |
|------|------|
| `make test`（serial） | ALL TESTS PASSED（新規 assertion 含む） |
| `make test_mpi` | ALL MPI TESTS PASSED |
| serial / MPI / OMP ビルド | `-Wall -Wextra` で警告ゼロ |
| `1d_L4_U4`（serial 実行） | acceptance が T=2 で ~0.71 → 低温で ~0.54 と単調減少（物理的に妥当） |
| **serial vs MPI np=3（4 replica を不均等分割）** | **acceptance mean・error がバイト単位で完全一致** |

新規テスト assertion:

- `tests/test_dqmc.c`：1 sweep で `accept_attempts == L.n × Ltr`、
  複数 sweep で累積、`accepted <= attempts`。
- `tests/test_replica.c`：`replica_bin_add_acceptance` の累積と
  `replica_bin_acceptance` の比（0.4）。
- `tests/test_mpi_replica.c`：pack→unpack 後に acceptance（0.5）と
  raw counter が保存されること。

## 設計上の注意点（バグではない・非ブロッキング）

1. **acceptance は sign 非重みの生の Markov chain 診断量**
   （[src/replica.c:71](../src/replica.c#L71)）。Plan A の意図どおりで、
   半充填 bipartite（sign=1）では自明。将来 sign problem のある領域に転用する場合も、
   診断量としては「生の採択率」が正しいので現状の選択でよい。

2. **jackknife は bin ごとの ratio を resampling 単位にする**。
   各 bin の attempts が等しい前提で mean が pooled 比に一致する。
   `nmeas % nbin != 0` の余りは io 検証段階で弾かれる（既存挙動）ため、
   attempts が bin 間で不揃いになるケースは発生しない。

3. **acceptance の pool 範囲**は `total_bins = nrep × nbin`。全 replica の bin を
   合わせた全体採択率と、その bin/replica 間ばらつきに基づく error を出す。
   診断量としては妥当。

## 推奨事項

- Plan A は完了と判断してよい。B（`hst_type` / `shifted_spin`）に進める。
- D（autocorrelation 比較）で per-sweep の acceptance trace が必要になった際は、
  Plan の `measure_trace` / `acceptance_sweep` 出力を別途追加する
  （現状は beta ごとの jackknife summary のみ）。
