---
date: 2026-08-21
datetime: 2026-08-21 23:16 JST
model: Claude Opus 5 (review); Codex (GPT-5) (follow-up correction)
status: review
follow_up_status: resolved
topic: Szz(q) structure factor implementation review
target: |
  branch feat/szz-structure-factor (uncommitted worktree, base 3177dfe)
  src/structure_factor.{c,h}, src/{main,io,lattice,replica,replica_run,replica_mpi,profiler}.c
  tests/test_structure_factor.c, tests/test_szz_{output,parallel}.sh
  docs/2026-08-21-szz-structure-factor-{usage,validation}.md
reviewer: Claude Opus 5
revised_by: Codex (GPT-5)
summary: |
  S^{zz}(q) 実装のレビュー。静的読解に加えてビルド・全 test matrix・baseline 比較・
  独立な自由フェルミオン照合・ASan/UBSan・leak 検査を実際に実行した。correctness bug は
  検出されず、レビュー High 5 件は実測で修正を確認した。指摘は Medium 4 件、Low 8 件で、
  最も重要なのは shell test が make target に組み込まれていないこと。follow-up で
  Medium 4件と rectangular unit testを反映し、toolchain・binning・working-tree
  件数の記述も訂正した。
---

# Szz Structure Factor 実装レビュー

- レビュー日時: 2026-08-21 22:33 JST
- 追補日時: 2026-08-21 23:16 JST
- 使用AI: Claude Opus 5 (1M context)、追補 Codex (GPT-5)
- 要約: 実装は正しい。物理は独立式で機械精度一致、後方互換は baseline と
  byte 一致、全 test matrix が緑。correctness bug は無い。運用上の穴として
  shell test が自動実行されないこと、sum-rule 失敗時の診断情報が無いことを指摘する。

## 0. Follow-up Result

2026-08-21 23:16 JST に Medium 4件と L-2を反映し、全 test matrix を再実行した。

- M-1: `test_szz`/`test_szz_parallel` を Makefile に追加し、`make test` と
  `make test_hybrid` からそれぞれ自動実行した。実行ログでも両方の `OK` を確認した。
- M-2: ratio failure と sum-rule failure を分離した。sum-rule 診断は
  `lhs`/`rhs`/`diff`/`tol`/`sum_sign` を出し、計算 API を unit test した。
- M-3: validation 文書を「短い run では plateau 判定不能」へ修正した。
- M-4: rank の phase table と MPI root の bin vector のメモリ式・代表値を usage 文書に
  追加した。
- L-2: 2x3 rectangular square の全6 qを direct oracle と比較する unit testを追加した。
- M-2 の refactor で L-3 の複合 early return も解消した。L-5 の API 締め付けは
  follow-up の `LOG.md` に記録した。
- `make test`, `make test_omp`, `OMPI_CC=cc make test_mpi`,
  `OMPI_CC=cc make test_hybrid`, `make test_slow` はすべて成功した。

以下の §1--§5 はレビュー時点の記録として残し、解消状況は本節を正本とする。

## 1. レビュー方法

`docs/reviews/2026-08-21-szz-structure-factor-design-plan-review.md` の続きとして、
設計・計画レビューで挙げた High 5 件が実際に閉じているかを中心に検証した。

**静的読解に加えて、以下をこのレビューで実行した**（設計レビューは静的のみだった）。

```sh
make dqmc dqmc_omp dqmc_mpi dqmc_hybrid   # -Wall -Wextra warning ゼロ
make test / test_omp / test_mpi / test_hybrid / test_slow
sh tests/test_szz_output.sh / tests/test_szz_parallel.sh
git worktree add --detach <tmp> 3177dfe   # baseline binary を別途 build
```

加えて、実装とは独立に自分で書いた自由フェルミオン公式・sum rule・
error case・ASan/UBSan・leak 検査を回した（§2.2〜§2.5）。

## 2. 検証結果

### 2.1 ビルドと test matrix

| command | 結果 |
| --- | --- |
| `make dqmc dqmc_omp dqmc_mpi dqmc_hybrid` | 成功、`-Wall -Wextra` warning ゼロ |
| `make test` | ALL TESTS PASSED |
| `make test_omp` | ALL OMP TESTS PASSED |
| `make test_mpi` | ALL MPI TESTS PASSED |
| `make test_hybrid` | ALL HYBRID TESTS PASSED |
| `make test_slow` | ALL SLOW TESTS PASSED |
| `sh tests/test_szz_output.sh` | OK |
| `sh tests/test_szz_parallel.sh` | OK |

レビュー時の既存 build artifacts を使った test 実行は成功した。一方、現在の
`mpicc --showme:command` は PATH に無い `gcc-15` を示し、強制 rebuild は失敗する。
このローカル環境での clean rebuild には validation 文書 §1 のとおり
`OMPI_CC=cc` が必要である。

### 2.2 後方互換（baseline との byte 比較）

base commit `3177dfe` を `git worktree` で別ディレクトリに展開して binary を build し、
同一 input で新旧を比較した。

- `input/1d_L4_U0.txt`: stdout **完全一致**。
- `input/1d_L4_U4.txt`: stdout **完全一致**。
- `szz_q=none` で `szz.tsv` は作られない。

有効化した場合（`chain L=4, U=4, seed=1, beta_list=0.5..8`）:

- `szz_q=af` / `szz_q=all` のいずれでも、**scalar data 行は disabled 実行と完全一致**。
- 差分は metadata comment の
  ` szz_file=szz_af.tsv szz_nq=1` 追記のみ。

`src/replica_run.c` で `replica_bin_add_acceptance()` の呼び出しが測定の前から
sample commit の後へ移動しているが、成功 run では加算値が同一であり、
上記の byte 一致（acceptance 列を含む）で実証されている。

### 2.3 物理の独立検証

実装が使っている Wick 式とは**独立な運動量空間の閉じた式**

$$
S^{zz}_{U=0}(\mathbf q)=\frac{1}{2N}\sum_{\mathbf k}
f_{\mathbf k}\left(1-f_{\mathbf k+\mathbf q}\right),\qquad
f_{\mathbf k}=\frac{1}{1+e^{\beta(\varepsilon_{\mathbf k}-\mu)}}
$$

を自分で実装して照合した。

**chain L=4, PBC, U=0, 全 4 q, β=0.5/1/2/4/8**:

| β | S(0) DQMC | S(0) 独立式 | S(π) DQMC | S(π) 独立式 |
| ---: | --- | --- | --- | --- |
| 0.5 | 0.11165298331037048 | 0.11165298331037046 | 0.13834701668962965 | 0.13834701668962954 |
| 8 | 0.062500028133787314 | 0.062500028133787355 | 0.18749997186621267 | 0.18749997186621264 |

全 20 点で一致（差 ≲ 1e-16）。

**square 4x6（Lx≠Ly）, PBC, U=0, 全 24 q, β=1 と 2**:

```text
rows 48   max|DQMC - free-fermion| = 8.327e-17
          max|S(q) - S(-q)|        = 5.551e-17
```

これは重要な確認である。unit test の `measure_szz_sample` 検証は
2x2 square と 4-site chain（いずれも `n=4`）でしか行われておらず、
どちらも `disp = dx + dy*Lx` の x/y 取り違えを検出できない（§4.6）。
4x6 の一致により、**index mapping が実機で正しいことを確認した**。

**sum rule の外部照合**（stdout の `ntot`/`doublon` と TSV の Σ_q を突き合わせ）:

| 条件 | Σ_q S^zz(q) | (1/4)(N_e − 2N D) |
| --- | ---: | ---: |
| chain L=4, U=0, β=8 | 0.5 | 0.5 |
| square 4x6, U=0, β=1,2 | 3 | 3 |
| square 4x4, U=4, β=1 | 2.9264669490940363 | 2.9264669490940 |
| square 4x4, U=4, β=4 | 3.0228738360952200 | 3.0228738360952 |
| square 4x4, U=0, **OBC**, β=2 | 2 | 2 |

相互作用ありでも $S^{zz}(\pi,\pi)$ は β=1 で 0.269(4)、β=4 で 0.648(20) と
温度低下とともに増大し、物理的に妥当である。

### 2.4 設計レビュー High 5 件の閉じ確認

| # | 内容 | 実測結果 |
| --- | --- | --- |
| H-1 | selector の無言切り捨て | **閉** `szz_q` 255 文字は可、259 文字は `ERROR: invalid szz_q value on line 9`、653 文字行は `ERROR: input line 9 exceeds 511 bytes`、`0:0, 2:0` と空値も reject。`io.c` の `parse_szz_string()` が raw line を再解析するため `%255s` の切り捨てを検出できる |
| H-2 | 未列挙の call site | **閉** `tests/test_dqmc_alternating.c`・`tests/test_sign_regression_slow.c` とも更新済み。`make test_slow` が build・通過 |
| H-3 | goto 越えのポインタ宣言 | **閉** `local_szz`/`all_szz`/`szz_bins`/`szz_values`/`szz_counts`/`szz_displs` はすべて MPI block 冒頭の既存宣言群と同じ位置で `NULL` 初期化 |
| H-4 | 負の modulo | **閉** `structure_factor.c` は `if (dx < 0) dx += Lx;` の条件加算。unit test が `corr_disp[Lx-1]` を直接検証。4x6 の実測一致でも裏付け |
| H-5 | Gatherv 整合性検査 | **閉** root が `all_szz` を全要素 `NAN` で初期化し、gather 後に finite を検査。`all` ではさらに bin ごとに sum rule を検査。物理的に正当な全ゼロは reject しない |

### 2.5 MPI とメモリ安全性

自分で実行した確認:

- **root の `szz_file` open 失敗**（存在しないディレクトリ配下）を `mpirun -np 2` で
  発生させ、hang せず全 rank が exit 1。`setup_failed` → `mpi_any_failed()` 経路が
  正しく機能している。
- **`nranks > nrep`**（4 ranks / 2 replicas, `szz_q=all`）が正常完走し、TSV 4 行。
- `tests/test_szz_parallel.sh` が omp(2 threads) / mpi(2) / mpi(4, nrep=3) / hybrid(2x2) の
  data 行と scalar 行の一致を検証している。
- **ASan + UBSan** build（`-fsanitize=address,undefined`）で square 4x6, U=4, nrep=2,
  `szz_q=all` を実行 → runtime error ゼロ。selected-list と reject 経路も clean。
- **`leaks --atExit`** → `0 leaks for 0 total leaked bytes`。
- `src/replica_run.c` の `szz_sample` 確保後の `return 1` は 7 箇所、
  対応する `free(szz_sample)` も 7 箇所で漏れなし。

### 2.6 実装として良い点

- `replica_result_add_sample()` が `ReplicaBin next = result->bins[bin];` に一旦加算し、
  Szz 検証を通してから commit する transactional 実装。設計 §7.3 の
  「片方だけ bin に入る partial sample を作らない」要求を素直に満たしている。
- `szz_bin_ratio()` を serial（`replica_result_szz_bin_values()` 経由）と
  MPI root（raw buffer 直）の**両方が共有**しており、設計レビュー M-2 で懸念した
  二重実装を回避している。
- `qx_folded_over_pi` 列、`beta_requested`/`beta`/`T` の 3 本立て、全 double `%.17g` は
  設計レビュー M-4/M-5/M-6 をそのまま実装している。
- profiler は `calls == 0` の行を出さないため、`PROF_MEASURE_SZZ` を enum 中央に
  挿入しても disabled 実行の `profile.csv` は変わらない。`tests/test_profiler.c` が
  name array の整合を検証している。

## 3. Correctness bug

**検出なし。** 静的読解、全 test matrix、独立式照合、sanitizer、leak 検査の
いずれでも実装の誤りは見つからなかった。

## 4. 指摘事項

### 4.1 Medium

#### M-1: shell test が make target に組み込まれていない（follow-up で解消）

`Makefile` は

```make
ALL_TESTS = $(wildcard tests/test_*.c)
```

で `.c` だけを集めるため、`tests/test_szz_output.sh` と
`tests/test_szz_parallel.sh` は **`make test` / `test_mpi` / `test_hybrid` の
いずれからも実行されない**。`grep -n szz Makefile` は無ヒットである。

この 2 本は計画 Task 6「Output Tests」と Task 7「Cross-Mode Reproducibility」の
gate そのもので、C の unit test では代替できないもの（TSV の 12 列 schema、
`q_index` 順序、`beta*T==1`、folded q、serial/omp/mpi/hybrid の行一致、
`nranks>nrep`）を検証している。組み込まれていない以上、

- TSV の列を 1 本増減させても
- MPI の q ordering が replica-major から bin-major に化けても
- `szz_file=none` の reject が消えても

`make test` はすべて緑のままである。実装者が手で 2 コマンド打つ運用に
依存しており、regression 検出器として機能しない。

修正案（`.PHONY` にも追加する）:

```make
test_szz: dqmc
	@sh tests/test_szz_output.sh

test_szz_parallel: dqmc dqmc_omp dqmc_mpi dqmc_hybrid
	@sh tests/test_szz_parallel.sh
```

そのうえで `test:` の末尾に `test_szz` を、`test_hybrid:` の末尾に
`test_szz_parallel` を連結するか、少なくとも `AGENTS.md` の
「テスト」節に 2 コマンドを追記して、実行忘れが起きない形にすること。

`.gitignore` に `!tests/test_*.sh` が入っており追跡対象になっているのは適切なので、
残りは make 側だけである。

#### M-2: sum rule / ratio 失敗時のエラーに数値が無い（follow-up で解消）

serial 経路 (`src/main.c`):

```text
ERROR: invalid Szz bin at beta=%.17g replica=%d bin=%d
```

MPI 経路:

```text
ERROR: invalid gathered Szz bin at beta=%.17g flat_bin=%d
```

いずれも

- `szz_bin_ratio()` の失敗（`sum_sign==0` / non-finite）
- `szz_sum_rule_valid()` の失敗（Σ_q と局所モーメントの不一致）

の**2 つを 1 つのメッセージに畳んでおり**、`lhs`、`rhs`、`lhs-rhs`、`tol`、
`sum_sign` のいずれも出力しない。すぐ隣の scalar 経路が
`count`, `sum_sign`, `sum_sign_Ehub`, … を全部出しているのと非対称で、
設計 §12 の「error message には可能な範囲で … を含める」にも届いていない。

この check は **beta 全体のサンプリングが終わった finalization 時点の hard failure**
である。長時間 production run が最後の 1 秒で落ちたとき、利用者は

- MPI の受信破損（H-5 が想定した本命）
- 大きな格子で `tol = 1e-12 + 1e-10*max(1,|rhs|)` が厳しすぎた

のどちらなのかを判別する手掛かりを持たない。`szz_sum_rule_valid()` を
`(int *reason, double *lhs, double *rhs, double *tol)` を返す形にし、
どちらの check が落ちたかと 4 つの数値を必ず出力すること。

参考までに丸め誤差を見積もると、$\sum_q\cos$ の残差は $O(N\epsilon)$ なので
lhs の誤差は概ね $N\cdot\max|C^{zz}|\cdot N\epsilon/N \sim 10^{-11}$ 程度で、
L=64（N=4096）でも `tol` に収まる。現行 tolerance は妥当だが、
落ちたときに**それを確認できる情報が出ない**のが問題である。

#### M-3: validation 文書 §4 の bin-width 表は plateau の根拠になっていない（follow-up で解消）

記載値:

| nbin | bin size | dSzz(AF) |
| ---: | ---: | --- |
| 10 | 80 | 0.012238744067016234 |
| 20 | 40 | 0.015035056742551515 |
| 40 | 20 | 0.014639491824923693 |

binning 解析では **bin size を増やしたときの誤差が plateau に近づくか**を見る。
有限サンプルの推定値は厳密に単調である必要はないが、この表では bin size が最大
（nbin=10）のときに誤差が最小で、plateau を示す系統的傾向は見えない。3 点とも
誤差推定自身のばらつき
（$1/\sqrt{2(N-1)}$ = 24% / 16% / 11%）の範囲内で、
「20/40 で 2.7% 内だから plateau の兆候」という結論は支えられていない。

対処は次のいずれか。

- `nmeas` を 1 桁以上増やし、**同一測定列を再 binning** して
  bin size = 5,10,20,40,80,160 の系列を作る（bin 数を保つため nmeas を増やす）。
- あるいは「短い run では誤差推定自体が 10〜25% ばらつくため、この表からは
  plateau を判定できない。production 条件で改めて実施する」と結論を弱める。

利用文書 §5 の利用者向け助言（`dSzz` の bin-width plateau を確認せよ）は
正しいので、直すのは validation 文書の**根拠の書き方だけ**である。

#### M-4: `szz_q=all` のメモリ見積もりが利用文書に無い（follow-up で解消）

`phase_cos` は `8 * N * nq` bytes で、`all` では `8N^2`。
これは **plan が rank ごとに構築されるため MPI rank ごとに 1 部**必要になる。

| 格子 | N | phase_cos / rank |
| --- | ---: | ---: |
| 16x16 | 256 | 0.5 MB |
| 32x32 | 1024 | 8 MB |
| 48x48 | 2304 | 42 MB |
| 64x64 | 4096 | 134 MB |

さらに root は `all_szz` と `szz_bins` を各 `8 * total_bins * nq` bytes 保持する。
`AGENTS.md` にある production 規模（`nrep=120`）で `nbin=100` とすると
`total_bins=12000` なので

- L=16: 24.6 MB × 2 ≈ 50 MB
- L=32: 98 MB × 2 ≈ 196 MB

となる。48x48 を 1 node 32 rank で回すと phase_cos だけで 1.3 GB になる。
設計 §15 に「N×Nq memory が問題になれば分離可能表を導入」とあるが、
**利用文書には一切記載が無い**。usage 文書 §3 か §5 に上記の式
（rank あたり `8*N*nq`、root 追加 `16*total_bins*nq`）を 3 行で入れること。

### 4.2 Low

- **L-1: `measure_szz_sample` の内側ループが冗長。**
  `g_up[j+j*n]` / `g_dn[j+j*n]` を $N^2$ 回読み（stride `n+1` で cache 非効率）、
  `spin_j` を毎回再計算し、`isfinite` を 1 対あたり 7 回呼んでいる。
  `spin[]`（長さ `n`）を前計算し、finite 検査を要素ごと 1 回に減らせば
  数倍速くなる。実測は N=64 で約 4.4 ns/pair。sweep の $O(N^3L_\tau)$ に対して
  完全に無視できるので**性能上の問題ではなく、純粋な整理**である。

- **L-2: unit test に Lx≠Ly（両方 >1）の測定ケースが無い。**
  `measure_szz_sample` の検証は 2x2 square と 4-chain のみで、どちらも `n=4`。
  この 2 つでは `disp = dx + dy*Lx` の x/y 取り違えを検出できない。
  §2.3 の 4x6 実測で実害が無いことは確認済みだが、
  2x3 か 4x2 を `direct_szz` oracle と突き合わせる case を 1 つ足せば
  unit suite 単体で閉じる。

- **L-3: `szz_sum_rule_valid()` の early return が読みにくい。**
  `return plan != NULL && !plan->is_all;` が「plan が NULL なら invalid」
  「is_all でなければ検査不要で valid」「is_all だが入力が壊れていれば invalid」の
  3 通りを 1 式に畳んでいる。guard を分けたほうがよい。

- **L-4: `dqmc_run_replica()` の error 経路が `replica_result_free(result)` を呼び、
  `result->status` を 0 に戻す。**
  `main` は `replica_failed[r]` で失敗を検出するので実害は無いが、
  replica log の `results[r].status != 0` という二重チェックはこの経路で無効になる。

- **L-5: `replica_result_alloc()` が `nbin <= 0` を reject するようになった。**
  `params_read()` が `nbin >= 2` を保証するので安全な締め付けだが、
  public API の挙動変更なので `LOG.md` に 1 行残す価値がある。

- **L-6: `parse_index()` は `strtol` 由来で先頭 `+` を受理する。**
  `+1:0` が `mx=1` として通る。無害だが、厳格さを謳うなら弾いてよい。

- **L-7: validation 文書 §7 の性能表は情報量が無い。**
  `none` 0.405 s に対し `all` 0.325 s と、無効のほうが遅い。
  文書自身が noise と断っているので誤りではないが、
  wall-time 列は落として `measure_szz` の calls / total だけ残すか、
  意味のある長さの run に差し替えるほうがよい。

- **L-8: 変更がすべて未コミット。**
  `feat/szz-structure-factor` は `3177dfe` を指したままで、
  レビュー時点で tracked 26ファイルが変更、untracked 16ファイルが存在した。
  このうち source/test/input の新規ファイルは9個で、残りは文書と既存の基礎ノートである。
  計画は 9 個の commit boundary を定義していたので、
  merge 前にその粒度で commit しておくと `git bisect` が使える。
  なお `LOG.md` の既存エントリと未コミットの DQMC 基礎ノート
  （`docs/2026-07-18-dqmc-basics-note.{tex,pdf}`）は保持されている。

## 5. 結論

- **実装は正しい。** 物理は独立式と機械精度で一致し、後方互換は baseline binary との
  byte 比較で確認でき、全 test matrix・sanitizer・leak 検査が clean である。
  設計レビューの High 5 件はすべて実測で閉じている。
- follow-up で M-1〜M-4、L-2、L-3を解消し、L-5もログへ明記した。
- 残る Low は L-1、L-4、L-6、L-7、L-8で、correctness/merge gate ではない。
