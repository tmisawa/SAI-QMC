---
date: 2026-09-08
datetime: 2026-09-08 14:24 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Algorithmic and validation references for SAI-QMC.
  Provides bibliographic details for the implementation and external validation.
---

# References

The main implementation reference was **Yuichi Otsuka's doctoral thesis,
Appendix A**. The thesis PDF, OCR, and reconstructed TeX are not included in
the distribution.

1. Yuichi Otsuka, *格子上の電子系における乱れ及び相互作用の効果*
   (Effects of disorder and interactions in electron systems on lattices;
   in Japanese), doctoral thesis, Department of Applied Physics,
   University of Tokyo, March 2002. Appendix A.
2. R. Blankenbecler, D. J. Scalapino, and R. L. Sugar,
   “Monte Carlo calculations of coupled boson-fermion systems. I,”
   *Physical Review D* **24**, 2278–2286 (1981).
   [DOI: 10.1103/PhysRevD.24.2278](https://doi.org/10.1103/PhysRevD.24.2278).
3. J. E. Hirsch, “Discrete Hubbard-Stratonovich transformation for fermion
   lattice models,” *Physical Review B* **28**, 4059–4061 (1983).
   [DOI: 10.1103/PhysRevB.28.4059](https://doi.org/10.1103/PhysRevB.28.4059).
4. M. Kawamura, K. Yoshimi, T. Misawa, Y. Yamaji, S. Todo, and N. Kawashima,
   “Quantum lattice model solver HΦ,” *Computer Physics Communications*
   **217**, 180–192 (2017).
   [DOI: 10.1016/j.cpc.2017.04.006](https://doi.org/10.1016/j.cpc.2017.04.006).
   HΦ was used for external validation described in the historical record;
   its source code is not part of SAI-QMC.

The random-number implementation's authors and permissions are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Software citation metadata
are in [CITATION.cff](CITATION.cff).
