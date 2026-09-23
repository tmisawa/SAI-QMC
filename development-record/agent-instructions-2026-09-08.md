---
date: 2026-09-08
datetime: 2026-09-08 11:21 JST
model: OpenAI GPT-6 (Codex)
summary: |
  SAI-QMC の公開方針と、全期間の開発ログを公開対象にする合意を記録した。
  ハッカソンの分析期間、最小限の削除、日本語原記録と英語案内の運用を明示する。
---

# AF_QMC Agent Instructions

有限温度 補助場量子モンテカルロ (DQMC/BSS) コードを C で実装するプロジェクト。
本ファイルは Codex / Claude など coding agent 向けの作業ルール。`CLAUDE.md` は本ファイルへの参照スタブ。

## 参照先（共通ルール）

- 共通運用ルール: `../../common/AGENTS_RULES.md`
- ファイル運用 (README/TODO/LOG): `../../common/INSTRUCTION.md`
- ログ運用: `../../common/common_log.md`
- Markdown スタイル: `../../common/MD_STYLE.md`
- 横断的教訓: `../../common/LESSONS.md`

共通ルールはここに長くコピーしない。以下はこのプロジェクト固有の差分のみ。

## プロジェクト概要

- **目的**: 半充填ハバード模型の有限温度補助場QMC (Blankenbecler–Scalapino–Sugar / 行列式QMC) を C でゼロから実装し、小サイズクラスタで **エネルギーの温度依存性 E(T)** を外部の厳密対角化 (ED)・TPQ と比較検証する。
- **形態**: AIMHack2026（2026-06-24–26）のテーマ。実装ログは6月25–26日の2日間。期間中およびその後の全記録を残す方針（後述「ログ運用」）。
- **スコープ**: 実装するのは DQMC のみ。ED/TPQ は外部ツール（HΦ 等）で計算し数値比較する。半充填 (μ=U/2, 二部格子) なので符号問題なし。
- **実装リファレンス**: 大塚雄一 博士論文 付録A。式 A.1–A.135 は検証済み（`LOG.md` 参照）。

## 公開方針（2026-09-08 ユーザー確認）

- 公開ソフト名は **SAI-QMC**。最新コードを別の **Public repository** に公開し、`AF_QMC` は private の開発記録・raw データ・内部資料の正本として維持する。
- 公開ログはハッカソン前後の **全期間** を対象とする。論文の分析対象は **2026-06-24–26** と日付で区切り、その後の成果と区別する。
- 失敗、不採用案、レビューの誤りと訂正、人間の判断・承認を残す。日本語原記録に英語の案内を添える。
- 公開版から不要な個人環境情報等を最小限削除する。伏字記号や仮名化を既定にせず、実際に理由がある記述だけを削る。private原本と編集差分は保持する。
- 個人環境情報を一部省略した旨を公開案内に簡潔に記す。模型・数値条件・seed・検証結果・訂正の時系列を保ち、LOGを全会話の逐語記録と称さない。
- 英語README、LICENSE、CITATION、実行例、検証データ、適用限界を用意する。大塚博士論文のPDF/OCR/TeX等は除外し、正式に引用する。
- ライセンスはMITを第一候補として第三者コード・GPL等の条件を監査する。初回バージョンは未確定で、`v0.1.0` は提案段階。

## ディレクトリ構成

- `src/` — C ソース（実装は計画書の Task 順に追加）。
- `tests/` — 単体テスト（各 `test_*.c` が独立 `main`。`make test` で一括実行）。
- `input/` — サンプル入力（key=value 形式）。
- `references/` — **正本リファレンス**。大塚博士論文の PDF / OCR済み md・tex / プレビュー PDF。式の出典。
- `LOG.md` — 作業ログ（逆時系列）。
- `docs/superpowers/specs/` — 設計書。`docs/superpowers/plans/` — 実装計画 (14タスク TDD)。`docs/hackathon-kickoff.md` — 発表用サマリ。
- `Makefile` — ビルド（macOS=Accelerate / Linux=OpenBLAS+LAPACK 自動切替）。

## 作業ルール（固有）

- **行列規約**: すべて column-major（Fortran順）、`A[i+j*n]`。LAPACK/BLAS を Fortran シンボル（`dgemm_` 等）で直接呼ぶ。
- **物理規約**: グリーン関数 `g_ij = ⟨c_i c_j†⟩`、密度 `⟨c_i†c_j⟩ = δ_ij − g_ji`。半充填 `μ = U/2`（非対称 `U n↑n↓` 形）。低温は UDV/QR 安定化必須。
- **ED/TPQ 比較時**: `U n↑n↓` 形と `U(n↑−½)(n↓−½)` 形の定数シフトと μ の扱いを必ず揃える（設計書 §5 / `VALIDATION.md`）。
- **開発手法**: 計画書に従い TDD（失敗テスト→実装→緑→commit）のバイトサイズ刻みで進める。
- **文献・式の扱い**: 式番号・数値は推測で埋めず、`references/` の出典で確認。不確かな点は `[要確認]` を残す（共通ルール §9）。

## 文書作成ルール

- 文書（設計書・計画・レビュー・サマリ等、`docs/` 配下や `LOG.md` を含む）を作成・追記するときは、**冒頭に必ず次の3点を書く**:
  1. **日時**（日付＋時刻、JST。書く前に `date` コマンドで確認する）
  2. **使用した AI モデル名**
  3. **簡潔なまとめ**（2〜5行程度）
- frontmatter で書く場合は共通ルール (`../../common/common_log.md` §1、`AGENTS_RULES.md` §6) のテンプレートに従う:

  ```markdown
  ---
  date: YYYY-MM-DD
  datetime: YYYY-MM-DD HH:MM JST
  model: <使用した生成AIモデル名>
  summary: |
    <簡潔なまとめ（2〜5行）>
  ---
  ```

## 実行・検証

- ビルド: `make dqmc`（serial）, `make dqmc_omp`（OpenMP）, `make dqmc_mpi`（MPI）, `make dqmc_hybrid`（MPI+OpenMP）。
- テスト: `make test`, `make test_omp`, `make test_mpi`, `make test_hybrid`（各 `ALL ... TESTS PASSED` を確認）。
- 実行例: `./dqmc input/1d_L4_U4.txt > out.dat`（出力: `T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign`）。
- 検証ラダー: U=0 解析解 → Δτ→0 外挿 → 安定化残差 → ED/TPQ 照合 → 符号=1（詳細は `VALIDATION.md`）。

## HPC 環境メモ

- **kugui / Intel compiler 系**: `openmpi_intel` ではなく
  `intel/2022.2.1` + `intel-mpi/2021.7.1` + `mpiicc`/`mpiexec` を使う。
  `intel-mpi/2021.5` は実機に modulefile がなかった。
- **kugui / Intel MPI では `FI_PROVIDER=psm3` が必要**:
  `export FI_PROVIDER=psm3` を入れる。これを unset した standalone
  `MPI_Gatherv` smoke test は `MPI_Gatherv` 後の root 出力に到達せず、
  2分弱で手動キャンセルした。
- **kugui / 避ける構成**:
  `openmpi_intel/4.1.5` + `intel/2022.2.1` classic `icc` は使わない。
  `MPI_Gatherv` の double 受信バッファ後半が 0 のまま残る受信欠落を
  standalone smoke test で確認済み。`FI_PROVIDER=psm3` を足しても改善しない。
- **kugui / DQMC 確認済み構成**:
  `intel/2022.2.1` + `intel-mpi/2021.7.1` + `mpiicc`
  + `FI_PROVIDER=psm3` では、以前落ちた
  `L=8`, `U=4`, `dtau=0.1`, `beta=0.5`, `nrep=120` の診断 run が正常終了した。
  詳細は `LOG.md` の 2026-06-30 の kugui Intel MPI 記録を参照。

## Git / remote 操作

- **【絶対厳守】ジョブ投入前の条件提示と確認**: `qsub` / `sbatch` / `pjsub` / `hpcflow` 等で計算ジョブを投入する前に、**必ず投入条件を提示してユーザーの明示的な確認（GO）を得てから投入する**。例外なし。提示する条件は最低限: ①対象host・queue・resource（node/ncpus/mpiprocs/threads/memory）、②walltime、③投入スクリプトと入力（stan.in 等）、④実行コマンド、⑤計算条件（模型・パラメータ・BC・収束条件など）、⑥リモートの run ディレクトリ／取得・変更されるパス、⑦**投入するジョブ本数**。
  - **承認は「1回・その1件のみ」有効**。別ジョブ、追加投入、予備・保険ジョブ、再投入、条件変更後の投入は、そのつど改めて条件提示と確認を取り直す。「前に承認されたから」で流さない。
  - ユーザーが「やって」等と言っていても、上記条件を提示して確認を求めるステップを省略しない。
- **【ジョブ設計】基本 1パラメータ = 1ジョブ**: パラメータ走査（U・BC・β・サイズ等）は、1条件を1つの独立ジョブとして投入し、スケジューラに並列化させる。**1ジョブ内で複数条件を `&`＋`wait` の同時バックグラウンド実行で詰め込むのは、遅く・資源競合し・walltime も読みにくいので避ける**。1ジョブ=1条件=フルリソース占有を基本とする。
- **【既存スクリプト再利用時】入力ガードを grep ＋ 1本スモーク先行**: 既存の投入ドライバ/スクリプトを新しいパラメータ（新しい U・格子・境界条件など）で再利用するときは、投入前に **該当変数の扱いを `grep`**（例 `grep AFQMC_U driver.sh`）して、そのスクリプト特有の **whitelist ガード**（例 `case "$U" in 4|8) ;; *) exit 2`）が無いか必ず確認する。加えて、**本番ファンアウト前に必ず 1 条件だけのスモークを 1 本流して**正常起動を確認してから残りを投入する。理由: DRY_RUN は submit 層（qsub 生成）しか実行せず、**ドライバ内のランタイム検証はジョブ実行時にしか走らない**ため、DRY_RUN では検出できない（U=12 で 6 本一括投入し全て即 Exit 2 になった失敗の教訓, 2026-07-04）。
- 共通ルール §7 に従う。`commit` は依頼時に実行してよいが、`push` / PR 作成・更新は実行直前にユーザー確認を取る。
- **リモート作業前承認**: `ssh` / `scp` / `rsync` / `hpcflow` などで外部計算機・HPC・remote host に接続して作業する前に、必ずユーザーの明示承認を取る。対象にはリモートコマンド実行、ファイル転送、ジョブ投入・監視・キャンセル、リモート上のビルド・解析・同期を含む。
- **回収・同期時のバイナリ除外**: `rsync` / `scp` などで HPC の run directory や
  `data/production_runs/` を回収するときは、ユーザーが明示的に要求しない限り、実行バイナリを同期しない。
  最低限 `dqmc`, `dqmc_mpi`, `dqmc_hybrid`, `dqmc_mpi_icc_intelmpi_psm3`,
  `dqmc_*`, `*.o`, `build/` を除外する。回収対象は原則として `summary.tsv`,
  `out.dat`, `run_info.txt`, `time.txt`, `input.in`, `replicas.csv`, `profile*.csv`,
  `hopping_*.txt`, submission TSV など、解析・再現に必要なテキスト/CSV に限定する。
  実行バイナリが必要な場合は、目的・ファイル名・容量を提示して明示確認を取る。
- Genkai などのHPC計算は、承認時に対象host、run id（予定値で可）、投入スクリプト、使用queue/resource、実行するコマンド、想定する計算条件、取得・変更されるリモートパスを提示する。
- コミットメッセージ末尾に Co-Authored-By 行（使用モデル）を付す。

## ログ運用

- **本プロジェクトは全記録を残す**: 作業完了ごとに `LOG.md` に追記する（逆時系列、JST、frontmatter は共通ルール §4 準拠）。日時は書く前に `date` で確認する。
- 日次ログ への転記はユーザー明示時に行う。該当日の `Done` セクションに、プレフィックス `[AF_QMC]` で追記する。
- 日次ログの通常転記は `../../common/common_log.md` に従い 1 行サマリを基本にする。ユーザーが「今日やったことをすべて」など詳細転記を明示した場合は、既存行を消さず、主要作業を複数の 1 行サマリとして網羅的に追記する。

## 固有メモ

- `references/` は source of truth。削除・上書きしない（共通ルール §8）。
- 原論文 (A.103) 2行目に符号タイポあり。実装は (A.107) に従えば影響なし（`LOG.md` 参照）。
- 数値安定性が崩れたら `stab_interval` を小さくする（dqmc の安定化スケジュール）。
