---
date: 2026-06-25
datetime: 2026-06-25 16:40 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: DQMC replica 並列化設計のレビュー（整合性・統計的正しさ・実装ギャップ）
summary: |
  commit aa878bf の replica 並列化設計をレビューした。並列化の粒度(replica 並列・内側 sweep 非並列)と
  統計合成(numerator/denominator 分離→ratio→jackknife)は正しく、MPI 拡張・符号問題・後方互換への配慮も妥当。
  着手前に詰めるべき点は Medium 2 件(profiler グローバルのスレッドローカル化、seed 方式変更と
  serial 等価テストの矛盾)＋ Low 3 件。現コードの seed 式/profiler グローバル、macOS OpenMP の実態を実機確認した。
---

# DQMC Replica 並列化設計 レビュー

## 対象

- 設計書: `docs/superpowers/specs/2026-06-25-dqmc-replica-parallelization-design.md`（commit `aa878bf`）
- 現実装 HEAD: profiler/timer 実装済み（serial）
- 確認した現コード: `src/main.c`（seed 式）、`src/profiler.c`（current profiler のグローバル）、`Makefile`、`src/io.c/h`

## 総評

**並列化の粒度（replica 並列、内側 sweep は非並列）と統計合成の設計は正しく、MPI 拡張・符号問題・後方互換への配慮も行き届いた良い設計。** 致命的な誤りは無い。着手前に詰めるべき点が Medium 2 件・Low 3 件。

実機で確認した事実:

- 現 seed 式は `p.seed + 1000ULL*b`（`src/main.c:95`）。
- 現 profiler は **非スレッドローカルなグローバル** `static Profiler *g_current_profiler`（`src/profiler.c:8`）。
- macOS の `cc -fopenmp` は失敗（Apple clang 非対応）。ただし `libomp` は brew 導入済み（`/opt/homebrew/opt/libomp`）。`_Thread_local` は使用可能。

## Findings

### Medium 1: profiler のグローバルをスレッドローカルにしないと OpenMP で破綻する

設計は「replica ごとに local profiler を持たせる」とするが、**現在の計測は `green.c`/`linalg.c` が `profiler_current()`（単一グローバル `g_current_profiler`）を参照**する。OpenMP で複数 replica が同時実行すると、全スレッドが同じグローバル Profiler の `stat` 配列を加算し **data race** になる。"replica local profiler" を既存 instrumentation に繋ぐには、`g_current_profiler` を**スレッドローカル化**（C11 `_Thread_local`＝実機で動作確認済み、または `#pragma omp threadprivate`）し、各 replica が自スレッドで `profiler_set_current()` する必要がある。設計にこの機構が明記されていない。**最も具体的な実装ギャップ。**

### Medium 2: seed 方式の変更が「serial equivalence」テストと矛盾し、既存データの再現性を壊す

「Seed 設計」は `mix_seed(base_seed, beta_index, replica_id)` を採用するが、現行は `p.seed + 1000*b`。両者は一致しないため、**`nrep=1` でも従来の数値（および committed の `data/L6_U4_mu2_dtau_ed_comparison/` 比較データ）をビット再現しない**。一方テスト計画「serial equivalence: `parallel=serial, nrep=1` が**既存出力と一致**」（設計 line 277）は、`mix_seed(seed,b,0) == seed+1000*b` でない限り**成立しない**。設計内部で矛盾している。

→ decide が必要:
- (a) **ビット一致を保つ**: `nrep=1` では旧 seed 式を使う（または `mix_seed` をその特例にする）。committed 比較データの再現性を維持。
- (b) **アルゴリズム的等価で可**とし、既存比較データを再生成する。

回帰テスト戦略と committed data の双方に影響するため、設計段階で明示すべき。

### Low 3: OMP=serial のビット一致には BLAS を 1 スレッドに固定する必要

「OpenMP correctness: omp が serial と一致」（line 281）は、Accelerate/OpenBLAS が内部マルチスレッドだと **reduction 順非決定**で崩れ得る。決定性テストは BLAS スレッド=1（`VECLIB_MAXIMUM_THREADS=1` / `OPENBLAS_NUM_THREADS=1`）で実行すべきで、これは設計が既に挙げる oversubscription 対策とも一致する。determinism と oversubscription を同じ理由として明記すると良い。

### Low 4: profiler CSV のヘッダ拡張が test_profiler.c を壊す

CSV 列を常時拡張（`wall_sec,thread_total_sec,nrep,parallel` 追加）すると、`tests/test_profiler.c` の**完全一致ヘッダ assert** が失敗する。テスト計画で触れているが、serial 実行でもヘッダが変わる点（既存 assert 更新が必須）を明示しておくと安全。

### Low 5: macOS OpenMP の具体フラグ（設計の懸念を実証）

`cc -fopenmp` は実機で失敗（Apple clang 非対応）を確認。ただし libomp は導入済みなので、`dqmc_omp` の動作フラグは具体的に:

```
cc -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include \
   -L/opt/homebrew/opt/libomp/lib -lomp ...
```

パスは環境変数で上書き可能にしておくと可搬。設計の「serial build/test は壊さない」方針は正しい安全網。

## 補足（指摘ではない）

- **seed 独立性**: avalanche seed で実用上独立な xoshiro ストリームになるが、厳密な非重複保証には xoshiro `jump()` が要る（v1 ではオーバーキル、現状で可）。
- **warmup は replica 毎に重複**（replica 並列固有のコスト）。並列効率のため `nwarm << nmeas` を維持。
- **bin 内自己相関**の過小評価は現行コードからの持ち越し（新規問題ではない）。replica 間 bin は真に独立で推定を改善する。

## 良い点（評価）

- replica 粒度の選択（内側 sweep は本質的に逐次なので正しい）。
- **numerator/denominator 分離**（ratio を後段化＝符号問題対応可、かつ現行の「bin 毎 ratio→jackknife」と統計的に一致）。
- **replica_id 順の決定的合成**（OMP 再現性）。
- 予約 mode＋OMP guard の明示エラー（silent fallback なし）。
- stdout データ行不変（ED 比較/plot 互換）、header 拡張のみ。
- MPI 拡張が綺麗（POD bin、rank 非依存の global-replica-id seed）。
- 妥当な TDD 実装順序。

## 結論

設計の方向性は妥当で、このまま実装計画へ進めてよい。ただし着手前に **Medium 2 件（profiler のスレッドローカル化、seed 方式と serial 等価テストの矛盾）を decide** すること。Low 3 件は実装・テスト時に反映すれば足りる。
