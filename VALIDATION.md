---
date: 2026-09-30
datetime: 2026-09-30 13:47 JST
model: OpenAI GPT-6 (Codex; revision), Codex GPT-5 (original), Claude Sonnet 5 (Anthropic; 2026-09-27 addition), Claude Opus 5.5 (Anthropic; 2026-09-28 addition)
summary: |
  Delta tau-ladder並列テンパリング（PT）の実装検証: cross-weight直接検査、厳密列挙χ²と負の対照、
  3215eeeとのbyte同一性、serial/OpenMP/MPI/hybrid一致、MPI失敗経路、L6 chain U=4のPT対独立chain
  screen（16/16、最大|z|=1.808）はすべて合格。PTがTrotter誤差を解消したとは主張しない。
  09-28: 統合ladder driverの厳密列挙回帰（3選択肢組×3 slot×E/D、18比較で最大|z|=1.89）と
  slot破綻/交換失敗のmessage検査を追加し、field_init=uniformの倍精度上限を実測で記録した。
  120 replicaの段階B比較は受理増加に対して混合改善基準を満たさずfail（§9）。PT検証は§10。
  Stage Aの4×4・12 replica診断pilotは正常終了。受理8件・4 replicaで両指標とも判定不能。
  半充填ハバード模型の E(T) を grand-canonical ED/TPQ と比較する検証手順。
  アンサンブル整合・dtau→0 外挿・規約変換・符号/粒子数チェック・2D の ED サイズ制約をまとめる。
  Global HS update の実装検査、4×2 clusterの初回84 runと追加検証を記録した。
  追加測定後に科学的受入と既定頻度100の判定を通過。beta16の微小なSz2の厳密値との一致は未確定。
  09-22にtest hook分離、履歴なしtest、two-spin検査を追加し、4形態と旧commit比較を再検証した。
---

# 検証手順: E(T) を ED/TPQ と比較

QMC 出力列:

```text
T  E_hub dE_hub  E_gc dE_gc  E_ph dE_ph  ntot dN  doublon dD  sign
```

- `E_hub = <H0> = ekin + eint`
- `E_gc = <H0 - mu N>`
- `E_ph = <H0 - (U/2)N + (U/4)n_site>`
- `H0 = K_hop + U sum_i n_i↑ n_i↓`

## 0. アンサンブルの整合

DQMC は `mu=U/2` の grand-canonical ensemble。
粒子正孔対称点で `<N>=n_site` にはなるが、固定粒子数の canonical ensemble と有限温度・有限サイズで一致するとは限らない。

比較の原則:

- grand-canonical ED と比較する。
- ED 側も `Z = Tr exp[-beta(H0 - mu N)]`、`mu=U/2` とする。
- `mu` は重みに一度だけ入れ、演算子として何を測るかを明確にする。
- 主比較は QMC の `E_hub=<H0>` と grand-canonical ED の `<H0>`。
- `E_gc` を使う場合は ED 側も `<H0-mu N>` を返す。

canonical TPQ/ED を使う場合は、直接一致を主張せず、アンサンブル差があり得る検証項目として扱う。

## 1. dtau→0 外挿

固定 beta で `dtau=0.2,0.1,0.05` などを実行し、各エネルギーを `dtau^2` で線形外挿する。

```text
E(dtau) = E(0) + c dtau^2
```

U=4,8 では統計誤差を小さくすると Trotter 系統誤差が支配的になることがある。
ED/TPQ 比較は原則として外挿後に行う。

## 2. ED/TPQ との比較

最初の対象:

- 1D L=4, PBC
- U/t=4
- half filling
- grand-canonical ED

規約変換:

```text
E_ph = E_hub - (U/2) ntot + (U/4) n_site
E_gc = E_hub - mu ntot
```

相手が `U(n↑-1/2)(n↓-1/2)` 形なら `E_ph` 列を使う。
相手が非対称 `U n↑n↓` 形の `H0` を測っているなら `E_hub` 列を使う。

判定目安:

```text
|E_qmc(dtau→0) - E_ED| < 2 * (統計誤差 + 外挿誤差)
```

外挿前の生データで比較する場合は、統計誤差だけで判定しない。

## 3. 粒子数と符号

必ず同時に確認する:

- `ntot ≈ n_site`
- `sign = 1`

v1 は non-bipartite lattice を実行エラーにする。
そのため `sign` は半充填二部格子における絶対符号として 1 に保たれる想定。

## 4. 2D 検証の注意

- 長さ2の方向を持つ PBC 格子は同一 pair の二重結合が出る。通常の単結合格子と混同せず、ED側でも実際の hopping 行列を再現する。
- grand-canonical ED は Hilbert 空間が `4^N` で増える。
- 小 OBC 系を基本とする。下記4×2 PBC検証は、二重結合を含む hopping 行列の一致を確認した例外である。

候補:

- `2x2` OBC
- `2x3` OBC
- `2x4` OBC / ladder

`4x4` は grand-canonical ED には大きすぎるため、TPQ や将来検証向けとして分けて扱う。

任意格子入力を使う場合は、DQMC が使った hopping 行列を ED 側にも渡し、規約を一致させる。

## 5. Szz(q) の検証

equal-time longitudinal spin structure factor の規格化、U=0 独立 oracle、sum rule、
PH mapping、parallel/MPI integrity、bin-width check は
`docs/2026-08-21-szz-structure-factor-validation.md` を参照する。

入力 selector と TSV schema は
`docs/2026-08-21-szz-structure-factor-usage.md` にまとめている。

## 6. Global HS update（4×2 cluster, U/t=8）

### 実装検査

- `make test`, `test_omp`, `test_mpi`, `test_hybrid`, `test_slow` は合格。
  OpenMP は2 thread。MPI compiler wrapper が参照する compiler がローカルで欠けていたため、検証processだけ `OMPI_CC=cc` を指定した。
- 同じ toolchain の旧commit `463dc75` と比較し、更新無効時の既存出力・乱数列を維持する回帰が合格。
  bin出力だけを有効化した場合も既存出力は一致した。
- 256配位の独立な密行列 oracle による受理境界、PH/non-PH重み、rollback、Green再構築を検査した。
  無条件受理・受理比逆転・rollback削除の3変異はすべてtestが検出した。kernelのASan/UBSanも合格。
- 数値/I/O失敗では全MPI rankの終了コードを直接回収して非0終了を確認した。
  macOSに存在しない `/dev/full` のunit testだけはskip。portableなwrite/close失敗注入は全形態で検証した。
- 大域更新なしの同じ4×2 slow regressionは5 CHECKで失敗し、有効時は合格した。
  これは下記の科学的な受入判定とは別の回帰検査である。

### 条件とprovenance

square `Lx=4, Ly=2, pbc=1, t=-1, U=8, mu=4`、grand canonical。
Ly=2方向の二重結合を含み、hopping SHA-256は全runで
`52d69c565bd12324cda1312292105a505fdb9ff646859b6d62c0f4ec70666a58`。
EDと同じ行列である。energyは標準の `U n_up n_down` 形の `E_hub/N`、Dはper site、spinは `Sz=(n_up-n_down)/2`。

初回の共通入力は `nwarm=5000, nmeas=50000, nbin=25, stab=4, nrep=1`、
`sweep_order=alternating, green_rebuild=combine, szz_q=all, sperp_q=all`。
bin出力、spin consistency、profileを有効にした。

| beta | dtau | interval | 独立replica数 | 目的 |
| --- | --- | ---: | ---: | --- |
| 4, 8, 16 | 0.05, 0.025 | 10 | 各12、計72 | 科学的受入・dtau²外挿 |
| 16 | 0.05 | 100 | 12 | 既定頻度の評価 |

- 固定binaryのsource commit: `da6b65e0c3b08d7b87ed67888fa2349e07850a3c`。
- binary SHA-256: `fc65181654ac051aa0c8ee5f328c539e54bc70b546d854b3fa524e50a26fbddd`。
- Apple Clang 16、C11/O2、Accelerate、serial、BLAS/OpenMP thread数1。local同時実行は最大4。
- seedは下記の機械可読集計に含める。入力/出力checksum、toolchain、終了状態はraw runごとのmetadataに保持した。
  対応する旧48入力とのseed一致、全84runのheader・hopping・bin数・counterを検査した。

### 初回84 runの判定

全runが正常終了し、stderrは空だった。長寿命状態は0/84。
dtau²外挿の18判定とbeta4の旧run比較はすべてpass。
初回の総合判定は下記の理由で **科学的受入fail、既定頻度undetermined** であり、合格に読み替えない。

| 対象 | 結果 | 判定・次の確認 |
| --- | --- | --- |
| beta4 / dtau0.05 / interval10、SU(2)差 | 0.0238141 ± 0.0086671（2.75 SE） | fail。別seed12本で再確認 |
| beta4 / dtau0.025 / interval10 | replica1の再block SE比1.6063 | undetermined。12本全体を延長 |
| beta16 / dtau0.05 / interval10 | replica4の再block SE比1.6486 | undetermined。12本全体を延長 |
| beta16 / dtau0.05 / interval100、SU(2)差 | SE=0.0465765（上限0.04） | undetermined。12本全体を延長 |

初回の各判定と全replicaの量は、raw dataとともに初回の `comparison.json` に保持する。
追加検証は、初回判定を踏まえて明文化した延長手順（r1〜r3の段階、延長sweep数、判定基準を実行前に固定した内部の実装計画。
本repositoryには含めず、条件と結果は下記の機械可読集計に残す）と、各段階の実行前に固定したmanifestに従って完了した。

### 追加検証の経過

| 段階 | 内容 | 科学的受入 | 既定頻度100 |
| --- | --- | --- | --- |
| r1 | 独立seed12本と、未確定3条件を各12本・200000 sweepへ延長 | beta4の全24本SU(2)差が3.11 SEでfail。beta16横スピン精度も未確定 | pass |
| r2 | beta4/dtau0.05の全24本を200000 sweepへ延長 | beta4再確認はpass。beta16の精度2判定がundetermined | pass |
| r3 | beta16/dtau0.05/int10の全12本を800000 sweepへ延長 | pass | pass |

r2でのbeta4のSU(2)差は全24本で `0.006783 ± 0.007351`。
独立seed12本だけでも `0.006592 ± 0.011391` でpassした。
Szzの再block判定は延長した全条件で安定し、長寿命状態は追加runでも検出されていない。
r2時点の未達はbeta16/dtau0.05/int10のSU(2) SE `0.094019 > 0.04` と、beta16の横スピン外挿SE `0.035061 > 0.03`。

この条件のreplica9、bin33（0始まり）にscaled Sperp平均約118.34の大きな値がある。
同じ軌跡の先頭33 binの完全一致を確認した隔離replayで、その中の1配位を保存し、
100桁と160桁の行列積・逆行列で独立に確認した。
scaled Sperpは `233383.06156743`、doubleとの差は相対 `2.28e−12`、100桁と160桁のGreen差は最大 `3.16e−28`。
この配位の大きな推定値は丸め誤差によるものではなく、元データから除外していない。
productionのsourceとbinaryは変更していない。

別の短い診断では、beta16/dtau0.05の初期5000 sweepを観測した。
旧runで `Sz_tot²≈1` に留まったreplica3・10は、interval10/100の双方でそれぞれ大域反転を1回受理し、
後続binで `Sz_tot²` が丸め誤差程度へ移った。他の10本は初期5000 sweepでも大域受理0。
この過渡的な診断は平衡値の集計に含めない。

各段階の結果・入力・checksumは、それぞれの判定JSONとmanifestへ保存した。
完全なraw data・再現script・内部診断は、実装検証記録から参照できる。

### 最終判定（r3まで）

**科学的受入pass、既定頻度100の評価pass。** 指定した判定基準を変えずに評価した。
最終選択は96 run pathで、各条件内に12本（beta4/dtau.05は24本）の独立replicaを持つ。
長寿命状態は0/96、Szzの再block未安定は0、17個のED一致判定・1個の残留上限screenと旧beta4比較もすべてpass。
beta16のSz2については残留上限screenのpassであり、微小な厳密値との一致は判定不能とする。
この評価範囲では `global_interval` の既定値100を維持する。`global_update` の既定値は `none` のままとする。

beta16/dtau.05/int10のSU(2)差は `-0.021142301 ± 0.0235096`、
横スピン外挿は `1.6590942 ± 0.0159396` となり、未達だった精度2基準を満たした。
延長runの60個のprefix照合がすべて一致し、元の短いchainを独立標本として重複集計していない。
[機械可読の最終集計](docs/validation/global-hs-4x2-2026-09-21.json) にseed、条件別統計、全判定、
source/binary/ED/解析のchecksumと完全な判定reportのSHA-256を記録する。

SU(2)差は同一binの`3Szz(Q)−1.5Sperp(Q)`。SEはreplicaを単位として評価する。
長寿命状態の判定とSzzの再block/SD-SE判定は、specの閾値を維持する。

| beta | dtau | interval | replicas | nmeas/replica | trapped | SD/inner SE | unstable Szz SE | SU(2)差 ± SE |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 4 | 0.025 | 10 | 12 | 200000 | 0 | 0.70094 | 0 | 0.01518424 ± 0.0078545 |
| 4 | 0.05 | 10 | 24 | 200000 | 0 | 0.74195 | 0 | 0.006782998 ± 0.007351 |
| 8 | 0.025 | 10 | 12 | 50000 | 0 | 0.74441 | 0 | 0.007996305 ± 0.018909 |
| 8 | 0.05 | 10 | 12 | 50000 | 0 | 1.28506 | 0 | 0.003779295 ± 0.018831 |
| 16 | 0.025 | 10 | 12 | 50000 | 0 | 0.71964 | 0 | -0.006570009 ± 0.011869 |
| 16 | 0.05 | 10 | 12 | 800000 | 0 | 0.59918 | 0 | -0.0211423 ± 0.02351 |
| 16 | 0.05 | 100 | 12 | 200000 | 0 | 0.79067 | 0 | -0.008580894 ± 0.014779 |

### dtau²外挿の最終値

`x0=(4*x(.025)−x(.05))/3`、誤差は仕様の二乗和を使う。2刻み幅のseed集合には重複がない。

| beta | 量 | DQMC外挿値 | SE | ED | 判定 |
| ---: | --- | ---: | ---: | ---: | --- |
| 4 | E/N | -0.874254541 | 0.00081392 | -0.873175758 | pass |
| 4 | D per site | 0.0719160305 | 0.00011197 | 0.0719991305 | pass |
| 4 | 3 Szz(Q) | 1.66609267 | 0.0032403 | 1.65945979 | pass |
| 4 | 1.5 Sperp(Q) | 1.64810801 | 0.0097079 | 1.65945979 | pass |
| 4 | SU(2) difference | 0.0179846501 | 0.010756 | 0 | pass |
| 4 | Sz_total squared | 0.107220904 | 0.0012619 | 0.109645588 | pass |
| 8 | E/N | -0.89021465 | 0.001255 | -0.890014615 | pass |
| 8 | D per site | 0.0720366703 | 6.2786e-05 | 0.0718901091 | pass |
| 8 | 3 Szz(Q) | 1.67486432 | 0.0075523 | 1.67116438 | pass |
| 8 | 1.5 Sperp(Q) | 1.66546234 | 0.022403 | 1.67116438 | pass |
| 8 | SU(2) difference | 0.00940197468 | 0.025981 | 0 | pass |
| 8 | Sz_total squared | 0.00573329176 | 0.0011215 | 0.00597809399 | pass |
| 16 | E/N | -0.889288798 | 0.00084956 | -0.890836624 | pass |
| 16 | D per site | 0.0719160329 | 6.4448e-05 | 0.0718697791 | pass |
| 16 | 3 Szz(Q) | 1.65738161 | 0.006561 | 1.67072706 | pass |
| 16 | 1.5 Sperp(Q) | 1.65909418 | 0.01594 | 1.67072706 | pass |
| 16 | SU(2) difference | -0.00171257827 | 0.01766 | 0 | pass |
| 16 | Sz_total squared (residual screen) | -8.30179814e-16 | 5.9662e-16 | 1.77463051e-05 | pass |

beta16のSz_total squaredは残留上限screenであり、`x+2.5*SE=6.61373e-16`。
厳密値1.77463e−5との一致は判定不能とする。負の推定値はclipしていない。

### 初回の計時と適用限界

| beta | dtau | interval | 測定中の大域受理率 | pass当たり秒 | 1 runの平均wall秒 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 0.05 | 10 | 0.029969 | 0.000718 | 18.81 |
| 4 | 0.025 | 10 | 0.031360 | 0.001399 | 38.03 |
| 8 | 0.05 | 10 | 0.000425 | 0.001399 | 37.29 |
| 8 | 0.025 | 10 | 0.000419 | 0.002746 | 75.68 |
| 16 | 0.05 | 10 | 0 | 0.002758 | 74.57 |
| 16 | 0.025 | 10 | 0 | 0.005550 | 154.14 |
| 16 | 0.05 | 100 | 0 | 0.002764 | 59.54 |

beta16/dtau0.05ではinterval100から10への変更でwallが15.03秒（25.2%）増えた。
同一host・4並列での実測だが、他processの影響を含む。pass時間はwarmupを含むprofilerの`total_sec/calls`を使い、
測定期間だけのattemptsから推定していない。

beta16では測定中の大域受理が0であり、`<Sz_tot²>`も丸め誤差程度だった。
specの残留上限判定と、厳密値約1.77e−5の一致を区別する。稀な励起sectorを十分観測したとは主張しない。
2点外挿ではO(dtau^4)を検査できない。2刻み幅のseed集合には重複がなく、外挿SEは仕様の二乗和の式を使う。
同じseedを使う頻度10/100の比較では、対応するreplicaの差からSEを求めた。
延長対象は前段の結果を見て選んでいるため、単回固定実験としての有意水準保証は主張しない。
この検証は4×2に限る。4×4・6×6の効果は未検証で、受理率だけを混合の十分性の根拠にしない。

### 2026-09-22の回帰検査の補強

数値kernelと既存の科学的検証データは変更せず、次の検査を追加した。

- 失敗注入の環境変数は`AFQMC_TEST_HOOKS`を定義した専用buildでのみ有効。
  通常のserial/OpenMP/MPI/hybrid binaryでは4種類のhook変数を設定してもstdoutとbin出力がbyte一致する。
  専用buildでの数値・書込み・close失敗とMPI全rankへの伝播は引き続き検査する。
- 通常の`make test`はGit履歴なしでも実行可能。省略/none/bin-only/profileの一致を同一binaryで検査する。
  `make test_global_default`はGit commit `463dc75`との同toolchainによる厳密なbyte比較を行う別targetとし、履歴不足では明示的に失敗する。
- two-spin（`half=0, mu=U/2`）でも全256配置の受理境界・棄却復元・詳細釣り合い・定常分布を検査する。
  forwardのcombine/centered/two-sidedとU=0でGu/Gdの再構築と次のlocal sweepの一致を確認する。
  別コピーでGd再構築を省く変異を加えると、新しい状態検査が失敗することも確認した。

macOS/Accelerate/Open MPIで`make test`、`test_omp`、`test_mpi`、`test_hybrid`、`test_global_default`が全て合格。
`.git`を持たないsource snapshotでも`make test`が合格した。
`/dev/full`検査はmacOSでSKIPのまま。今回slow/sanitizerと長い4×2科学的計算は再実行していない。
旧検証のsource/binary checksumと数値結果は、その実行時点の記録として維持する。

## 7. Stage A site-flip diagnostic pilot（2026-09-26）

**実行と整合性検査は合格。指標`p`・`d`の仮説判定はともに判定不能。**
The 12-replica pilot completed normally, but both indicators remain undetermined
because the predeclared acceptance-count gates were not met. This result does
not justify starting Stage B weighted site selection.

### 条件とprovenance

- source: `9dd1a34425140ecf04528fb3cb404263624079e4`。
- square 4×4（16 sites）、P/P、入力`t=-1.0`、`U=8`、`beta=24`、`dtau=0.0125`、`Ltr=1920`。
  エネルギー単位は`|t|=1`。相互作用は`U n_up n_down`、`mu=U/2=4`のgrand-canonical ensembleで、粒子数は固定しない。
- `global_update=site`、`global_interval=10`、固定順序のsite選択。
  各replicaで`nwarm=10000`、`nmeas=50000`、`nbin=100`、`stab=4`、
  `sweep_order=alternating`、`green_rebuild=combine`。
- `nrep=12`、replica ID 0–11、base seed `1032491301596221733`。
  `parallel=omp`、8 threads、macOS / Apple clang 16 / Accelerate / libomp。
  `global_site_diag_file=site_diag.tsv`と`replica_bin_file=bins.tsv`を併用した。
- 実行期間は2026-09-25 23:01:25–2026-09-26 00:00:14 JST。
  solverと実行wrapperの終了コードはともに0、solver walltimeは3528.27秒。stderrは計時出力のみ。
- binary SHA-256: `595c273c7e588564a86a11bdc6c1b99dc282b8f8c02b2d718a2a61e6ccddce52`。
- input SHA-256: `4f46d46bf213fcbbb8983d5afc333805954c75fe75fce3133d3e3ab85291be56`。
- diagnostic TSV SHA-256: `5cf02837a92f8dfe46db7e9c7fbb964bcd5450436b77f459705305d3c8c8c282`。
- 解析script SHA-256: `b373fa7d70384fa1f19675e0432a5812d12b8ddc6620cce5091785adbe9cca7b`。

診断は測定期間のみを集計する。1,200行（12 replica × 100行）について、replica ID・seed・
行数を検査した。`p`と`d`は各80,000試行/replica、合計各960,000試行で、replica別の試行数・受理数は
`bins.tsv`と一致した。binary・inputの実行前後checksumは不変で、保存した37ファイルのmanifest検証も通過した。

### 事前規定の解析と結果

HS場を`s_li`、副格子符号を`epsilon_i`、`m_i = sum_l s_li`、`M = sum_i epsilon_i m_i`として、提案直前の
`p_i = |m_i|/Ltr`、`d_i = -epsilon_i m_i sign(M)/Ltr`を用いる（`sign(0)=+1`）。
`d_i > 0`は多数派のstaggeredパターンと逆向きの偏極を表す。
`p`の[0,1]と`d`の[−1,1]を各50 binsに分け、端点1はbin 49へ含める。

下側・上側の四分位集合は、全replicaを合わせた試行数の25%に達する最短prefix・suffixを
bin単位で選ぶ。集合はreplica bootstrap中も固定し、重なれば判定不能とする。
`A`を試行数、`C`を受理数として、比は連続性補正を入れた
`R = ((C_top + 0.5)/(A_top + 1)) / ((C_bottom + 0.5)/(A_bottom + 1))`とする。

bootstrap前に全体と四分位集合の和集合のそれぞれで、受理30件以上かつ受理のあるreplica 8本以上を要求する。
bootstrapはreplica単位でseed 20260925・10,000 drawsを指定する。
上側または下側の試行数が0のdrawを空drawとし、全drawの5%を超えれば判定不能とする。
95%区間を得られた場合は`R >= 3`かつ区間下限`> 1`で支持、区間上限`< 2`で不支持、残りは判定不能とする。

| 指標 | 下側bins | 下側A / C | 上側bins | 上側A / C | 補正R | 95%区間 | 判定・理由 |
| --- | --- | ---: | --- | ---: | ---: | --- | --- |
| p | 0–2 | 291222 / 0 | 8–49 | 256442 / 8 | 19.30561957 | null | undetermined / insufficient_events |
| d | 0–20 | 253376 / 4 | 24–49 | 273406 / 4 | 0.92673926 | null | undetermined / insufficient_events |

両指標とも全体・四分位集合の和集合で受理8件、受理のあるreplicaは4本（ID 1・4・6・10、各2件）だった。
同じ8件の反転を両指標で集計しており、16件の独立な受理ではない。
最低件数gateを満たさず、実際のbootstrap drawは0回。比は記述値にとどまり、どちらの指標も支持・不支持とは扱わない。

四分位集合には空binも含まれる。`p`の試行はbin 18まであり、bin 17は34試行、bin 18は1試行、
bins 19–49は0試行である。受理はすべてbins 11–16にあるが、試行の末尾とは異なる。
上側をbins 8–49と表示するのは上記の固定suffix規則によるもので、空binによって件数や比は変わらない。

このpilot記録時点では、次の診断候補は同じ条件・固定順序の120 replicaであり、結果は未取得だった。後続結果は§8を参照。
このpilotから混合の十分性や重み付き選択の効果は結論せず、段階Bは保留する。

## 8. Stage A 120 replica diagnostic（2026-09-26）

**120 replicaの固定順序診断は正常終了し、事前規定の判定は`p`: 支持、`d`: 不支持となった。**
The 120-replica fixed-selection diagnostic supports `p` and rejects `d` under
its predeclared criteria. This permits planning the `p`-based Stage B candidate;
weighted-selection performance and sufficient mixing remain untested.

### 条件と検証

- source: `4a21b6415d6169410ba66a19fa6536d9cb3800f4`。§7のpilotから数値sourceは変更していない。
- square 4×4、P/P、入力`t=-1`（エネルギー単位`|t|=1`）、`U=8`、`mu=4`、
  `beta=24`、`dtau=0.0125`、`Ltr=1920`。相互作用は`U n_up n_down`、grand canonicalで粒子数は固定しない。
- 120 replica（ID 0–119）、base seed `1032491301596221733`。各replicaでwarmup 10,000、
  測定50,000 sweep、100 bins。`stab=4`、`sweep_order=alternating`、`green_rebuild=combine`、
  `global_update=site`、`global_interval=10`、固定順序のsite選択。
- 120 MPI ranks × 1 OpenMP thread、Intel 2023.2 / Intel MPI 2021.10.0 / MKL。
  solver exit 0、stderr 0 bytes、実測solver walltime 1829.66秒。
- serial・OpenMP・MPI・hybridの全suiteが合格。短い3 caseの整合性検査とserial/MPI診断のbyte一致も合格。
- 診断12,000行・測定12,000 binsについて期待120 replica、ID・seed・完全性を確認。
  各指標80,000試行/replica、合計9,600,000試行。bin別の試行数・受理数とも一致した。
- MPI binary SHA-256: `ec450cd2a9705014c3fb42c21ffd2495c1966dc300af6e94d30e7ea919f24d41`。
- input SHA-256: `9ebb75cda4402aa3429f2d1e9fb95eab890a4a1931ae575428ffda10d1529301`。
- diagnostic TSV SHA-256: `c16a0428cb6de5b16b13b6cde02763e909f1acd7572daf6dd5c4e70516e99899`。
- 解析script SHA-256: `b373fa7d70384fa1f19675e0432a5812d12b8ddc6620cce5091785adbe9cca7b`（pilotと同じ）。

### 事前規定による判定

指標の定義、四分位集合、連続性補正、最低件数、支持・不支持の閾値は§7と同じで、変更していない。
両指標とも全体・上下四分位の和集合で受理63件、受理を含む31 replicaであり、30件・8 replicaのgateを通過した。
同じ63反転を両指標で集計しており、126件の独立な受理ではない。
replica bootstrapはseed 20260925で10,000 drawsを実行し、空drawは両指標とも0だった。

| 指標 | 下側bins | 下側A / C | 上側bins | 上側A / C | 補正R | bootstrap 95%区間 | 判定 |
| --- | --- | ---: | --- | ---: | ---: | --- | --- |
| p | 0–2 | 2916577 / 0 | 8–49 | 2539525 / 63 | 145.85611882 | [99.27275081, 196.87927066] | 支持 |
| d | 0–20 | 2510384 / 43 | 24–49 | 2725621 / 20 | 0.43404955 | [0.26251312, 0.67120642] | 不支持 |

pの下側受理は0で、raw ratioはnull。上表は事前規定の連続性補正による比である。
四分位集合の空binは固定prefix/suffix規則に従って含める。
pは`R >= 3`かつ95%区間下限`> 1`、dは95%区間上限`< 2`の基準をそれぞれ満たした。
この条件でpを用いた段階Bの計画へ進む根拠が得られた。段階Bの実装・効果検証は未実施であり、
受理率の偏りから混合の十分性を結論しない。

### baselineとの照合と範囲

同じ120 seedsの診断なしbaseline（source `c422af0`）と、12,000測定binsのseed・試行数・受理数が全一致した。
`bins.tsv`全体もbyte一致し、双方のSHA-256は
`bb6fb157408ea5517c0757e62ce91a95997f6f54a802ab9004a7fd36c3d65b1e`である。
これは保存されたbin集計の一致であり、個々の提案や全Markov chainの同一性を証明するものではない。
pilot 12 replicaは今回のseed集合の部分集合なので、合算して132の独立replicaとは扱わない。

数値・全histogram・provenanceは[機械可読の検証記録](docs/validation/global-site-diag-120-2026-09-26.json)にも保存した。

## 9. Stage B polarized selection versus fixed order（2026-09-26）

**α=2で受理数は増えたが、事前規定の混合改善基準は満たさず、総合判定は`fail`となった。**
At the tested L4, U8 conditions, polarized selection increased acceptance but
did not meet the predeclared mixing-improvement criteria. The implementation
checks passed; these results do not establish sufficient equilibration or
scientific adoption of the U≥8 DQMC values. This section supersedes the
pending-performance status recorded in §8 without changing that observation.

### 条件と完全性

- source: `3215eee700b9b6359242e228e515cf83a5a53732`。fixed基準は
  `c422af0fb69cc3b008a6847f4cc6955f0057f965`、診断onの計時基準は§8のsource。
- square 4×4、P/P、入力`t=-1`、`|t|=1`、`U=8`、`mu=4`、相互作用`U n_up n_down`。
  grand canonical、粒子数は固定しない。`dtau=0.0125`、`stab=4`、
  `sweep_order=alternating`、`green_rebuild=combine`。
- P1はβ24・interval10、P2はβ24・interval100、P3はβ16・interval10。
  全て`global_update=site`、`global_site_select=polarized`、`global_site_power=2`、診断on。
  β24の時間分割数は1920、β16は1280。各120 replica、warmup10,000・測定50,000 sweep・100 bins。
- P1/P2のbase seedは`1032491301596221733`、P3は`299273251183640732`。
  対応するfixed基準と全120 seedsが整数として一致する。同一seedの組は比較の対応付けであり、
  乱数消費数の異なるfixedとpolarizedで軌道が同じことを意味しない。
- 120 MPI ranks × 1 OpenMP thread、Intel 2023.2 / Intel MPI 2021.10.0 / MKL sequential。
  3本ともexit 0、stderr 0 bytes、保存した各29 filesのstrict checksum検証が合格。
  各12,000測定bins・12,000診断行、ID 0–119、seed、試行数・受理数の和が一致した。
- binary SHA-256: `2f2ccb5e31afa0ef96b2b1baf908c040447e3b58a7b129ed369228819965a8a5`。
  凍結した解析script SHA-256: `a8689213bbf53f115178baf940de4f92ac73c3efa17bb4e0a4c30830f7e4cd42`。
  入力・bins・診断・profileのSHA、全seeds、計時値は
  [機械可読記録](docs/validation/global-site-select-2026-09-26.json)に保存した。

既存3基準と診断on基準もstrict検証した。旧sourceのprofileはCSV、新sourceは空白区切りだったため、
旧profileの区切りのみを変換した派生ファイルを解析へ渡した。全15列・全行の文字列一致と変換前後SHAを確認し、
原データと凍結解析scriptは変更していない。別実装で全6条件のsector統計とP1−fixedのpaired energy差を再計算し、解析JSONと一致した。

### 受理とsector統計

sectorは各保存binの`16 * sum_sign_Szz_0 / count > 0.5`を高側とする。
横断数は隣接bin間で側が変わった回数であり、個々の受理やbin内の反転回数ではない。
表中の矢印は同条件のfixed→polarizedを表す。

| 条件 | 試行数 | 受理数 | 受理倍率 | bin間横断 | 両側訪問replica | 全bin高側replica |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| P1: β24、interval10 | 9,600,000 | 63→367 | 5.8254 | 61→60 | 30→34 | 0→0 |
| P2: β24、interval100 | 960,000 | 16→111 | 6.9375 | 16→13 | 15→13 | 9→15 |
| P3: β16、interval10 | 9,600,000 | 3,406→12,162 | 3.5708 | 1,243→1,160 | 120→119 | 0→0 |

P1の提案で`p≥0.16`の比率は0.4258884375（fixed診断は0.2645338542）。
受理倍率は記述値であり、独立なBernoulli試行を仮定した誤差や改善の有意性は付けていない。
受理増加をsector横断の増加と同一視しない。bin内の再反転などの機構は、この集計だけでは特定できない。

### 精度と6基準

誤差はreplicaを独立単位とするdelete-one jackknifeのSE。
共通gateは`SE(E/N)≤0.0004`かつ`SE(3Szz(Q))≤0.03`。
paired差と後半−前半差はreplicaごとの差の平均を`sd/√120`で割ってzとする。
該当runが精度未達なら、各基準は先に`undetermined/precision`となる。

| run | E/N ± SE | 3Szz(Q) ± SE | 精度gate |
| --- | --- | --- | --- |
| P1 | −0.529537519 ± 0.000251750 | 3.711617785 ± 0.022117217 | pass |
| P2 | −0.528163283 ± 0.000415674 | 3.531173606 ± 0.054025854 | fail |
| P3 | −0.528473682 ± 0.000212665 | 3.748340586 ± 0.015345419 | pass |
| fixed β24、interval10 | −0.530341052 ± 0.000260986 | 3.784039760 ± 0.016867709 | pass |
| fixed β24、interval100 | −0.528619321 ± 0.000342331 | 3.616747923 ± 0.046136212 | fail |
| fixed β16、interval10 | −0.529005887 ± 0.000233899 | 3.739695395 ± 0.016138417 | pass |

| 基準 | 規定と実測 | 判定 |
| --- | --- | --- |
| 1: P1の横断・両側訪問 | 目標≥122回・≥60本に対し60回・34本 | fail |
| 2: P1/P2の全bin高側0本 | 実測0本・15本。P2精度未達を先に適用 | undetermined |
| 3: P1−P2の一致 | z(E/N)=−3.0341、z(3Szz)=3.1884。P2精度未達を先に適用 | undetermined |
| 4: P1の前後半一致 | z(E/N)=0.1255、z(3Szz)=0.5978、z(高側比率)=−0.8313、全て絶対値<3 | pass |
| 5: P1とfixed β24の非劣化 | z=2.1720、−2.4468。SE比=0.9646、1.3112（上限1.5） | pass |
| 6: P3とfixed β16の非劣化 | z=1.6670、0.3992。SE比=0.9092、0.9509（上限1.5） | pass |

基準1がfailのため総合はfail。基準2/3は観測値に懸念があっても、事前規定どおり判定不能であり、
事後的にfailへ付け替えていない。基準5/6の合格は規定内の整合性であり、十分な混合の証明ではない。

### 計時・補助ED比較・範囲

`profile`の`all beta_total` walltimeはP1=1813.690 s、P2=1362.329 s、P3=1224.205 s。
P1 / fixed診断onのwall比は0.997996、測定中`dqmc_global`のthread-summed time比は1.013513。
診断なしの旧fixedに対してはそれぞれ1.009865、1.018699。
profile wallはMPI起動などを含む外部計時とは異なる。これらは各1回の実測であり、実行時の変動を含むため純粋なアルゴリズム追加コストとは断定しない。

合否に使わないED基底状態値`E/N=−0.5293046883872968`、`3Szz(Q)=3.775202868960937`との差は、
P1で−0.000232831・−0.063585084、P2で+0.001141405・−0.244029263、
P3で+0.000831007・−0.026862283。統計SEは上表のとおりで、有限温度差とTrotter偏差の寄与は今回分離できていない。
β・dtau外挿なしに、EDとの差を統計誤差だけで説明したり科学的採用へ進めたりしない。

このα=2・L4・U8・β16/24・指定run長で、受理率の増加は確認したが、規定した混合改善は確認できなかった。
他のα、長さ、温度、更新法への一般化はしない。科学的採用は引き続き保留する。

## 10. Δτ-ladder parallel tempering（2026-09-27〜）

**cross-weight検査・厳密列挙χ²・統合ladder driverの厳密列挙回帰・3215eeeとのbyte同一性・
serial/OpenMP/MPI/hybrid一致・MPI失敗経路・失敗messageの区別・L6 chain U=4でのPT対独立chain
検証は、いずれも合格。`field_init=uniform`の倍精度上限を実測で記録した。**
This section records the correctness validation of the new `tempering=dtau_ladder`
parallel-tempering (PT) machinery: an independent cross-weight check, an
exact-enumeration stationarity/sampling test (with a negative control that is
required to fail), an exact-enumeration regression of the integrated ladder
driver under the production option sets, byte-identity of every
`tempering=none`/unspecified run against the pre-PT baseline, agreement across
`serial`/`omp`/`mpi`/`hybrid`, MPI failure-path handling, the distinction
between slot breakdowns and exchange failures in the error messages, and a
finite-size (`L=6` chain, `U=4`) check of PT slots against independent non-PT
chains at the same `(beta, dtau)`. It also records the measured
double-precision bound of `field_init=uniform`. This is a
correctness check of the implementation, not a scientific validation of any
physical result computed with PT, and it does not show that PT reduces or
removes Trotter error — every slot keeps its own `dtau_k`.

### cross-weightの直接行列式検査

`dqmc_log_weight_of`（他slotの配置での対数weight）と`dqmc_replace_field`
（配置の入替とGreen関数の再構築）を`src/dqmc.c`に追加し、独立実装との一致を
検査した。比較対象は、`green_build_B`/`green_logdet_full`と経路を共有しない
（`green_build_B`出力とnaive `B`構成の1点anchor checkを除く）、`expK`・`s`・
`lambda`から直接組み立てたnaive Gaussian消去法によるlog-determinantである。
交換比の恒等式`logR = logW_a(C_b) + logW_b(C_a) - logW_a(C_a) - logW_b(C_b)`と
その反対称性、`NULL`・失敗状態guardを確認した。`dqmc_global_site_pass`末尾の
再構築処理を専用関数として抽出した変更は、既存の条件・呼び出し順を保ったままの
機械的な抽出であり、`make test`の既存`test_dqmc_global*`/`test_global_output`が
回帰なしを確認した（rc=0、`ALL TESTS PASSED`）。

### 厳密列挙χ²（sampling p値）と負の対照

3-slot ladder（`TemperingLadder`、`tempering_ladder_try_pair`/`_round`）を、
既知の定常分布を持つ厳密列挙oracleでp値検定した
（`tests/test_tempering_sampling.c`。2-site chain、`Ltr=4`で256配置。独立な
ladder 4000本をそれぞれ150 cycle（1 cycle = 各slotの1 sweepと交換round 1回）
進め、最終配置をslotごとに1 sampleとしてχ²検定する。合格基準は各slotで
`p >= 1e-3`）。2026-09-28訂正: 以前の版はこの規模を「200 trial」と記していたが、
200は`tests/test_tempering_ladder.c`で受理と棄却の両方を得るまで試行を繰り返す
上限であり、このsamplingテストの規模ではない。

| slot | p値 |
| ---: | ---: |
| 0 | 0.3503 |
| 1 | 0.9012 |
| 2 | 0.1786 |

3 slotとも`p >= 1e-3`で合格（rc=0）。

負の対照として、交換比の式から`- logw_b_cb`項を落とした変異を、実際のworktree・
binaryとは独立なscratch treeにのみ適用し、同じsamplingテストを実行した。

| slot | p値（負の対照） |
| ---: | ---: |
| 0 | 1.9e-6 |
| 1 | 7.1e-62 |
| 2 | 3.4e-123 |

3 slotとも`p < 1e-3`で棄却され、`rc=1`。実際のworktreeの交換比の実装は
変更されておらず、正しい式（`... - logw_a_ca - logw_b_cb`を含む）のままである
ことをテスト後に確認した。この負の対照は、samplingテストが誤った交換比の式を
検出できることを示す。

### 統合ladder driverの厳密列挙回帰（`make test_slow`、2026-09-28）

上のsamplingテストは`TemperingLadder`を直接駆動する。本番の選択肢の組を通した
統合driver `dqmc_run_ladder`（slotごとのsweep、大域更新、交換round、測定、carried
stackの再利用）は、`tests/test_tempering_driver_exact_slow.c`が検査する。系は開放端
2-site chain、`U=4`、3 slot（`beta=0.4, 0.8, 1.2`）、`tempering_ltr=4`
（`dtau_k=beta_k/4`）で、slotごとの配置は`2^8=256`通り。各slotの`E`と`D`の
有限`dtau`厳密値は、`dqmc_log_weight`（直接行列式との一致は
`test_dqmc_tempering_weight`で検査済み）のweightと`l=0`のGreen関数から全列挙で
求める。比較する選択肢の組は次の3つ（共通: `nwarm=200`、`nmeas=3000`、`nbin=3`、
`stab=2`）。

| case | sweep | 大域更新 | `tempering_interval` |
| --- | --- | --- | ---: |
| (a) | forward | なし | 1 |
| (b) | alternating | `global_update=site`、3 sweepごと（`stab=2 < Ltr`なので、棄却された交換の後はcarried stackを再利用し、受理された交換と大域passの後は無効化して再構築する） | 1 |
| (c) | alternating | `global_site_select=polarized`、2 sweepごと | 3 |

各caseで固定seed（`20260928`）から独立ladder 800本を走らせ、ladderごとの平均の
平均を、ladder間のばらつきから求めたSE（自由度799）で厳密値と比較する。判定は
18比較（3 case × 3 slot × `E`・`D`）すべてで`|z| < 4`。正しい実装が1比較で
これを超える確率は約6.3e-5（正規分布の裾。自由度799ではStudent-t補正は無視できる）
なので、18比較全体の誤警報確率はBonferroni上界で約1.2e-3である。あわせて、
測定区間のpairごとの交換試行数が交換roundの規則（`tempering_interval` sweepごと、
round番号の偶奇でpairを交互に選ぶ）から計算した値と全ladderで一致すること、交換が
受理と棄却の両方を含むこと、大域更新が(b)(c)で実行され(a)で実行されないことも
検査する。

結果（rc=0、`ALL SLOW TESTS PASSED`）: 18比較すべて`|z| < 4`、最大`|z| = 1.89`
（(b) slot 2の`D`）。交換受理率は3 caseとも0.750。

| case | slot | beta | E（SE） | 厳密E | z(E) | D（SE） | 厳密D | z(D) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| (a) | 0 | 0.4 | 0.910160(234) | 0.910131 | +0.12 | 0.158994(35) | 0.158970 | +0.70 |
| (a) | 1 | 0.8 | 0.220023(264) | 0.219844 | +0.68 | 0.101780(41) | 0.101749 | +0.75 |
| (a) | 2 | 1.2 | -0.204557(426) | -0.204910 | +0.83 | 0.070690(40) | 0.070710 | -0.49 |
| (b) | 0 | 0.4 | 0.910177(238) | 0.910131 | +0.19 | 0.158961(36) | 0.158970 | -0.25 |
| (b) | 1 | 0.8 | 0.219810(256) | 0.219844 | -0.13 | 0.101727(39) | 0.101749 | -0.58 |
| (b) | 2 | 1.2 | -0.204709(414) | -0.204910 | +0.49 | 0.070789(41) | 0.070710 | +1.89 |
| (c) | 0 | 0.4 | 0.910024(254) | 0.910131 | -0.42 | 0.158974(38) | 0.158970 | +0.12 |
| (c) | 1 | 0.8 | 0.220187(292) | 0.219844 | +1.17 | 0.101758(41) | 0.101749 | +0.21 |
| (c) | 2 | 1.2 | -0.204916(437) | -0.204910 | -0.01 | 0.070752(45) | 0.070710 | +0.95 |

括弧内は末尾桁のSE（例: `0.910160(234)`は`0.910160 ± 0.000234`）。所要CPU時間は
case (a)・(b)・(c)でそれぞれ50.2・66.2・60.7秒（負荷の高い共有machineで計測。
`make test_slow`全体は5分33秒）。この検査が対象とするのは小さな系での統合driverの
定常分布であり、大きな系での混合の速さや、PTによるTrotter誤差の変化は対象外である。

### 3215eeeとのbyte同一性（`tempering`未指定・`tempering=none`）

`tests/test_tempering_disabled_unchanged.sh`（`make test_tempering_default`）は、
このPT開発の起点commit `3215eee700b9b6359242e228e515cf83a5a53732`を同じ
toolchainで別途buildし、`chain`・`square`の2 fixture × 12 variantで出力を
比較する。variant一覧（`omitted`/`none`/`field_random`/`global`/`select`/
`bins`/`alternating`/`diag`/`drift`/`udvscale`/`centered`/`profile`、合計
24比較）は`tests/test_tempering_disabled_unchanged.sh`を正本とする。`profile`
variantだけは実行時間の列を除いた空白区切りの列（schema・呼出し回数）を比較し、
他の全variantは生成された全fileを完全byte一致で比較する。24比較すべてが合格した。

**2026-09-27の事実記録**: 旧`make test_global_default`（比較先commit
`463dc75`）は、このrepositoryでは実行できない。`463dc75`はSAI-QMCの
object storeに存在せず、この欠落は今回の変更と無関係な既存の状態である
（`463dc75`が存在しないrepositoryでは、tempering実装の前後を問わず同じ理由で
常に失敗する）。本変更のbyte同一性保証は、新設した`3215eee`ベースの
`make test_tempering_default`が担う。

### serial/OpenMP/MPI/hybrid一致とMPI失敗経路

`tests/test_tempering_parallel.sh`は`nrep=4`のPT runを`serial`・`omp`
（2 threads）・`mpi`（`-n 2`）・`hybrid`（`-n 2` × 2 threads）の4形態で実行し、
`bins.tsv`全体、`pt.tsv`（`cost`行を除く）、およびstdoutの`#`で始まらない行を
比較して一致を確認した（`solver_elapsed_seconds`行と先頭行は形態ごとに異なる
ため`#`行を除外して比較する）。mpiの`solver_elapsed_seconds`行が`nranks=2`で
終わることも確認した。

`tests/test_tempering_mpi_failure.sh`は4ケースで異常系を検査した。

| case | 条件 | 結果 |
| --- | --- | --- |
| 1 | `-n 2`、`nrep=3`、rank 1のみladder 2を失敗させるhook | 両rank exit 1、rank 0のstderrがladder 2を明示、`pt.tsv`のladder 2行が`failed=1`、他のladderは`0` |
| 2 | `-n 4`、`nrep=2`（rank 2・3はladderを持たない） | 全rank exit 0、`pair`行20行 |
| 3 | `-n 2`、`tempering_file=no_such_dir/pt.tsv`（開けないpath） | 両rank exit 1 |
| 4 | `-n 2`、`nrep=3`、`tempering_file`省略 | 両rank exit 0、`pt.tsv`は生成されない |

全ケースで各rankの終了codeを直接回収して判定した。`make test`・`test_omp`・
`test_mpi`・`test_hybrid`はいずれもrc=0で`ALL ... TESTS PASSED`。

### slot破綻と交換失敗の報告の区別（2026-09-28）

交換roundの前に、ladder driverは全slotの状態を確認する。slot自身のsweepまたは
大域passですでに失敗したslotは、そのslotの`dqmc warmup numerical breakdown`・
`dqmc measurement numerical breakdown`行（`slot=`、`ladder=`、`beta`、`dtau`、
`sweep_count`、`failure_reason`を含む）として報告し、roundは試行しない。全slotが
健全なままround自体が失敗した場合（weight評価、非有限の`log R`、受理後の再構築）
だけを`tempering exchange failed`として報告する。以前の実装では、roundの事前
status検査がslotの破綻を`tempering exchange failed (... slot_status=1,0)`として
報告し、破綻したslotがそのroundのpairに含まれないときは1 sweep遅れて報告していた。
成功するrunの出力は変わらない（`make test_tempering_default`はOK）。

`tests/test_tempering_failure_messages.sh`（`make test`に含む）は、hook付きbuild
（`AFQMC_TEST_HOOKS`）で失敗を注入してstderrを検査する（3 slot、`tempering_ltr=20`、
`tempering_interval=1`、`nwarm=4`、`nmeas=8`、`nbin=2`、`global_update=site`、
`global_interval=1`）。修正前のbinaryでは測定・warmupの2 caseが不合格（下表の
「修正前」）、修正後は3 caseとも合格した。

| case | 注入 | 修正後のmessage（合格条件） | 修正前のmessage |
| --- | --- | --- | --- |
| 測定 | slot 1の大域passをsweep 6（測定の2 sweep目）で失敗させる（既存の`AFQMC_TEST_GLOBAL_FAIL_AT=6`・`AFQMC_TEST_GLOBAL_FAIL_BETA=1`）。round 5はpair (1,2)を試行する | `dqmc measurement numerical breakdown (slot=1 ladder=0 beta_index=1 ... sweep_count=6 bin=0 meas=1 status=1 ...)` | `tempering exchange failed (ladder=0 pair=1 round=5 sweep=6 slot_status=1,0)` |
| warmup | sweep 2の後にslot 0をfailed状態にする（`AFQMC_TEST_TEMPERING_SLOT_FAIL_AT=2`・`_SLOT=0`）。round 1はslot 0を含まない | `dqmc warmup numerical breakdown (slot=0 ladder=0 beta_index=0 ... sweep_count=2 status=1 ...)` | sweep 3で`tempering exchange failed (ladder=0 pair=0 round=2 sweep=3 slot_status=1,0)` |
| 交換 | 全slotを健全に保ったまま、sweep 3後のroundのlog-det評価を失敗させる（`AFQMC_TEST_TEMPERING_EXCHANGE_FAIL_AT=3`） | `tempering exchange failed (ladder=0 pair=0 round=2 sweep=3 slot_status=0,0)`、breakdown行なし | 同じ |

3 caseとも、非zero終了、`tempering ladder 0 failed`、スカラー出力がheader行のみ、
`replica_bin_file`が空、`tempering_file`の`ladder`行が`failed=1`であることも
検査する（1本のladderの失敗でrun全体が失敗し、`tempering_file`だけが完全に
書かれる）。production binaryはこれらのhookを無視し、hookを設定しても
`bins.tsv`、`tempering_file`（`cost`行を除く）、stdout（`solver_elapsed_seconds`
行を除く）、stderrは変わらない。

### L6 chain、U=4: PTスロット対独立chainの検証

periodic `Lx=6`、`U=4`、`mu=U/2=2`（半充填）のHubbard chainで、PT
（`tempering_ltr=200`、4 slot、`beta_list=4,5,6.666666666666667,10`、
`dtau_k=beta_k/200`）の各slotを、同じ`(beta_k,dtau_k)`を`dtau`で明示した
独立（非PT）chainと比較した。両方とも`nrep=16`、`nwarm=2000`、`nmeas=50000`、
`nbin=100`。`E/N`、`D`、`M^2=N*Szz(q=0)`、`Szz(Q)`（`Q`はstaggered/AF波数）の
4観測量について、16本のreplicaにわたる平均・SEを求め、
`z=(PT平均-独立平均)/sqrt(PT_SE^2+独立_SE^2)`で比較した。

| slot | beta | T | z(E/N) | z(D) | z(M²) | z(Szz(Q)) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 4 | 0.25 | -0.43 | 0.92 | 0.87 | 0.66 |
| 1 | 5 | 0.20 | -0.54 | 0.75 | -0.87 | -0.11 |
| 2 | 6.666667 | 0.15 | 1.81 | -0.42 | 0.70 | -0.04 |
| 3 | 10 | 0.10 | 1.12 | -0.68 | -0.14 | 0.33 |

16比較すべて`|z| < 3`（最大`|z| = 1.808`、slot 2の`E/N`）。詳細な平均・SE・
生データ・再現手順は[data/tempering_L6_U4_ed_20260927/README.md](data/tempering_L6_U4_ed_20260927/README.md)
に記録する。

参考として、`E/N`と`D`について、PTおよび独立chainとgrand-canonical EDとの
差を、既存の3点`dtau`外挿表（別のbeta格子から補間した`slope(T)*dtau_k^2`の
予測）と比較した。PTと独立chainのED差がともに予測から3 SE以内に収まった行は
8/8（4 slot × `E/N`・`D`の2観測量）。**これはTrotter誤差の起源を証明する
ものではなく、別のbeta格子から補間した3点勾配を使った参考の整合性checkであり、
この run自身の`dtau -> 0`外挿でもない。**

上記の`|z| < 3`という基準は、各行が自分自身の統計誤差の範囲内でPTと独立chainが
整合することを示すものであり、両者が同じ精度に達したことを意味しない
（`PT SE`と独立`SE`は行によって最大で2倍近く異なる）。ED差は参考情報であり、
Trotter誤差への自動的な帰属ではない。この検証全体は実装の正しさについての
限定的な確認であり、ここで到達した統計量を超えた一般的な証明ではない。

### `field_init=uniform`の倍精度上限（2026-09-28）

半充填（`mu=U/2`）では`expK`の`+dtau*mu`と`expv`の`-dtau*U/2`が相殺し、
上向きスピンの`B_l = exp(-dtau*T) exp(lambda*s_l)`となる（`T`はホッピング行列）。
全`+1`の場では`exp(lambda*s_l)`が単位行列の定数倍なので、積は
`B_{L-1}...B_0 = exp(Ltr*lambda) exp(-beta*T)`で、最大スケールは
`exp(Ltr*lambda + beta*w)`（`w`は`-T`の最大固有値。二部格子では`T`の最大固有値に
等しく、周期境界4x4正方格子で`4|t|`）。この全`+1`の場が最大スケールの配置であり、
指数が倍精度の上限`ln(DBL_MAX) = 709.78`付近に達すると、`dqmc_init`の最初の
Green関数構築で`udv_lmul_work non-finite matrix at stage=qr_raw ... value=-inf`、
続いて`dqmc_init failed`が出て、sweepを1回も行わずに非zeroで終了する。コードに
事前の棄却規則は加えていない（失敗は初期化時に即座かつ明示的に起こる）。

周期境界4x4正方格子、`t=-1`、`U=8`、`dtau=0.0125`（`lambda=0.318869`）、
非PT（`nwarm=0`、`nmeas=2`または`4`、`sweep_order=alternating`、`stab=4`、
`global_update=site`）での実測:

| beta | Ltr | 指数`Ltr*lambda+4*beta` | `ln(DBL_MAX)`との差 | 結果 |
| ---: | ---: | ---: | ---: | --- |
| 24 | 1920 | 708.23 | -1.55 | 正常に開始・終了 |
| 24.05 | 1924 | 709.70 | -0.08 | 正常に開始・終了 |
| 24.0625 | 1925 | 710.07 | +0.29 | 正常に開始・終了 |
| 24.075 | 1926 | 710.44 | +0.66 | 正常に開始・終了 |
| 24.0875 | 1927 | 710.81 | +1.03 | 正常に開始・終了 |
| 24.1 | 1928 | 711.18 | +1.40 | 初期化で失敗 |
| 24.5 | 1960 | 722.98 | +13.20 | 初期化で失敗 |
| 25 | 2000 | 737.74 | +27.96 | 初期化で失敗 |
| 26 | 2080 | 767.25 | +57.47 | 初期化で失敗 |

`Ltr=1932`〜`1956`（4刻み）もすべて初期化で失敗した。観測された境界は
`Ltr=1927`と`1928`の間で、式の指数が`ln(DBL_MAX)`を越える点（`Ltr≈1924.2`）より
1.0〜1.4 e-fold上にある。失敗するのは行列要素の段階なので、境界は式から
lattice依存のO(1) e-foldずれうる。PTでも同じで、`beta_list=20,24.5`、
`tempering_ltr=1960`では最も低温のslot（`dtau=0.0125`）の初期化が同じmessageで
失敗し、`tempering ladder 0 failed`で終了した（0.05秒）。一方、
`beta_list=16,17.6,19.2,20.8,22.4,24`、`tempering_ltr=1920`（最低温slotは
`beta=24`、`dtau=0.0125`、指数708.23）では、6 slotとも初期化され、10 sweepの
短い実行が正常に終了した。この設定の余裕は`ln(DBL_MAX)`まで約1.6 e-foldしかない。
`field_init=random`の初期配置は、各siteの時間方向の和がほぼ0なので、このスケール
より十分小さい。

### CHECK_CLOSEの非有限値の拒否（2026-09-28）

test helperの`CHECK_CLOSE(a, b, tol)`は`fabs(a - b) > tol`のときだけ失敗と
判定していた。NaNとの比較は常にfalseであり、`INFINITY - INFINITY`もNaNに
なるため、`CHECK_CLOSE(NAN, 1.0, 1e-12)`や
`CHECK_CLOSE(INFINITY, INFINITY, 1e-12)`は黙って合格していた。
`tests/test_util.h`は本repositoryの最初のcommit（"Initial import of
SAI-QMC 0.1"）以来この点を変更されていなかった。

`a`、`b`、`tol`がすべて有限かつ`tol >= 0`であることを要求する
`static inline int test_close(double a, double b, double tol)`を追加し、
`CHECK_CLOSE`はこれを使って判定し、非有限な入力を拒否した場合は専用の
messageを出すようにした。負の対照（NaN・Infの各組み合わせ、負のtolerance）
を検査する`tests/test_check_close.c`を新設し、`make test`に組み込んだ。
test helperのみの変更であり、`src/`以下のproduction codeは変更していない。

この変更を含むcommitで`make test`、`OMPI_CC=cc OMP_NUM_THREADS=2 make
test_omp test_mpi test_hybrid`、`make test_slow`、
`make test_tempering_default`を再実行し、すべて既存の合格条件（`ALL ...
PASSED`、および`test_tempering_default`は3215eeeとのbyte同一性）を満たして
合格した。非有限値の検出によって新たに失敗した既存testはなかった。


## 11. 条件付き局所D・同期K/Eの比較用測定（2026-09-30）

`conditional_measure=1`は、各局所更新の直前に同じsite/time cutのHS二状態を
条件付き平均する。DとKを同時測定し、`Ehub=K+U*N*D`とする。
既定値0、従来のscalar・spin・bin先頭20列を保持し、測定時の乱数消費や更新を変えない。
仕様と解析手順は[利用説明](docs/conditional-measurements.md)を参照。

### 独立参照と実装検査

- 2サイト・4sliceの全256 HS配置について、独立した両spin直接行列積で
  反転前後を作り、明示的な重み付き平均と新しいD/Kの式を照合した。
  U=0も含め、delayed Greenの読み出しも一致する。
- [独立Fock参照](data/conditional_measurements/README.md)は
  2サイトのΔτ0.125/0.025と4サイトのΔτ0.25を全列挙し、全Fock空間の
  同じ有限Δτ transfer matrixと比較した。D・Eの期待値が一致し、
  局所エネルギー条件付き式と明示反転平均との差は最大2.5e-15以内。
- 固定β0.5とPT β0.25/0.375/0.5（U8、2サイト、Ltr4）、各64独立系列、
  warmup 200・測定3000で統合driverを検査した。forward/globalなしと
  alternating/global interval 3のD・E全16比較が規定の5SE以内、最大|z|=2.173。
- field・RNG・Green・受理数の不変性、受理したPT交換でも測定蓄積がslotに残ること、
  MPI転送layout・出力schema・欠落bin/非有限値の拒否を検査した。
  変更前binaryとの既定出力比較も一致した。

小系の定常期待値の確認を、低温4×4の平衡化やSEのcoverage保証へ拡張しない。
条件付きDの境界は固定Δτでのみ有限であり、K/Eやspinの裾にも同じ境界が
成立するとは主張しない。全binを保持して独立系列・測定費用とともに評価する。

`make test`、`make test_omp`、`OMPI_CC=clang make test_mpi`、
`OMPI_CC=clang make test_hybrid`、`make test_slow`はすべて合格。
MPI/hybridはserial/OMPとcompilerを揃えた。既存のbyte一致基準を緩和していない。
新規のcross-mode検査だけは異なるcompiler間も想定し、整数は厳密一致、
浮動小数点は絶対・相対1e-12の許容差で比較する。


### 独立8系列の最初の低温比較

4×4 PBC・U8・β16・Δτ0.025で、base seed 2026093001の8系列、
各warmup 10000・測定10000、20 binを全て保持した。条件付きDの系列間SEは
通常の1/3.508、同期K/EによるE/NのSEは1/3.324となった。paired平均差は
Dで−0.91 SE、E/Nで−0.36 SE。入力・全bin・平均値・解析commandは
[data/conditional_measurements](data/conditional_measurements/README.md)を参照。
別Δτ・PT・費用比較はこの記録時点で実行中であり、費用当たりの改善や
低温の平衡化をこの結果だけから確定しない。


### 追加の独立8系列: Δτ0.0125

同じ4×4 PBC・U8・β16で、base seed 2026093002、warmup 10000・測定10000、
8系列・各20 binを保持した。通常SE/条件付きSEはDで5.024、E/Nで2.667。
paired平均差はDで−1.50 SE、E/Nで+0.70 SEだった。
各Δτの入力・bin・解析値を上記data directoryに保存した。
PT各slotと測定費用の比較は継続中であり、この2条件だけから本計算の採用を決めない。
