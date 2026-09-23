# Hackathon Kickoff — Finite-Temperature Auxiliary-Field QMC for the Hubbard Model

> Target format: **~2 PowerPoint slides**. Content below is grouped per slide.
> Layout instructions for building the pptx are given in each slide's "Layout" block.

---

## Slide 1 — Starting Point & Preparation

### Title
**Finite-Temperature Auxiliary-Field QMC for the Hubbard Model**
*A 2-day hackathon: build a determinant (BSS) QMC code in C and validate it against ED / TPQ*

### Where we start from
- **Reference, already verified.** We use Appendix A of Otsuka's PhD thesis (*"Effects of disorder and interaction in lattice electron systems"*) as the implementation reference. The scanned PDF has been OCR'd to Markdown/LaTeX, and **every QMC equation (A.1–A.135) has been cross-checked**, including a Mathematica check of the core identities.
- **Verified building blocks** ready to implement directly:
  - Discrete Hubbard–Stratonovich transform, `cosh λ = exp(Δτ U / 2)` (A.10/A.11)
  - Trace = determinant: `Tr_F ∏ exp(−c†A_l c) = det(I + ∏ exp(−A_l))` (A.35)
  - Equal-time Green's function `g = (I + B_L⋯B_1)⁻¹` (A.53/A.95)
  - Metropolis weight ratio (A.99) and rank-1 (Sherman–Morrison) update (A.107) — residual ~1e-15
  - Imaginary-time wrapping (A.108)
- **Known pitfalls captured up front:** Green's-function convention `g = ⟨c c†⟩`; half-filling at `μ = U/2`; low-temperature numerical stabilization (UDV/QR) is mandatory; a sign-typo in the original (A.103) was found and corrected.
- **Design & plan written and committed.** A design spec and a 14-task TDD implementation plan already exist in the repo (`docs/superpowers/`).

### Layout (Slide 1)
- **Top band (~15%):** title + one-line subtitle, left-aligned. Dark accent bar.
- **Left column (~55%) "Where we start from":** 3–4 bullets. Keep the equation bullets as a compact monospace sub-list so the formulas read cleanly.
- **Right column (~45%) "Verified ingredients":** a small 2-column checklist table — left = equation tag (A.11, A.35, A.95, A.99, A.107, A.108), right = one-word role (HS transform, Tr=det, Green init, accept ratio, fast update, wrapping). Use ✓ marks.
- **Footer strip:** "Reference: Otsuka PhD thesis, Appendix A — equations independently verified."
- Color: one accent color for headers; keep body black on white. No clip-art.

---

## Slide 2 — Goal & Plan for the 2 Days

### Goal
**Reproduce the energy-vs-temperature curve `E(T)` of the half-filled Hubbard model with a from-scratch C QMC code, and validate it against exact diagonalization (ED) and TPQ on small clusters.**

### Scope (deliberately tight)
- **Implement:** determinant/BSS auxiliary-field QMC **only**, in C.
- **Model:** Hubbard, **half-filling** (bipartite, `μ = U/2`) → **no sign problem** — the cleanest validation target.
- **Lattice:** abstracted as a general hopping matrix `t_ij`; built-in chain & square generators (PBC/OBC) + optional file input. The QMC core only ever sees `exp(±Δτ K_σ)`.
- **ED / TPQ:** use **external tools** (e.g. HΦ) for the reference curves; QMC is the thing we build.
- **Linear algebra:** LAPACK/BLAS (Accelerate on macOS, OpenBLAS on Linux).

### Validation ladder (how we know it works)
1. `U = 0` free electrons → matches analytic `E(T) = Σ ε_k f(ε_k)`
2. `Δτ → 0` extrapolation → Trotter error vanishes as `O(Δτ²)`
3. Stabilization check → rank-1-updated `g` vs recomputed `g` agree
4. **Main goal:** `E(T)` for 1D L=4–6 / 2D 2×2 vs external ED/TPQ, within QMC error bars
5. Sign check → `⟨sign⟩ = 1` at half-filling

### Two-day milestones
- **Day 1:** linear algebra + UDV stabilization → lattice/model → Green init; pass the `U = 0` analytic test end-to-end.
- **Day 2:** rank-1 update + wrapping + sweep → energy measurement + jackknife → run `E(T)` and overlay with ED/TPQ.

### Layout (Slide 2)
- **Top band:** "Goal" headline, one bold sentence, centered or left.
- **Left column (~50%) "Scope":** 4–5 short bullets (Implement / Model / Lattice / ED-TPQ / LinAlg). Bold the lead word of each bullet.
- **Right column (~50%) "Validation ladder":** a numbered vertical list 1→5, rendered as a small "staircase" (each step indented slightly more, or a 5-row table). Highlight step 4 ("main goal") with the accent color.
- **Bottom band (full width) "Two-day milestones":** a 2-box timeline — `Day 1` box and `Day 2` box side by side with an arrow between them. One line of text each.
- Keep one consistent accent color with Slide 1. Aim for <40 words per column so it stays readable at presentation size.

---

## Appendix (speaker notes, not on slides)
- Why half-filling first: particle-hole symmetry pins `⟨N⟩ = N_site`, so the grand-canonical QMC and canonical ED/TPQ describe the same state — energies are directly comparable (mind the constant shift between `U n↑n↓` and `U(n↑−½)(n↓−½)` conventions).
- Why BSS (classic) and not Hirsch–Fye or modern fast updates: every step maps 1-to-1 onto verified equations in the reference, maximizing debuggability for a 2-day sprint. Speedups are a post-correctness extension.
- Repo artifacts: `docs/superpowers/specs/2026-06-24-finite-T-aux-field-qmc-design.md` (design) and `docs/superpowers/plans/2026-06-24-finite-T-aux-field-qmc.md` (14-task TDD plan).
