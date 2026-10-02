---
date: 2026-09-30
datetime: 2026-09-30 13:30 JST
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

## 条件付き局所測定

`conditional_measure=1`で、各局所更新時にHS変数の±を重み付き平均したDと、
同じ時点のK・Dから作るEを追加測定する。既定値は`0`。
`replica_bin_file`が必須で、従来の測定列を保持したまま比較用の4列を追加する。
PTでは測定値を温度slotに蓄積する。
式・出力規約・独立replicaでの解析方法と適用範囲は
[条件付き測定の説明](docs/conditional-measurements.md)を参照。
測定方式の改善と平衡化の確認は別に評価する。

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

## 大域HS場更新

低温・大`U`では、局所flipだけの更新でreplicaが全`S^z`の非零sectorに似た
長寿命状態に留まることがあります。opt-inのsite world-line更新は、1つのsiteの
Hubbard–Stratonovich場を全time sliceで一括反転する提案に対し、安定化した
行列式の比でMetropolis判定します。既定の`global_site_select=fixed`では、
各passは全siteを固定順に試行し、Green関数を再構築します。

| key | 値 | 既定 | 意味 |
| --- | --- | --- | --- |
| `global_update` | `none` / `site` | `none` | `site`でsite world-line大域更新を行う |
| `global_interval` | 正の整数 | 100 | 何sweepごとに大域passを1回行うか。warmupから通算する |
| `replica_bin_file` | path | 空 | replica・binごとのsign付き和を書くTSV。大域更新と独立に有効化できる |
| `global_site_diag_file` | path | 空 | site flipの試行数・受理数をpolarization別に集計するhistogramを書く。`global_update=site`が必要 |
| `global_site_select` | `fixed` / `polarized` | `fixed` | flipを試すsiteの選び方。`polarized`は重み`(p_i/p_0)^alpha + 1/n`で抽選する |
| `global_site_power` | 実数`>= 0` | 2 | polarized重みの指数`alpha`。`fixed`でも値を検証する |

```text
global_update=site
global_interval=10
replica_bin_file=bins.tsv
```

`nwarm=7`・`global_interval=3`なら、通算sweep 3・6がwarmup、9・12・…が測定中のpassです。
通算番号は`beta`ごとに0へ戻し、bin境界では戻しません。無効時は既定の乱数列・測定・出力を維持し、
`global_interval`は`global_update=none`でも正の整数として検証します。

有効時はスカラー出力に`global_acceptance global_attempts`の2列が加わります。
受理率は測定期間の全replicaの`Σaccepted / Σattempts`で、試行がなければ`nan 0`です。
passのコストはsite数・time slice数・stabilization block数とともに増え、`profile=1`の
`dqmc_global` regionで計時できます。

`replica_bin_file`は全`beta`を1ファイルに書き、beta index、replica ID、bin IDの順に並べます。
`sweep_begin/end`はwarmupを除く測定sweepの番号（1始まり、両端を含む）です。
格子・条件・規格化と列名はcomment headerに記録します。物理量は`Σ sum_sign_O / Σ sum_sign`で求め、
`sum_sign_Ehub`は全系のenergy、`sum_sign_D`はper siteです。`Szz`は`S^z=(n_up−n_down)/2`、
`Sperp`は`SxSx+SySy`で、規格化はともに`1/N`。`Q`はsquareの`(pi,pi)`またはchainの`pi`、`0`は`q=0`、
未選択のqは`nan`です。同じbinの`3*Szz(Q)−1.5*Sperp(Q)`は`spin_consistency_file`の`−3*DeltaSU2`に対応します。
binの出力先が有効な他の出力先と同じ場合はファイルを書く前に拒否し、数値エラーがあった`beta`の行は出さず、
完了済み`beta`の行は保持します。open・write・closeの失敗は非0終了です。

`global_site_diag_file`は、`beta`とreplicaごとに、siteのworld-line flipの
試行数と受理数を、2つのsiteごとの指標に対して記録します。1つ目は
`p = |m_i|/L`で、siteのHS world lineのpolarizationです（`m_i`はそのsite
の全time sliceにわたる場の和です。atomic limitで物理スピンを固定したsite
では、符号付き`m_i/L`の平均は`±tanh(lambda)`になりますが、これはheaderに
書くscaleであり、有限`L`での`p`の期待値ではありません）。2つ目は
`d = -eps_i m_i sign(M)/L`で、majorityのstaggered patternからのmismatchです
（`eps_i`はsublatticeの符号、`M = sum_j eps_j m_j`、`M = 0`では
`sign(M) = +1`とします）。各指標は50 binで、`p`の幅は`[0,1]`で0.02、
`d`の幅は`[-1,1]`で0.04です。数えるのは測定sweepだけで、`beta`とreplicaごとに
100行のTSVを書きます。このdiagnosticは乱数列や他の出力を変更しません。
キーを省略した場合（`global_update=site`だけを指定した場合も含む）はdiagnosticを
収集しません。以下で説明するweighted site selectionの設計に使われます。

`global_site_select=polarized`は、site flipの固定順序を重み付き抽選に置き換えます。
各passの先頭で全siteのpolarization `p_i = |m_i|/L`を1回計算し、`n`回の各試行で
反転するsiteを`w_i = (p_i/p_0)^alpha + 1/n`に比例する確率で選びます。`p_0 = tanh(lambda)`は
atomic limitのscale（`U = 0`では`p_0 = 1`）、`alpha = global_site_power`です。
siteを反転しても他のsiteの`|m_j|`もそのsiteの`|m_i|`も変わらないため、重みは提案の前後で同じであり、
受理はHastings補正のないMetropolis比です。各試行で乱数を2回（site、次に受理判定）使うので、
同じseedでも`polarized`の乱数列と結果は`fixed`と異なります。`fixed`の実行は、これらのkeyの有無に
かかわらず変わりません。同じpassで同じsiteを複数回選ぶことがあります。`1/n`により全siteが提案され得て、
`alpha = 0`では一様ランダムなsite選択になります。選択方式が`fixed`でないときだけ、stdoutのheaderに
` global_site_select=<value> global_site_power=<alpha>`が付き、diagnosticのheaderにも選択方式が入ります。
重みが使えない場合（非有限、累積和の増分消失、相対重みが保守的な下限`2^-52`未満、または
積の丸めを含めた53-bit乱数の到達点がないsite区間）は数値エラーとしてreplicaを終了します。
相対重みの下限だけでは選択可能性を保証できないので、全区間の到達可能性も乱数消費前に検査します。
staggered mismatch `d`による選択は実装していません。Stage Aの診断がこの指標を不支持としたためで、
`global_site_select=staggered`は入力エラーです。L4・U8・β16/24でのα=2の比較では受理数が増えましたが、
事前に定めた混合改善基準は満たしませんでした。[VALIDATION.md §9](VALIDATION.md#9-stage-b-polarized-selection-versus-fixed-order2026-09-26)を参照してください。

4×2 cluster・`U/t=8`での検証は[VALIDATION.md](VALIDATION.md)と
[docs/validation/global-hs-4x2-2026-09-21.json](docs/validation/global-hs-4x2-2026-09-21.json)に記録しています。
site反転の受理率は低温で急速に下がるため、任意のサイズ・温度で混合を保証するものではありません。

## Δτラダー並列テンパリング

局所flipだけの更新（前節の大域site更新を含む）でも緩和が遅い計算には、
並列テンパリング（PT）が使えます。PTは`beta_list`の各値に対応する`nbeta`個の
slotからなるladderを1本走らせ、全slotで共通の時間slice数を共有するため、
各slotは自分の時間刻み`dtau_k = beta_k / tempering_ltr`を持ちます。
一定間隔で、隣接する2 slotのHubbard–Stratonovich配置を交換する提案を行います。

| key | 値 | 既定 | 意味 |
| --- | --- | --- | --- |
| `tempering` | `none` / `dtau_ladder` | `none` | `dtau_ladder`でΔτラダー並列テンパリングを有効化する |
| `tempering_ltr` | 非負整数 | `0` | 全slotが共有する時間slice数。`tempering=dtau_ladder`では`> 0`が必須（`dtau_k = beta_k / tempering_ltr`）。`tempering=none`では`0`のままにする |
| `tempering_interval` | 正の整数 | `1` | 交換roundを行うsweep間隔（warmup中も含む）。slotがちょうど2個のときは試行するpairがあるroundが2回に1回なので、そのpairの試行は`2*tempering_interval` sweepに1回になる。`tempering=none`でも検証されるが効果はない |
| `tempering_file` | path | 空 | 交換統計を書くTSV（任意）。`tempering=dtau_ladder`でのみ使用可。空のままでもPTは実行できる |
| `field_init` | `random` / `uniform` | `random` | 初期Hubbard–Stratonovich場の族。PTの有無にかかわらず使える |

```text
tempering=dtau_ladder
tempering_ltr=200
tempering_interval=1
beta_list=4,5,6.666666666666667,10
tempering_file=pt.tsv
```

`tempering=dtau_ladder`では`dtau`を指定できません（各slot自身の
`dtau_k = beta_k / tempering_ltr`が代わりに使われます）。`beta_list`は
2個以上の値を昇順（strictly increasing）で指定する必要があり、
`stab_drift_file`・`udv_scale_file`・`udv_centered_file`・
`global_site_diag_file`・`profile=1`は拒否されます。全slotがすでに
sign-freeでparticle-hole対称な模型（半充填・二部格子）である必要があります。
`global_update=site`はPTと併用できます。

隣接するslot `a`、`b`の交換提案では、両slotそれぞれの配置と相手の配置を
比較します。`log W_k(C) = 2 log|det(1+B^k_up(C))| - lambda_k * sum(s)`
（`lambda_k = acosh(exp(dtau_k * U / 2))`）を用い、
`log R = log W_a(C_b) + log W_b(C_a) - log W_a(C_a) - log W_b(C_b)`に対して
`min(1, exp(log R))`の確率で受理します。専用の交換用乱数streamから、
1回の試行につき必ず1つの乱数を引きます（`log R >= 0`でも引きます）。
試行する隣接pairは交換roundごとに`(0,1),(2,3),...`と`(1,2),(3,4),...`を
交互に切り替え、warmup中も交換を行います。slotがちょうど2個の場合、
`(1,2),...`側のroundには試行するpairがないため、唯一のpair `(0,1)`は
2 roundに1回、すなわち`2*tempering_interval` sweepに1回だけ試行されます。

`log R`が非有限になった場合、weight評価が失敗した場合、受理後の配置再構築が
失敗した場合は、棄却としてではなく、そのladderの数値的失敗
（`tempering exchange failed`）として終了します。slot自身のsweepまたは
大域更新で数値破綻が起きた場合もladderは終了しますが、交換失敗としてではなく、
そのslotの`dqmc warmup numerical breakdown`または
`dqmc measurement numerical breakdown`行（`slot=`と`ladder=`を含む）として
報告されます。slotが破綻した後は交換roundを試行しません。

1本のladderが失敗すると、run全体が失敗します。他のladderは最後まで実行され、
その後プロセスは（MPIでは全rankで）非zeroで終了し、stderrに失敗した各ladderを
`tempering ladder r failed`として示します。どのladderの観測量も書かれません。
スカラー出力（標準出力または`output_file`）はheader行だけで、温度ごとの行も
最後の`solver_elapsed_seconds`行もありません。`replica_bin_file`は空のままで、
`szz_file`・`sperp_file`・`spin_consistency_file`・`replica_log`はheaderだけです。
完全に書かれるのは（指定した場合の）`tempering_file`だけで、失敗したladderの
`ladder`行は`failed=1`になります。

slot `k`（ladder `r`）自身のMonte Carlo chainは、通常のreplicaと同じ規則で
`replica_seed(seed, k, r)`をseedとします。専用の交換用乱数streamはその代わりに
`replica_seed(seed, nbeta, r)`を使います（`nbeta`はどのslotの番号としても
使われません）。`tempering_file`の`ladder`行は、この値を`swap_seed`として
記録します。

各ladderのslotと交換は、そのladderの実行中を通じて1つのMPI rank・1つの
OpenMP threadの中で逐次実行され、ladder自体は通常のreplicaと同じ規則で
rank・threadに分配されます。同じseedなら`serial`・`omp`・`mpi`・`hybrid`の
結果は一致します。slot `k`の観測量は、通常のreplicaの出力と同じ場所に
`beta_index=k`、`replica_id`をladder idとして書かれます。統計単位は
slotではなくladderです。PTはTrotter誤差を変えません。各slotは自分の
`dtau_k`を保持し、配置を交換しても統計誤差と時間刻み誤差は混ざりません。

PT時は、標準出力の先頭行と`szz_file`・`sperp_file`・
`spin_consistency_file`のheaderで、数値の代わりに`dtau=ladder`と表示します。
標準出力の先頭行には`tempering=dtau_ladder tempering_ltr=... tempering_interval=...`
（`tempering_file`を指定した場合は`tempering_file=...`も）が追加されます。
`replica_bin_file`には`# tempering=dtau_ladder tempering_ltr=...`という
header行が1行追加されます。通常の単一`dtau=`欄は使われないため、各行自身の
`Ltr`と`beta_effective`列から`dtau_k = beta_effective / Ltr`を求めます。
標準出力の最後の行は`# tempering solver_elapsed_seconds=... nranks=...`で、
プロセス開始から出力を閉じる直前までのsolver自身のwall時間です
（schedulerのjob時間や、ladder間の総和ではありません）。

`tempering_file`は自己記述的なTSVで（`# tempering=...`header、slotごとの
`# slot=k beta=... dtau=... lambda=...`行、`# columns:`の列定義を含む）、
5種類の行を持ちます。

| kind | 意味 |
| --- | --- |
| `pair` | binごとの、隣接pairの交換試行数・受理数（`bin=-1`はwarmup全体の合計） |
| `slot` | binごと・slotごとに、そのslotをbin終了時に占めているwalkerと、そのbinの各sample時点でslotの占有walkerが最後に訪れた端がhot（slot 0）・cold（最終slot）のどちらだったかを積算した回数 |
| `walker` | walkerごとに、測定区間内で完了したhot→cold→hotの往復回数と、実行終了時に占めていたslot |
| `ladder` | ladderごとに1行。交換用乱数streamのseedと、そのladderが失敗したかどうか |
| `cost` | ladderごとの4種類のworker秒（warmup、測定sweep、測定交換、測定observable計算） |

`cost`行は1つのladder自身のworker時間であり、jobのwall時間やnode-hourでは
ありません。複数のladderを同時に走らせている場合、`cost`行を単純に足し合わせて
実際のwall時間を推定してはいけません。往復は、測定区間の内部で完全に
hot slot→最も冷たいslot→hot slotの順に完了した場合だけを数えます。

`field_init=uniform`は、通常の各site乱数draw（実行され、その後捨てられるため
乱数streamは`field_init=random`と同一のまま）の後に、全Hubbard–Stratonovich場を
`+1`に上書きします。PTの有無にかかわらず使え、初期配置だけを変えます。

全`+1`の場は、数値的なスケールが最大の配置でもあります。この場では、
上向きスピンの積`B_{L-1}...B_0`の最大スケールがおよそ
`exp(Ltr*lambda + beta*w)`になります（`lambda = acosh(exp(dtau*U/2))`、
`w`はホッピング行列の最大固有値で、周期境界の正方格子では`4|t|`、
周期境界のchainでは`2|t|`）。
この指数が倍精度の上限`ln(DBL_MAX) ≈ 709.78`をO(1)程度超えると（下の4x4の例では
710.8では開始でき、711.2以上で失敗）、sweepを1回も行わないうちに初期化で失敗します。
stderrには`udv_lmul_work non-finite matrix at stage=qr_raw`に続いて
`dqmc_init failed`が出て、非zeroで終了します。例として、周期境界の4x4正方格子、
`U=8`、`dtau=0.0125`では、`beta=24`（`Ltr=1920`）の指数は708.2で上限より
約1.6 e-fold小さく、正常に開始します。同じ`dtau`の`beta=24.5`・`25`・`26`は
初期化で失敗します。PTでは最も低温のslot（`beta_k`と`dtau_k`が最大）が
この上限を決めます。入力をこの上限と事前に照合する検査はなく、失敗は即座に
明示的に起こります。`field_init=random`はこのスケールより十分小さい配置から
始まります。

PT実装の検証（cross-weight検査、厳密列挙samplingテスト、本番用の選択肢の組を
使った統合ladder driverの厳密列挙回帰、PT前baselineとのbyte同一性、
`serial`/`omp`/`mpi`/`hybrid`の一致、失敗経路と失敗メッセージのテスト、
独立chainと厳密対角化に対する有限サイズ正当性検証）は
[VALIDATION.md](VALIDATION.md)に記録しています。
PTは1つのladderのslotを複数のMPI rankへ分配せず、
`beta_list`の温度配置を交換受理率などから自動最適化する機能もなく、
上記の診断fileやprofilerにも対応していません。本計算を行う前に
[既知の制約](docs/limitations.md)を確認してください。

## 検証と制約

CLIテストと条件付きbin解析にはPython 3.10以降が必要です。
任意実行の独立Fock空間参照generatorはNumPyとSciPyも使います。
`python3`が古いinterpreterを指す場合は、たとえば
`make PYTHON=python3.12 test`のように明示してください。条件付きCLI testが
起動する解析subprocessにも同じinterpreterを使います。

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

本ソフトウェアを利用する際は、SAI-QMCを引用してください。
バージョン0.1の引用情報は[CITATION.cff](CITATION.cff)にあります。
アルゴリズムの参考文献は[REFERENCES.md](REFERENCES.md)に記載し、
Yuichi Otsuka氏の博士論文Appendix Aを含みます。
博士論文や第三者の論文のPDF・OCRテキストは配布しません。

SAI-QMCは[MITライセンス](LICENSE)で提供します。
第三者への帰属表示と外部ライブラリの条件は
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)に記載しています。
今後の改善項目は[TODO.md](TODO.md)を参照してください。
