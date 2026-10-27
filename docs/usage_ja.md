---
date: 2026-10-27
datetime: 2026-10-27 17:05 JST
model: OpenAI GPT-6 (Codex); OpenAI GPT-5.6 Luna (Codex; 2026-10-02 directional boundaries); OpenAI GPT-6 (Codex; 2026-10-03 metadata update); Claude Opus 5.5 (Claude Code; 2026-10-27 sign reweighting); OpenAI GPT-6 (Codex; 2026-10-27 explicit sign_file and q validation)
summary: |
  2026-10-27: explicit sign_file=none selects exact file PH testing and extended headers; unmeasured q requires NaN.
  2026-10-27: mu・tprime・sign_fileのkey、符号付き合算比の推定量、ratio_status、出力schemaを追加。
  組み込み格子の方向別境界keyと、解決済み境界の出力表記を含む。
  SAI-QMC 0.1の入出力リファレンス。英語版usage.mdに対応する日本語訳。
  入力の既定値、出力列、観測量の規約、追加の診断出力、旧形式との違いを説明する。
---

# 入出力リファレンス

[English](usage.md) | [日本語](usage_ja.md)

`./dqmc input.txt 2> run.err`で実行します。入力はテキストファイルです。
スカラー観測量は`observables.dat`へ自動保存し、同じ内容を標準出力にも書き出します。
診断メッセージは標準エラー出力へ書き出します。
入出力の相対パスは、入力ファイルの所在ではなく、実行時の作業ディレクトリを基準とします。
同じ保存先で再実行すると出力を上書きするため、保存する計算ごとに作業ディレクトリを
分けてください。

## 入力書式と既定値

1行に1つずつ`key=value`形式で設定します。空行と`#`から始まるコメントを使用できます。
キーは行頭から書き、`=`の前後や値の中に空白を入れないでください。
カンマ区切りのリストやパスにも空白を入れず、値を引用符で囲まないでください。
バージョン0.1では、書式が合わない行の一部がエラーにならず読み飛ばされ、
既定値のまま計算される場合があります。書式を解釈できた行の未定義キーや不正な値、
不正な設定の組合せはエラーになります。
実行例は[input/](../input/)、入力処理と検査の実装は[`src/io.c`](../src/io.c)を参照してください。

| キー | 既定値 | 意味・指定できる値 |
| --- | --- | --- |
| `lattice` | `chain` | `chain`、`square`、`file` |
| `Lx`, `Ly` | `4`, `1` | 組み込み格子の各方向の長さ。正の整数 |
| `pbc` | `1` | 両方向の境界。`1`：周期境界、`0`：開放境界 |
| `bc` | `pbc`と同じ | 別名指定：`periodic` / `pbc` / `1`、または`open` / `obc` / `0` |
| `bc_x`, `bc_y` | 未設定 | 方向ごとの境界：`periodic`、`antiperiodic`、`open`。省略した方向は周期境界。`pbc`/`bc`とは併用できず、`bc_y`は`lattice=square`のみ |
| `latfile` | 未設定 | ホッピング行列のファイル。`lattice=file`では必須 |
| `t`, `U` | `-1.0`, `4.0` | 組み込み格子のホッピング振幅と斥力相互作用（`U>=0`） |
| `tprime` | `0` | `lattice=square`専用。両対角結合の行列要素。0でない値には`Lx>=2`かつ`Ly>=2`が必要 |
| `mu` | `U/2` | 化学ポテンシャル（有限の実数）。省略または厳密に`U/2`のときだけ半充填の候補 |
| `dtau` | `0.1` | 正の虚時間刻み |
| `beta_list` | `2.0` | 正の逆温度をカンマ区切りで指定。最大64個 |
| `nwarm`, `nmeas`, `nbin` | `200`, `2000`, `20` | 各レプリカ・各温度のウォームアップスイープ数、測定スイープ数、測定ビン数 |
| `stab` | `8` | 安定化を行う間隔。正の整数。別名は`stabilize_interval` |
| `green_rebuild` | `combine` | `combine`、`two_sided`、`centered`。[安定性の制約](limitations.md)も参照 |
| `sweep_order` | `forward` | `forward`または`alternating` |
| `parallel` | `serial` | `serial`、`omp`、`mpi`、`hybrid`。対応する実行プログラムが必要 |
| `nrep` | `1` | 独立なレプリカ数。正の整数 |
| `seed` | `12345` | 基準となる乱数種。符号なし64ビット整数 |
| `output_file` | `observables.dat` | スカラー観測量の保存先。`none`でファイル保存を無効にし、標準出力のみとする |
| `profile` | `0` | `1`で時間計測の出力を有効にする |
| `profile_file` | 有効時は`profile.dat` | 時間計測データの保存先 |
| `replica_log` | `nrep>1`では`replicas.dat`、それ以外では無効 | 保存先を明示すればレプリカが1個でも有効。`none`で無効にする |
| `replica_bin_file` | 無効 | replica・温度ごとのbin和の保存先 |
| `conditional_measure` | `0` | `1`で条件付き局所D/K/Eの比較用4列を追加。`replica_bin_file`が必須。[式と出力規約](conditional-measurements.md)を参照 |
| `szz_q`, `sperp_q` | `none` | スピン測定の運動量指定。`none`、`af`、`all`、または`mx:my,...` |
| `szz_file`, `sperp_file` | `szz.dat`, `sperp.dat` | 有効にしたスピン測定の保存先 |
| `spin_consistency_file` | `none` | 対応するビンから求めた`Sperp/2-Szz`の保存先。両スピン測定で同じ順序の運動量指定が必要 |
| `sign_file` | `none` | 温度ごとの符号統計の保存先。[符号reweighting](#符号reweighting)を参照 |

化学ポテンシャルは、`mu`を指定しなければ`mu=U/2`です。指定値は`U/2`へ丸めずにそのまま使います。
粒子・正孔対称で符号が`+1`になるのは、`mu`が省略または厳密に`U/2`で、ホッピング行列が二部の場合だけです。
非二部格子では`mu=U/2`でも密度1とは限りません。それ以外の場合は[符号reweighting](#符号reweighting)を参照してください。
`nwarm>=0`、`nbin>=2`、`nmeas>=nbin`を満たし、`nmeas`は`nbin`で割り切れる必要があります。
平均値と誤差は`nrep*nbin`個の測定ビンをまとめて求めます。
レプリカ数は、MPIのランク数やOpenMPのスレッド数とは別の設定です。

各`beta/dtau`は整数との差が`1e-9`以内でなければならず、条件を満たさない場合は
エラー終了します。実際の逆温度は、整数の虚時間スライス数と`dtau`の積です。
スピン出力では指定値`beta_requested`と実際の値`beta`を区別しています。
結果を比較するときは実際の逆温度を使ってください。
温度は`k_B=1`として`T=1/beta`です。既定のホッピングではエネルギー単位は`abs(t)=1`です。

## 格子とホッピング行列のファイル

鎖には`lattice=chain`と`Lx`、正方格子には`lattice=square`と`Lx`、`Ly`を指定します。
`pbc`（別名`bc`）は両方向の境界をまとめて指定します。方向ごとに指定するときは、
代わりに`bc_x`と`bc_y`に`periodic`、`antiperiodic`、`open`のいずれかを指定します。
省略した方向は周期境界なので、`bc_x=antiperiodic`だけでx方向反周期・y方向周期になります。
`bc_y`は`lattice=square`でのみ使え、どちらのキーも`pbc`や`bc`とは併用できません。

周期境界の方向は、最後のサイトから最初のサイトへの結合（`x=Lx-1`から`x=0`、
`y=Ly-1`から`y=0`）を振幅`t`で閉じます。反周期境界の方向はこの結合だけを`-t`とし、
ほかの結合は変えません。開放境界の方向にはこの結合がありません。
閉じた方向（周期・反周期）の長さが1より大きく奇数の場合は二部格子でなくなり、符号reweightingの経路で計算します。
開放境界はどの長さでも使えます。反周期境界の方向には長さ4以上の偶数が必要です。
長さ2では、閉じる結合が通常の結合と打ち消し合うためです。
長さ2の周期境界の方向は、同じサイト対を2回結びます（振幅`2t`）。

`tprime`は`lattice=square`に対角結合`(x,y)-(x+1,y+1)`と`(x,y)-(x+1,y-1)`を行列要素`tprime`で加えます。
開放境界の方向を越える対角結合は作らず、反周期境界の方向を越えるたびに符号を反転します
（角を越える結合では両方の符号を掛けます）。長さ2の周期境界の方向では最近接と同じく両方の像を累積します。
値はホッピング行列の要素であり、物理的な`H = -t sum_NN - t' sum_NNN`の`t=1, t'=0.2`は
`t=-1`、`tprime=-0.2`です。非ゼロ要素の2色塗り分けが存在するときだけ二部格子とします
（例: `t=0`かつ`tprime!=0`は`(-1)^x`で塗り分けられる）。微小な非ゼロの`tprime`も無視しません。
`lattice=chain`と`lattice=file`では、`tprime=0`でも指定するとエラーです。

出力のメタデータには、両方向とも周期境界、または両方向とも開放境界のとき、
従来どおり`pbc=1`または`pbc=0`と記録します。それ以外の組では、この部分を解決した値、
例えば`bc_x=antiperiodic bc_y=periodic`（鎖では`bc_x=antiperiodic`）に置き換え、
スカラー出力のヘッダー行にも同じ値を記録します。

`lattice=file`では、`latfile`の最初の値にサイト数を記述し、続いて実対称な
ホッピング行列を行ごとに空白区切りで記述します。対角要素はゼロでなければなりません。
行列要素がホッピング振幅を直接指定し、入力の`t`は掛けません。
例えば2サイトのホッピング行列は次のように指定します。

```text
2
0 -1
-1 0
```

ホッピング行列のファイルにはコメント行を入れないでください。
ホッピングが定めるグラフが二部格子でない場合は符号reweightingの経路で計算します。
fileの判定は入力によって異なります。

- `mu`または`sign_file`（`sign_file=none`を含む）を指定した入力では、行列が厳密に対称である必要があり
  （伝播は片側の三角だけを読みます）、全対角要素が厳密に`0`で、どれほど小さくても非ゼロの非対角要素が
  2色に塗り分けられるときだけ粒子・正孔対称とします。判定はエネルギーの単位によりません。
- これらのkeyのない入力は従来の挙動と出力を保ちます。読込みは`1e-12`以下の対角要素と非対称性を許し、
  二部性の判定では`1e-12`以下の要素を無視しますが、その要素は伝播する行列に残ります。このため、
  粒子・正孔対称でないfileを対称として扱う場合があります。厳密な判定には`sign_file`（または`mu`）を加えてください。
`hopping_used.txt`には、構築した行列を同じ形式で出力します。
スピンの運動量指定には組み込みの鎖または正方格子が必要で、`lattice=file`には
対応していません。反周期境界では、運動量指定を使えるよう、ホッピング行列のファイルではなく
`bc_x`/`bc_y`を使ってください。

## スカラー観測量の出力

`observables.dat`と標準出力には同じ内容を出力します。
`#`から始まる実行条件と列名のコメントに続き、指定した温度ごとに1行の
空白区切りデータを書き出します。保存先は`output_file=results.dat`で変更できます。
各温度の計算後に出力バッファを書き出します。途中で失敗した計算では、
不完全なファイルが残ることがあります。
スカラー出力を開けない、または書き込めない場合は、非ゼロの終了コードで終了します。
MPI/hybridでは、ランク0だけが集約した結果をファイルへ書き出します。

標準出力を保存する既存のスクリプトでは、`output_file=none`を指定し、
`./dqmc input.txt > out.dat`と実行できます。MPI/hybridでは、MPIランチャーの
標準出力を`output_file`と同じファイルへリダイレクトしないでください。
ランチャーが出力を追加したり、別のストリームを介して転送したりする場合があります。
スカラー出力は次の14列です。

```text
T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign acceptance dAcceptance
```

各列の意味は[英語README](../README.md#model-and-observables)と
[日本語README](../README_ja.md#模型と観測量)に記載しています。
エネルギーと`ntot`は系全体の値、`doublon`はサイト当たりの値です。
誤差はビン・ジャックナイフで推定します。有限の時間刻みによる系統誤差は含まず、
誤差の評価だけで計算の収束が保証されるわけではありません。
3種類のエネルギーの規約は、`E_hub=<K_hop+U sum n_up n_down>`、
`E_gc=E_hub-mu*ntot`、`E_ph=E_hub-(U/2)*ntot+(U/4)*n_site`です（`mu=U/2`では`E_ph=E_gc+(U/4)*n_site`）。
粒子・正孔対称性がない場合の推定量・`sign`・`nan`の行は[符号reweighting](#符号reweighting)に従います。列の構成は変わりません。

スカラーファイルはレプリカを集約した結果で、個々の測定値やビンごとの標本は含みません。
最後の2列`acceptance`と`dAcceptance`は元の12列形式の後に追加したもので、
古い参照ファイルには含まれない場合があります。

## スピン観測量の出力

`szz_q`と`sperp_q`で、それぞれの測定対象の運動量を選びます。

- `none`：測定を無効にします。保存先を指定するだけでは測定は有効になりません。
- `af`：反強磁性の運動量を指定します。長さが1より大きい各方向の運動量は`pi`で、
  その方向の格子サイズは偶数でなければなりません。
- `all`：組み込み格子のすべての離散運動量を指定します。
- `mx:my,...`：整数インデックスを明示します。例えば`Lx=4`の鎖では
  `szz_q=0:0,2:0`と指定できます。`0<=mx<Lx`、`0<=my<Ly`を満たし、
  鎖では`my=0`です。同じ運動量を重複して指定するとエラーになります。

運動量は`qx=2*pi*mx/Lx`、`qy=2*pi*my/Ly`です。開放境界では、これらは
Fourier変換のサンプリング点であり、並進対称性に対応する量子数ではありません。
反周期境界の方向でも、スピン相関は境界での位相を持たないため同じ整数の運動量を使い、
`af`は長さが1より大きい各方向で`pi`を指します。ただし反周期の結合は模型そのものを変えるため、
値は周期境界のときと一般に異なります。
各測定を無効にするには、対応する運動量指定を`none`にします。
測定が有効な状態で`szz_file=none`または`sperp_file=none`を指定するとエラーになります。
各出力には異なる保存先を指定してください。

各スピンファイルには、`#`から始まる実行条件と列名のコメントに続き、
指定した逆温度と運動量の組ごとに1行の空白区切りデータを出力します。
Szzの列名は次のとおりです。

```text
# beta_requested beta T q_index mx my qx_over_pi qy_over_pi qx_folded_over_pi qy_folded_over_pi Szz dSzz
```

Sperpでは最後の2列が`Sperp dSperp`、対応するビンから求める整合性出力では
`DeltaSU2 dDeltaSU2`になります。`q_index`は選択した運動量の順序に対応する、
0から始まるインデックスです。元の運動量の範囲は`[0,2*pi)`、折り返した運動量の
範囲は`(-pi,pi]`です。運動量の値を記した4列はいずれも`pi`で割った値です。

`Szz(q)=(1/n_site) sum_ij exp[i*q*(r_i-r_j)] <Sz_i Sz_j>`と定義し、
`Sz_i=(n_i_up-n_i_down)/2`です。一点平均の積を差し引かない完全な相関関数で、
3倍の係数も掛けません。`Sperp=Sxx+Syy=(S+-+S-+)/2`も同じサイト数で規格化します。
これらの構造因子の運動量についての総和と、サイト当たりの値は異なります。

このSU(2)対称な模型では、任意の時間刻みについて集団平均で`Sperp=2*Szz`が成立します。
個々の補助場配置で成立する必要はありません。
`DeltaSU2=Sperp/2-Szz`は対応する測定ビンを用いて求め、ジャックナイフで誤差を評価します。
これはサンプリングや実装の診断量であり、Trotter誤差を直接推定する量ではありません。

## 符号reweighting

配位は`|W|`で標本化し、各測定は`s=sign(W)=±1`を持ちます。`s`は安定化点と各sweepの終わりに、
安定化した行列式から厳密に再設定します。

推定量。粒子・正孔対称なら従来の推定量（binごとの比をjackknife）を保持します。それ以外では、
各温度のスカラー・Szz・Sperp・DeltaSU2を全`B=nrep*nbin`個のreplica binの合算比とします。
raw和は`C_b`（測定数）、`S_b=sum s`、`A_b=sum s*O`です。

```text
R = sum_b A_b / sum_b S_b
R(-b) = (sum A - A_b) / (sum S - S_b)
err^2 = (B-1)/B * sum_b (R(-b) - mean_b R(-b))^2
```

符号和にかかわらずbinを除外しません。`DeltaSU2`は両channelに同じbinを使います（対応あり）。
スカラーの`sign`列は`sum S / sum C`で、`acceptance`は試行当たりの統計なので符号で重み付けしません。
S(q)の総和則は各binのraw和で検査し、符号和によらず成立します。

温度ごとの`ratio_status`はその温度の全観測量で共通です。

| 条件 | 物理量と誤差 | 符号・受理率・raw和 | 実行 |
| --- | --- | --- | --- |
| `sum S > 0`かつ全`sum S - S_b > 0` | 合算比とjackknife | 出力 | `ok` |
| `sum S <= 0` | `nan` | 出力 | `nonpositive_total`、警告、継続 |
| `sum S > 0`だが`sum S - S_b <= 0`がある | `nan` | 出力 | `nonpositive_loo`、警告、継続 |
| raw和の非有限値、count不整合、数値破綻、MPIデータの欠落、I/O失敗 | 認定しない | | エラー、非ゼロ終了 |

符号が誤差の2倍以内で0の場合、符号和が非正のreplicaがある場合、statusが`ok`でない場合は、
有限な合算比が出ても測定として分解できていません。通常の計算と同様に、平衡化、自己相関（rebin）、
独立replicaを確認してください。`sign`は物理量が`nan`の場合も計算します。

制限: `tempering=dtau_ladder`、`sweep_order=alternating`、`conditional_measure=1`は粒子・正孔対称性が
必要で、出力を開く前にエラーになります。forward sweepと、`fixed`・`polarized`のsite選択による
`global_update=site`は使えます。非二部格子では`global_site_diag_file`のstaggered指標`d`に物理的な意味は
ありません。標準エラー出力に
`INFO: ph_symmetric=0 (mu=..., tprime=..., bipartite=...); sign problem possible`を出します。

拡張schema。`mu`・`tprime`・`sign_file`（`sign_file=none`を含む）を指定した入力、粒子・正孔対称性のない模型
（新keyのない奇数長周期chainを含む）は拡張headerを書きます。scalar headerの1行目とspin fileの
lattice行で、`mu`を17桁にし、` tprime=... ph_symmetric=... estimator=bin_ratio|pooled_ratio`を加えます。
列は変わりません。推定量はschemaではなく`ph_symmetric`で決まります。それ以外の入力は従来のheaderを
byte単位で保持します。

`sign_file`の省略と`sign_file=none`の明示は、どちらも符号fileを出力しません。
ただし、ほかの新keyもない場合に旧入力の挙動を保つのは省略時だけです。
`none`を明示すると、保存先を指定した場合と同じ厳密なfile格子のPH判定と拡張headerを使います。

拡張入力の`replica_bin_file`は
`# replica_bin_schema=2 mu=... tprime=... ph_symmetric=... estimator=... nrep=... nbeta=...`で始まり、既存の全列
（条件付き測定の列があればその後）の末尾に`sum_sign_N`と`sum_sign_Egc`を加えます。
`sum_sign_Eph`は`sum_sign_Ehub-(U/2)*sum_sign_N+U*n_site*sum_sign/4`です。この行がなければ従来の形式
（schema 1）です。

`sign_file`（既定`none`）は`# sign_file_schema=1`、定義、計算条件と、温度ごとに1行の次の列を持ちます。

```text
beta_index beta_requested beta_effective T Ltr dtau sign dsign f_neg df_neg
replica_sign_mean replica_sign_se nrep nbin count_total sum_sign_total
negative_count n_replica_nonpositive n_bins_nonpositive n_loo_nonpositive
ratio_status sign_unresolved no_negative_observed
```

（file内では1行）。`f_neg=(1-sign)/2`は測定した配位のうち負符号の割合で、
`negative_count=(count_total-sum_sign_total)/2`です。どちらもclipしません。`replica_sign_mean`は
非正のものを含む全replicaの`S_r/C_r`の平均で、`replica_sign_se`は`nrep=1`では`nan`です。
`sign_unresolved=1`は`0<sign<=2*dsign`、`no_negative_observed=1`は粒子・正孔対称性がなく負符号が
測定されなかった場合です。粒子・正孔対称なら`sign=1`でcounterは0です。root rankだけが書き、
出力先の衝突検査に含めます。

`scripts/analyze_sign_bins.py BINS.tsv [--rebin K] [--unit bin|replica] [--json]`は、replica-bin file
（schema 1または2。ほかのschemaは拒否）から符号統計・status・合算比を再計算します。`--rebin K`は
各replica内の連続する`K`個のbinをまとめ（`nbin`は`K`の倍数）、`--unit replica`はreplicaを
jackknifeのblockにします。schema 1のfileには`E_hub`、`doublon`、保存済みのq列だけがあります。
まとめる前に全行を検査し、違反はエラー（非ゼロ終了）とします（符号和の非正はstatusで、これとは別です）。
符号・エネルギー・doublon（schema 2では`N`と`E_gc`も）の和は有限であること、q列は`# szz_Q_index=...`が
測定済みとする列では有限、未測定列ではNaNであること（`inf`・`-inf`は常にエラー）、
各温度の`beta_requested`・`beta_effective`・`Ltr`が1つであること、
各replicaのseedが1つで互いに異なり、bin `0..nbin-1`とcount・sweep範囲がそろうこと、replicaと温度が
`0..nrep-1`・`0..nbeta-1`であることを確認します。空の表もエラーです。schema 2は`nrep`と`nbeta`を記録します。
schema 1では連続性しか確認できず、最後のreplicaや温度の欠落は`--nrep N --nbeta K`を指定したときだけ検出します。

## レプリカ情報・時間計測・安定化診断

`replica_log`は計算条件を記録する空白区切りのデータファイルで、レプリカ別の
観測量は含みません。列名は`# beta T replica_id seed nwarm nmeas nbin status`で、
MPI/hybridでは最後に`rank`を追加します。逆温度とレプリカの組ごとに1行を出力します。

`profile=1`の場合、時間計測データの列名は
`# beta T dtau Ltr phase region calls total_sec avg_sec frac_beta wall_sec thread_total_sec nrep parallel`
です。MPI対応ビルドでは最後に`nranks`を追加します。
各行は計測した処理区間に対応します。`Ltr`は虚時間スライス数です。

次の3つの保存先設定により、空白区切りの安定化診断データを有効にできます。
ファイル名には`drift.dat`、`scales.dat`、`centered.dat`などを使用してください。

| キー（既定は未設定） | 内容 |
| --- | --- |
| `stab_drift_file` | 更新したGreen関数と再構築したGreen関数のずれ |
| `udv_scale_file` | 左右のUDV因子の対数スケール |
| `udv_centered_file` | centered UDVのスケール、オフセット、数値範囲の残りの余裕 |

これらは`parallel=serial`のみで使用できます。
`udv_centered_file`には、さらに`green_rebuild=centered`が必要です。
別名はそれぞれ`stabilization_drift_file`、`udv_scale_diagnostics_file`、
`udv_centered_diagnostics_file`です。各ファイルは`#`から始まる列名で始まります。
無効にするには設定自体を省略してください。この3つの保存先では、文字列`none`は
無効化を意味しません。

## ファイル形式と過去のデータ

新しく出力する表は、`.dat`のファイル名、空白区切り、列名を含む`#`コメントに
統一しています。列の順序、規格化、出力する桁数は変更していません。
入力設定と「サイト数＋ホッピング行列」のファイルには`.txt`を使用します。

これは開発途中の旧形式からの変更です。以前はスピン・安定化診断にTSV、
レプリカ・時間計測にCSVを使い、列名もコメントではありませんでした。
新しいファイルを読む処理では、`#`行を読み飛ばし、データ行を空白で分割してください。
明示したファイル名はそのまま使用するため、拡張子を`.tsv`や`.csv`にしても
旧形式の区切り文字や列名には戻りません。

収録した過去のデータ、その解析スクリプト、日付付きの開発記録は、当時のファイル名と
形式を保っています。既存のCSV/TSV読取処理は、これらの保存済みデータに対して有効です。
以下の日付付きスピン仕様書は旧形式を記述しています。
現在のファイル形式と既定値は、このページを参照してください。

科学的な仕様の詳細：

- [Szzの入出力](2026-08-21-szz-structure-factor-usage.md)
- [Sperpの入出力と統計的な解釈](2026-08-22-sperp-structure-factor-usage.md)
- [エネルギーとED比較の規約](../VALIDATION.md)
