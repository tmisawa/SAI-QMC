---
date: 2026-06-25
datetime: 2026-06-25 15:06 JST
model: Claude Opus 4.8 (1M context)
status: review
topic: DQMC profiling timer 実装プランのレビュー（整合性・潜在バグ・移植性）
summary: |
  `docs/superpowers/plans/2026-06-25-dqmc-profiling-timer.md` を実装前にレビューした。
  プランは 12:03 修正後の現コードと整合し、ブロッカーとなるバグは無い。clock_gettime の
  macOS+C11 移植性、dqmc_init 全呼び出し箇所、la_inverse/udv の return 経路、Makefile の
  wildcard 自動リンクを実機/コードで確認。指摘は Medium 1 件（profiler 無効時のホットパス
  オーバーヘッド）＋ Low 2 件（init 位置・commit trailer）＋情報 2 件。
---

# DQMC Profiling Timer 実装プラン レビュー

## 対象

- 実装プラン: `docs/superpowers/plans/2026-06-25-dqmc-profiling-timer.md`
- 設計書: `docs/superpowers/specs/2026-06-25-dqmc-profiling-timer-design.md`
- レビュー時点の実装 HEAD: `green_wrap` が `expKinv` で B^{-1} を構築し、main の測定前重複 `green_from_scratch` を削除した状態（実装レビュー後の軽微修正コミット以降）
- 確認した現ソース: `src/green.c`, `src/main.c`, `src/dqmc.c`, `src/linalg.c`, `src/io.c/h`, `Makefile`, `tests/test_dqmc.c`, `tests/test_integration.c`

## 総評

**プランは健全で、現在のコードと整合しており、ブロッカーとなるバグは検出されなかった。** TDD の刻み、`profile=1` の opt-in 設計、エラー時 fail-fast、instrumentation 配置、CSV 集計ロジックはいずれも妥当。`profiler_current()` で線形代数の深い関数を計測しつつ上位層は明示 `Profiler*` を持つ折衷も理にかなっている。

指摘は **Medium 1 件（profiler 無効時のホットパスオーバーヘッド）** と Low 2 件、情報 2 件のみ。いずれも着手前に検討すれば足り、着手の妨げにはならない。

特に重要なのは、**このプランが 12:03 の軽微修正後の現コードに対して正しく書かれている**点を確認できたこと（下記）。

## 検証して問題なかった点（エビデンス付き）

| 項目 | 方法 | 結果 |
|---|---|---|
| clock 方式の移植性 | `#define _POSIX_C_SOURCE 199309L` + `<time.h>` の `clock_gettime(CLOCK_MONOTONIC)` を `cc -std=c11 -O2 -Wall -Wextra` で実機コンパイル・実行 | ✅ コンパイル・動作確認 |
| 現コードとの整合（green_wrap） | プラン Task 5 Step 4 の末尾 `free(tmp);free(Binv);free(B);` と現 `green_wrap` を比較 | ✅ 一致（`expKinv` 化後の構造に合致）|
| 現コードとの整合（main 測定ループ） | プラン Task 3 Step 3 の `measure_sample` ラップ位置と現 main を比較 | ✅ 一致（重複 `green_from_scratch` 削除後に合致）|
| dqmc_init 移行の網羅 | `grep -rn "dqmc_init(" src tests` | ✅ 呼び出し4箇所（main.c:80, test_dqmc.c:24/52, test_integration.c:29）すべてプランがカバー |
| la_inverse/udv の return 経路 | 現 `linalg.c` の return 行と PROF_END 挿入指示を突合 | ✅ la_inverse 4経路・udv_inv_one_plus 3経路が一致 |
| ビルド系 | `Makefile` の `SRC=$(wildcard src/*.c)`、`LIBSRC=$(filter-out src/main.c,$(SRC))` を確認 | ✅ profiler.c は dqmc 本体・全テストへ自動リンク。Makefile 変更不要というプランの判断は正しい |
| 物理結果への非影響 | instrumentation は計算に触れず、無効時 no-op。テストは `dqmc_init(...,NULL)` で profiler 無効 | ✅ 既存テストは数値的に不変のはず |
| CSV/accounting | phase 別＋`all` 集計、inclusive、calls=0 行スキップ、`beta_total` 算出を読解 | ✅ ロジック整合。test_profiler の substring 検証も成立 |

## Findings

### Medium: profiler **無効時**でもホットカーネル（特に `la_gemm`）に呼び出しオーバーヘッドが残る

`green.c`/`linalg.c` は `profiler_current()` 経由で計測する。`profiler_now_if_enabled` と `profiler_add_elapsed` は `profiler.c` の**外部関数**であり、TU をまたぐ `green.c`/`linalg.c` からは（LTO 無しの現 `Makefile` では）**インライン化されない**。よって `profile=0` でも各計測点で **関数呼び出し2回 ＋ global load 1回**が毎回走る。`la_gemm` は1スイープで数千回、run 全体で数百万回呼ばれるため、小行列（n=4〜16 が本プロジェクトの主対象）では **通常実行（profile=0）に数 % 程度のオーバーヘッド**が乗り得る。設計書はオーバーヘッドを「`profile=1` のとき」だけ触れており、**無効時のコストは未記載**。

対策（プラン Task 1 に 1 ステップ追加するだけ）: hot な判定関数を `profiler.h` 内で `static inline` 化する。

```c
static inline double profiler_now_if_enabled(const Profiler *p) {
    return (p && p->enabled) ? profiler_now() : 0.0;   /* 無効時は分岐のみ・呼び出し無し */
}
static inline void profiler_add_elapsed(Profiler *p, ProfRegion r, double s) {
    if (!p || !p->enabled) return;
    profiler_add(p, r, profiler_now() - s);
}
```

`profiler_now()` / `profiler_add()` は外部のままでよい（有効時しか呼ばれない）。これで無効時はインライン分岐へ縮退し、「off by default で実質ゼロオーバーヘッド」という profiler の本来の性質を満たせる。大きい n では元々無視できる差だが、本プロジェクトは小サイズが主対象なので効く。

### Low: profiler 初期化位置が早く、エラー経路のクリーンアップ漏れを誘発しやすい

プラン Task 3 Step 1 は `params_read` 直後（lattice 構築前）に `profiler_init` する。すると lattice=file エラー・unknown lattice・**非二部格子**・beta/dtau 非整数 など **5 箇所の `return 1` すべて**に `profiler_set_current(NULL); profiler_close(&prof);` を追記する必要があり、1 つ漏らすと未クローズになる。profiler は beta ループでしか使わないので、**非二部チェックの後・beta ループ直前**に初期化すれば、クリーンアップが必要な早期 return は beta/dtau の 1 箇所だけになり保守的。

### Low: コミット trailer のモデル名

プラン各 Task の commit が `Co-Authored-By: Codex (GPT-5) <noreply@openai.com>` 固定。AGENTS.md は「使用モデルを末尾に付す」方針なので、**実装するモデルの trailer** に置き換えるべき（本リポジトリの既存規約は `Claude Opus 4.8 (1M context)`）。

### 情報（バグではない・設計どおり）

- **inclusive nesting の重なり**: `dqmc_sweep ⊃ green_* ⊃ udv_* ⊃ la_gemm` が入れ子で、`frac_beta` の単純合計は 1 を超える（設計書に明記済み）。加えて、有効時は内側タイマーの clock コストが外側 region 時間を押し上げる系統的バイアスがある点を補足しておくと解析時に安全（相対比較には支障なし）。
- **Makefile のヘッダ依存なし**: `.h` だけ変更しても test が再ビルドされない既存制約。ただし本プランは各 Task で必ず LIBSRC の `.c` も触るため実害は出ない。

## 推奨

着手して問題ない。**Medium（hot 判定関数の `static inline` 化）を Task 1 に 1 ステップ足す**ことのみ推奨する。Low 2 件（init 位置・commit trailer）は任意の改善。それ以外は TDD の刻み・テスト・検証手順とも質が高く、このまま実装に進めてよい。
