# Supplementary development records

---
date: 2026-07-23
datetime: 2026-07-23 16:56 JST
model: OpenAI GPT-5 Codex
summary: |
  AF_QMC は private のまま維持し、公開コードを別 repository に分離する前提、
  および公開ソフトウェアの命名検討を記録した。
  VeriDQMC は採用せず、KasaneQMC を含め名称は時間をかけて再検討する。
---

## 2026-07-23: 公開コードの命名方針を記録

- `docs/2026-07-23-public-code-naming-note.md` を作成した。
- `AF_QMC` は今後も private とし、コードと公開可能な文書を別の public
  repository に抽出する前提を明記した。
- `VeriDQMC` は名称として響かないため採用せず、`KasaneQMC` は保留とした。
  AI を実行時手法と誤解させる名称や、一般的すぎる名称を避ける条件も整理した。
