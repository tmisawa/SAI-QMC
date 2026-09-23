---
date: 2026-07-02
datetime: 2026-07-02 19:26 JST
model: Claude Fable 5
status: note
topic: PH 対称性トリック導入後もスピン相関は計算できるか
summary: |
  「片方のスピンセクターだけ更新するとスピン相関が計算できないのでは」という
  疑問への回答メモ。結論は「計算できる」。PH トリックは G↓ を捨てるのではなく、
  半充填・二部格子では各補助場配置ごとに G↓ が G↑ から厳密な恒等式で構成できる
  ことを使って更新計算だけを省くもの。推定量は two-spin コードと値・分散とも
  同一で統計的損失ゼロ。縦・横スピン相関の具体式と実装上の注意
  （測定直前に G↓ を写像で作る方式の推奨、検証方法、適用限界）をまとめる。
---

# メモ: PH 対称性トリックとスピン相関測定の両立

## 疑問

半充填の粒子正孔（PH）対称性で down スピンの計算を省く高速化
（`docs/2026-07-02-performance-analysis.md` §3(4)、期待効果 2×）を入れると、
$G_\downarrow$ が無くなるためスピン相関 $\langle S_i S_j\rangle$ が
計算できなくなるのではないか？

## 結論

**計算できる。** PH トリックは down スピンの情報を「捨てる」のではなく、
各補助場配置ごとに $G_\downarrow$ が $G_\uparrow$ から**厳密に構成できる**
（決定論的な恒等式がある）ことを使って、**Monte Carlo 更新の計算だけを省く**もの。
測定に必要な $G_\downarrow$ の要素は恒等式で作ればよく、統計的損失はゼロ。

## 根拠

DQMC では固定した補助場配置 $\{s\}$ のもとで 2 つのスピンセクターは独立で、
全観測量は $G_\uparrow(s)$, $G_\downarrow(s)$ の Wick 縮約で書ける。
重要なのは、**two-spin の現行コードでも $G_\downarrow(s)$ は場が決まれば
決定論的**である（独立にサンプリングしていない）こと。
半充填・二部格子・スピンチャネル HS（$\lambda\sigma s$ 型）では

$$
[G_\downarrow(s)]_{ij} \;=\; \delta_{ij} - \varepsilon_i \varepsilon_j\,
[G_\uparrow(s)]_{ji},
\qquad \varepsilon_i = \pm 1\ (\text{副格子パリティ})
$$

が各配置で厳密に成り立つ。測定時にこの写像（$O(n^2)$、sweep の
$O(n^3 L_\tau)$ に対し無視できるコスト）で $G_\downarrow$ を作れば、
**推定量は two-spin コードと値も分散も同一**。

## スピン相関の具体式

規約: $g_{ij} = \langle c_i c_j^\dagger \rangle$,
$\langle c_i^\dagger c_j \rangle = \delta_{ij} - g_{ji}$（プロジェクト標準）。

縦成分
$\langle S^z_i S^z_j \rangle = \tfrac14 \langle
(n_{i\uparrow}-n_{i\downarrow})(n_{j\uparrow}-n_{j\downarrow})\rangle$
に必要な要素:

- 同一スピン（セクター内 Wick 縮約）:
$$
\langle n_{i\sigma} n_{j\sigma}\rangle_s
= (1-g^\sigma_{ii})(1-g^\sigma_{jj})
+ (\delta_{ij}-g^\sigma_{ji})\, g^\sigma_{ij}
$$
- 異スピン（配置ごとにはセクター独立なので因子化。相関は MC 平均で発達）:
$$
\langle n_{i\uparrow} n_{j\downarrow}\rangle_s
= (1-g^\uparrow_{ii})(1-g^\downarrow_{jj})
$$

横成分:
$$
\langle S^+_i S^-_j \rangle_s = (\delta_{ij} - g^\uparrow_{ji})\, g^\downarrow_{ij}
$$

いずれも $g^\downarrow$ を上の恒等式で置換すれば $G_\uparrow$ だけから評価できる。
非等時間相関も同様に写像できる。

整合性チェックの例: $\langle n_{i\downarrow}\rangle_s = 1 - g^\downarrow_{ii}
= g^\uparrow_{ii}$ より、配置ごとに
$\langle n_{i\uparrow}+n_{i\downarrow}\rangle_s = 1$（半充填が厳密に出る）。

## 実装上の注意

1. **測定コードは2通り**:
   - (a) 測定直前に $G_\downarrow$ を写像で構成し、既存の
     `measure_sample(g_up, g_dn)` にそのまま渡す — 変更最小・検証が楽。**推奨**。
   - (b) 観測量を $G_\uparrow$ だけで書き直す — コピー1枚分速いが式の書き換えが必要。
2. **検証**: 副格子パリティ $\varepsilon_i$ の符号規約と、$\lambda\sigma s$ 型
   HS における写像の正確な形（$s$ の符号の扱い）は大塚論文の規約に合わせて
   導出・検証する。two-spin 実装（`green_from_scratch` 経路）を温存してあるので、
   **同一場配置での「$G_\downarrow$ 直接計算 vs 写像」を突き合わせる単体テスト**が
   書ける（UDV スタック検証と同じパターン）。PH 計画の「検証ラダー通過を採用条件」
   に含める。
3. **適用限界**: 恒等式が成り立つのは半充填・二部格子・この HS 形のみ。
   ドープ・次近接ホッピング（フラストレーション）を将来入れる場合は
   two-spin 経路に戻せる設計にしておく（既存計画のスコープ注記と整合）。
4. 将来 $S^{zz}(q)$ 等の相関測定を追加する予定があるなら、PH 実装時に
   (a) 方式にしておくと測定コードがスピン対称性の仮定から自由になり安全。

## 関連文書

- 高速化提案の全体: `docs/2026-07-02-performance-analysis.md` §3(4)
- 次の性能作業の優先順位: `docs/reviews/2026-07-02-delayed-update-next-steps-review.md`
  （PH 対称性が最大レバー 2× と結論）
- コードリファレンス: `docs/2026-07-02-afqmc-code-reference.html` §7（測定量の規約）
