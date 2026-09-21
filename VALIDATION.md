---
date: 2026-09-21
datetime: 2026-09-21 23:18 JST
model: OpenAI GPT-6 (Codex; revision), Codex GPT-5 (original)
summary: |
  半充填ハバード模型の E(T) を grand-canonical ED/TPQ と比較する検証手順。
  アンサンブル整合・dtau→0 外挿・規約変換・符号/粒子数チェック・2D の ED サイズ制約をまとめる。
  Global HS update の実装検査、4×2 clusterの初回84 runと追加検証を記録した。
  追加測定後に科学的受入と既定頻度100の判定を通過。beta16の微小なSz2の厳密値との一致は未確定。
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
追加検証は、初回判定を踏まえて明文化した
[延長手順](docs/superpowers/plans/2026-09-21-global-hs-update-implementation.md#validation-extension-protocol-2026-09-21-2129-jst)
と、各段階の実行前に固定したmanifestに従って完了した。

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
長寿命状態は0/96、Szzの再block未安定は0、18個の外挿判定と旧beta4比較もすべてpass。
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
