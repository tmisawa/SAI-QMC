---
date: 2026-09-23
datetime: 2026-09-23 15:43 JST
model: OpenAI GPT-6 (Codex)
summary: |
  SAI-QMC 0.1の日本語利用案内。英語READMEに対応する。
  ビルド、入出力、並列実行、検証範囲、開発記録を説明する。
---

# SAI-QMC

[English](README.md) | [日本語](README_ja.md)

SAI-QMCは、二部格子上の斥力・半充填Hubbard模型を対象とする、有限温度の
行列式量子モンテカルロ法（DQMC/BSS）のC実装です。
エネルギー、密度、二重占有率、等時刻の縦・横スピン構造因子を測定します。
コマンドラインで実行するプログラム名は`dqmc`です。

バージョン**0.1**は、2026年8月22日までの数値計算実装を基に、
その後の入出力と文書の改善を加えたものです。変更は[LOG.md](LOG.md)に記録しています。
AIMHack2026後の開発成果も含みます。2026年6月24–26日の期間中に達成した範囲は
[ハッカソンの概要](docs/hackathon-2026.md)に区別して記載しています。
AIを用いた開発、レビュー、失敗、修正の経緯は
[開発記録](development-record/README.md)を参照してください。

## ビルドと実行

C11対応のCコンパイラ、`make`、BLAS、LAPACKが必要です。
macOSではAccelerate、Linuxでは`-llapack -lblas -lm`をリンクします
（例えばLAPACKとOpenBLASのパッケージを使用します）。OpenMPとMPIは任意です。

リポジトリのルートで実行します。

```sh
make dqmc
./dqmc input/1d_L4_U0_spin_all.txt
```

この小さな非相互作用系の例では、スカラー観測量を自動的に`observables.dat`へ、スピン観測量を
`szz.dat`、`sperp.dat`、`spin_consistency.dat`へ出力します。
相互作用のある系の温度走査には次の入力を使えます。

```sh
./dqmc input/1d_L4_U4.txt
```

出力先は実行時の作業ディレクトリを基準とします。再実行すると既存の出力が
上書きされるため、保存する計算ごとに作業ディレクトリを分けてください。
`input/bench_2d_L*.txt`は大きな系の性能測定用で、短時間の動作確認用ではありません。

## 入力形式

テキスト入力ファイルを1つ、実行プログラムの引数に指定します。
設定は1行に1つずつ`key=value`形式で記述し、`#`以降はコメントです。
キーは行頭から書き、`=`の前後やカンマ区切りリストには空白を入れないでください。
バージョン0.1の入力処理では、書式が合わない行の一部がエラーにならず読み飛ばされます。

例えば、`input/1d_L4_U4.txt`の内容は次のとおりです。

```text
lattice=chain
Lx=4
pbc=1
t=-1.0
U=4.0
dtau=0.1
beta_list=0.5,1.0,2.0,4.0,6.0,8.0
nwarm=300
nmeas=3000
nbin=30
seed=1
```

`beta_list`は逆温度のリストです（`k_B=1`として`T=1/beta`）。
`nwarm`と`nmeas`は各温度・各レプリカのウォームアップと測定のスイープ数です。
`nbin`は誤差評価のために測定を分割するビン数で、2以上かつ`nmeas`を割り切る
必要があります。各`beta/dtau`は整数との差が`1e-9`以内でなければなりません。
化学ポテンシャルは`mu=U/2`に固定されており、`mu`という入力キーはありません。

長方形の正方格子には`lattice=square`と`Lx`、`Ly`を指定します。
`pbc=1`が周期境界、`pbc=0`が開放境界です。周期境界では、二部格子を保つため、
長さが1より大きい各方向のサイズを偶数にする必要があります。
省略した設定の既定値は[入出力リファレンス](docs/usage_ja.md)に記載しています。

## 模型と観測量

シミュレーションで用いるグランドカノニカルHamiltonianは

```text
H = K_hop + U sum_i n_i_up n_i_down - mu N,  mu = U/2.
```

です。`U >= 0`で、格子は二部格子である必要があります。
組み込みの鎖・正方格子では、最近接ホッピングの既定値は`t=-1`です。
`lattice=file`を使うと、対角要素がゼロの実対称ホッピング行列を直接指定できます。
詳細は[入出力と観測量の規約](docs/usage_ja.md)を参照してください。

スカラー観測量は既定で`observables.dat`へ保存し、同じ内容を標準出力にも書き出します。
`output_file=results.dat`で保存先を変更でき、`output_file=none`で標準出力のみになります。
ファイルには実行条件と列名を記した`#`コメント行に続き、指定した温度ごとに
1行の空白区切りデータを出力します。スカラー出力は次の14列です。

```text
T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign  acceptance dAcceptance
```

| 列 | 意味 |
| --- | --- |
| `T` | 実際に計算した逆温度`beta`の逆数 |
| `E_hub`, `dE_hub` | 化学ポテンシャル項を含まないHubbardエネルギーと統計誤差 |
| `E_gc`, `dE_gc` | `-mu*N`を含むグランドカノニカルエネルギーと誤差 |
| `E_ph`, `dE_ph` | 相互作用を粒子・正孔対称な形で表したエネルギーと誤差 |
| `ntot`, `dN` | 全粒子数と誤差 |
| `doublon`, `dD` | サイト当たりの二重占有率と誤差 |
| `sign` | モンテカルロ符号の平均 |
| `acceptance`, `dAcceptance` | 局所更新の採択率と誤差 |

エネルギーと`ntot`は系全体の値、`doublon`はサイト当たりの値です。
エネルギーの関係は`E_hub=<K_hop+U sum n_up n_down>`、
`E_gc=E_hub-mu*ntot`、`E_ph=E_hub-(U/2)*ntot+(U/4)*n_site`です。
`acceptance`は測定スイープ中の局所更新の採択率、`dAcceptance`はその
ビン・ジャックナイフ誤差です。この2列は元の12列の後に追加したもので、
古い参照データには含まれない場合があります。

任意のスピン測定には`szz_q`と`sperp_q`を使い、`none`、`af`、`all`、
またはカンマ区切りの運動量リストを指定します。`Sperp=Sxx+Syy`です。
両者で同じ順序の運動量を指定すると、`spin_consistency_file`に
`DeltaSU2=Sperp/2-Szz`と、対応するビンを用いたジャックナイフ誤差を出力できます。

3種類のスピン出力をすべて有効にするには、次を追加します。

```text
szz_q=all
szz_file=szz.dat
sperp_q=all
sperp_file=sperp.dat
spin_consistency_file=spin_consistency.dat
```

各スピンデータファイルには`#`から始まる条件と列名のコメントに続き、
逆温度と選択した運動量の組ごとに1行の空白区切りデータが入ります。
最後の2列は観測量と誤差です。
全列の定義と運動量の規約は[入出力リファレンス](docs/usage_ja.md)に記載しています。

| 出力先 | 形式と有効にする設定 |
| --- | --- |
| `observables.dat`と標準出力 | 上記のスカラー14列。保存先は`output_file`で変更 |
| `hopping_used.txt` | サイト数に続いてホッピング行列。作業ディレクトリに出力 |
| `szz.dat`, `sperp.dat`, `spin_consistency.dat` | 上記の設定で有効にするスピン観測量のデータ |
| `replicas.dat` | レプリカの乱数種・計算条件・実行状態。`nrep>1`で既定で有効。`replica_log=none`で無効、`replica_log=path.dat`で出力先を指定 |
| `profile.dat` | `profile=1`で有効にする時間計測のデータ。出力先は`profile_file`で変更 |

新しく出力する表は、空白区切り・`#`から始まるコメントと列名に統一しています。
入力設定とホッピング行列は`.txt`形式です。過去の参照データは当時のファイル名と
形式で収録しています。

エラーメッセージは標準エラー出力へ書き出します。
`./dqmc input.txt 2> run.err`と実行すると、診断メッセージを別に保存できます。
スカラー観測量は自動保存されます。従来のリダイレクトによる保存には
`output_file=none`を使用できます。

## 並列実行

レプリカは独立なモンテカルロ系列で、乱数種は決定的な規則で割り当てます。
入力の`parallel`と`nrep`で実行方式とレプリカ数を選びます。

```sh
make dqmc_omp
OMP_NUM_THREADS=2 ./dqmc_omp input/1d_L4_U0_szz_all_omp.txt

make dqmc_mpi
mpirun -np 2 ./dqmc_mpi input/1d_L4_U0_szz_all_mpi.txt

make dqmc_hybrid
OMP_NUM_THREADS=2 mpirun -np 2 ./dqmc_hybrid input/1d_L4_U0_szz_all_hybrid.txt
```

Apple SiliconとHomebrewの組合せでは、OpenMPの既定の参照先は
`LIBOMP_PREFIX=/opt/homebrew/opt/libomp`です。別の場所にインストールした場合は、
このMake変数を変更してください。MPIのCコンパイララッパーは、使用する
Cコンパイラと互換性のあるものを選んでください。
ローカル検証で使用したOpen MPIとApple Clangの環境では、
`OMPI_CC=clang make dqmc_mpi dqmc_hybrid`でClangを明示的に選択します。

## 検証と制約

```sh
make test
make test_omp
OMPI_CC=clang make test_mpi
OMPI_CC=clang make test_hybrid
make test_slow
python3 scripts/verify_reference_data.py
```

`OMPI_CC`の指定はOpen MPI固有です。別のMPI実装を使う場合は省略するか、
環境に応じて`MPICC`を設定してください。

テストでは、Green関数の更新と安定化、独立な非相互作用の参照値、スピンの総和則、
粒子・正孔対称性、出力互換性、レプリカの集約、並列実行の整合性を確認します。
時間のかかるテストは別のターゲットに分けています。

本計算を行う前に[既知の制約](docs/limitations.md)を確認してください。
特に次の点に注意が必要です。

- 有限の時間刻みにはTrotter誤差があり、統計誤差には含まれません。
  比較するグランドカノニカル集団の条件をそろえ、`dtau^2`で外挿してください。
- 低温での数値安定性には限界があります。`green_rebuild=centered`は明示的に
  選択する方式で、確認された安定範囲は条件に依存します。既定値は`combine`です。
- スピンの誤差棒は、独立な計算間のばらつきを過小評価することがあります。
  長い測定、独立した乱数種、収束確認が必要です。収録した低温スピンデータの一部は
  収束確認を通過しておらず、その旨を記録しています。

[参照データ](data/README.md)には有限温度の厳密対角化（ED）との比較と
Trotter外挿を収録しています。チェックサムと出典は
[PROVENANCE.md](PROVENANCE.md)に記載しています。
過去の検証記録は、入力可能なすべてのパラメータの組合せを保証するものではありません。

## 引用とライセンス

バージョン0.1の引用情報は[CITATION.cff](CITATION.cff)にあります。
実際に使用したリリースまたはコミットを引用してください。
アルゴリズムの参考文献は[REFERENCES.md](REFERENCES.md)に記載し、
Yuichi Otsuka氏の博士論文Appendix Aを含みます。
博士論文や第三者の論文のPDF・OCRテキストは配布しません。

SAI-QMCは[MITライセンス](LICENSE)で提供します。
第三者への帰属表示と外部ライブラリの条件は
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)に記載しています。
今後の改善項目は[TODO.md](TODO.md)を参照してください。
