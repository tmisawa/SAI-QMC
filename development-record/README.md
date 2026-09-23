---
date: 2026-09-08
datetime: 2026-09-08 13:47 JST
model: OpenAI GPT-6 (Codex)
summary: |
  English guide to the full-period Japanese AI-assisted development record.
  Explains historical context, minimal omissions, supplemental entries, and reproducible counts.
---

# Development record

This collection documents the AI-assisted development experiment behind SAI-QMC.
It includes work before, during, and after AIMHack2026: design, implementation,
cross-model reviews, numerical failures, corrections, and recorded human decisions.
The original Japanese text is retained. These are dated work summaries and
technical documents, not verbatim transcripts of all conversations or tool calls.

The manuscript's analysis window is **June 24–26, 2026 (JST)**. Its 57 entries
are a subset of this full-period collection. See [the scope comparison](../docs/hackathon-2026.md).

## Contents

- [Main log](logs/af-qmc.md): 232 entries in the published copy,
  through August 22, 2026. The original reverse-chronological organization
  and historical formatting irregularities are retained.
- [Supplement](logs/main-supplement.md): one July entry present on a
  separate source branch but absent from the main log snapshot. It retains
  its original date, with the omissions described below.
- [Design specifications](../docs/superpowers/specs/) and
  [implementation plans](../docs/superpowers/plans/).
- [Reviews](../docs/reviews/) and [retrospectives](../docs/retrospectives/).
- [Historical agent instructions](agent-instructions-2026-09-08.md).
- Dated technical reports and figures under [docs](../docs/).

## Reading the record

Private correspondence, manuscript administration, personal directory prefixes,
unrelated project identifiers, and computing-account identifiers have been omitted
where unnecessary for the public record. The remaining DQMC run identifiers,
numerical conditions, seed values, observations, and outcomes are retained.
No redaction markers or invented identities have been inserted. The private
source and editing differences remain available to the maintainer.

Historical conclusions can be superseded by later evidence. For example, a
review's claim about finite-time-step SU(2) breaking was later corrected using
exhaustive auxiliary-field enumeration. Failed low-temperature spin convergence
checks are preserved. Read these alongside the later corrections and the
[current limitations](../docs/limitations.md).

Model labels report what was recorded at the time; they are not independently
verified model identifiers. The aggregation tool reports raw labels rather than
assigning all later Codex entries to the model family used in the hackathon.

Some historical paths, commit identifiers, plans, and links refer to the original
private working environment. The full production archive, private manuscript,
third-party PDFs/OCR, and scheduler configuration are not part of this source
distribution. Historical plans may contain commands requiring adaptation or
features that were never implemented. Runnable entry points are in the root
README and `input/`.

## Reproduce the counts

```sh
python3 development-record/aggregate_log_stats.py --period all
python3 development-record/aggregate_log_stats.py --period hackathon
python3 development-record/aggregate_log_stats.py --period all --entries
```

The public copy contains 233 entries across both log files: one preparation
entry, 57 hackathon entries, and 175 subsequent entries through August 22.
Administrative publication-preparation records are maintained separately.
The 57-entry hackathon count and recorded model labels are unchanged.
Subsequent software development is recorded in the repository's root [LOG.md](../LOG.md).
