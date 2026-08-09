# Linux Port Engineering History

This is the navigation page for the **complete chronological engineering
record** of the native Linux dedicated-server port.

The three primary engineering documents remain:

- [`linux-port-status.md`](linux-port-status.md) — current accepted state and
  immediate next action.
- [`linux-port-history.md`](linux-port-history.md) — this chronological-history
  index.
- [`linux-port-evidence-index.md`](linux-port-evidence-index.md) — compact
  evidence/archive lookup.

## Why the history is split into parts

The original history reached 7,455 lines, approximately 163 KB, with hundreds
of fenced code/log blocks. The Markdown source itself is valid, but keeping the
entire record in one rendered GitHub document produced an unusable Preview.

The historical payload is therefore stored in smaller Markdown files under
[`docs/history/`](history/).

**No historical bytes were discarded or rewritten.** The part files are exact,
ordered byte slices of the original history.

Concatenating `part-01.md`, `part-02.md`, … in numerical order reproduces the
original history exactly:

```text
raw history authority commit:
9b1ec2d79ea95935c3f6bb5f8e5e16394aa460dd

raw history SHA256:
45d7f2db1fe26192ffde7685a6f751c339c20950640c9f2bd842d6e2d38fb2eb

raw history line count:
7455
```

The raw single-file version also remains permanently recoverable from Git at:

```text
9b1ec2d79ea95935c3f6bb5f8e5e16394aa460dd:docs/linux-port-history.md
```

## Chronological archive parts

| Part | Starts at | Original lines | Bytes | Part SHA256 |
|---:|---|---:|---:|---|
| 01 | [Linux Port Engineering Runsheet](history/part-01.md) | 1–655 | 18604 | `6e3dc8a2f19e2c1a3bce9a9dcf44ad95ec4caa4e5425bb91e43a67ee004e7958` |
| 02 | [Native mod layout](history/part-02.md) | 656–1531 | 18496 | `6071957a746cdef1bbf701b6da8aa0409da202c2220d9f0809c872ec0a618686` |
| 03 | [Candidate identity](history/part-03.md) | 1532–2271 | 20335 | `9b014b4a2cb9786f4832a9d08476078d74e6b930ead5ad02ef388e8c1c4df52a` |
| 04 | [15. Stage 4c.4a — class-specific observability survey](history/part-04.md) | 2272–3180 | 18483 | `5529c2c6d834b8207296731a4b6398d04b53c0cb2bf91731e0d11c011252c2c5` |
| 05 | [Survey status](history/part-05.md) | 3181–4031 | 18807 | `68a95ead2b9bda470afc40c354cac6ea5b88338b439d91d749e8b0713371d1ac` |
| 06 | [Conclusion](history/part-06.md) | 4032–4938 | 18887 | `a60dad2682db93c20f8234582fb28a5ad56b84042846a562bf35dfe0d27a5300` |
| 07 | [Documentation defect](history/part-07.md) | 4939–5720 | 18534 | `c04624e0d1f3a7b86314c86568e54e72c5b4dceab98e95c7f40229ad5a99123e` |
| 08 | [Upstream server pool semantics](history/part-08.md) | 5721–6667 | 18611 | `4622a9854066e24053de9351fe7b20e8335357a6e1f146a7503fb69a5e9ece6b` |
| 09 | [New evidence: Rust patternsleuth resolver surface](history/part-09.md) | 6668–7455 | 16266 | `d9bd2c1698e4f9fa772d986f76e25a7dcaae96c5e8b184e69a93f78eb1977352` |

## Integrity rule

The archive parts are not summaries. They are the original chronological
record, split only at safe Markdown boundaries outside fenced code blocks.

Engineering authority remains:

1. exact evidence archive SHA256;
2. exact Git commit / source / artifact identity;
3. chronological history parts;
4. evidence index;
5. current status summary.
