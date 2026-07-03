# The conformance-corpus contract

How a vendored test corpus is declared, how each case is classified, and what a run reports. This is the
**normative** contract; its machine half — the manifest and case records and the classification rules —
is `sciforge/python/sciforge/corpus/` (dep-free stdlib, the `sciforge.bench` precedent). A corpus that
does not satisfy this contract is not imported.

## 1. The five statuses

Every case resolves to **exactly one** status. The rules are decision rules, not labels — they are
applied in the order below, and the first that matches wins (this ordering is the contract; it is
encoded in `sciforge.corpus.classify`).

1. **`excluded_by_design`** — the pattern needs a capability the engine excludes by its linearity thesis
   (a backreference, recursion, a callout, a subroutine call). Counted **apart**; never `bug`, never
   "failed". A corpus case declares this with its `requires` field.
2. **`out_of_contract`** — the corpus's origin tester uses **different match-selection semantics** than
   the declared oracle, and this row's stored expectation reflects the origin's rule (it disagrees with
   what the oracle produces). Example: a RE2 corpus is *leftmost-longest*, so `a|ab` on `"ab"` is stored
   as `"ab"`, where a *leftmost-first* oracle (Python `re`, ECMAScript) gives `"a"`. Such a row is set
   aside — it is not ours to score against a leftmost-first oracle. **This is decided by the manifest's
   `semantics` field, not case by case:** a row is out of contract when `semantics` differs from the
   oracle's own semantics *and* the stored expectation disagrees with the oracle. It pre-empts `pass`:
   a foreign-semantics row that real happens to match the oracle on is still out of contract.
3. **`intentional_divergence`** — real disagrees with the oracle **on purpose**, and the difference is
   documented in `divergences.dox`. The case carries the **link** to that section (its anchor). Example:
   `\<`/`\>` word-edge anchors, or variable-width lookbehind — real accepts them, `re`/PCRE do not.
4. **`pass`** — real's result equals the declared oracle's result.
5. **`bug`** — real disagrees with the declared oracle, and **none** of the three lines above applies.
   To be fixed.

The order matters: `excluded_by_design` and `out_of_contract` are settled before real is even compared
to the oracle; `intentional_divergence` is settled before a disagreement is called a `bug`. So `bug` is,
by definition, "an unexplained disagreement with the oracle".

## 2. The manifest — one per vendored corpus file

Provenance, licence, and the contract fields. Every field below is required except `notes`.

| Field | Meaning |
| --- | --- |
| `origin` | The URL the corpus was retrieved from. |
| `sha256` | Hex digest of the exact vendored bytes (pins the content). |
| `license` | SPDX identifier or licence name. |
| `attribution` | Required attribution text for the source. |
| `retrieved` | ISO date (`YYYY-MM-DD`) of retrieval. |
| `semantics` | The origin tester's match selection: `leftmost-first`, `leftmost-longest`, `posix`, or `maximal-munch`. **This is the field that drives `out_of_contract`.** |
| `type` | `text` or `bytes`. |
| `api` | The engine entry point exercised: `search`, `fullmatch`, `finditer`, … |
| `oracle` | The authority the corpus is judged against: `python_re`, `std_regex`, `reference_tokenize`, or `real_only`. |
| `notes` | Free-form. |

An oracle carries its own semantics: `python_re` and `std_regex` (ECMAScript) are leftmost-first,
`reference_tokenize` is maximal-munch, `real_only` means the corpus *is* real's pinned behaviour. A row
is `out_of_contract` only when the manifest `semantics` differs from the oracle's own.

## 3. The case format

- **Native format: TOML** — it maps almost one-for-one onto the rust-regex corpus format, so their
  files come in with a thin reader. A Fowler/`glibc` `.dat` file is read through a **small adapter**, not
  a second native format.
- **Fields:** `pattern`, `input`, `expected` (spans + groups for a matcher, a token list for a lexer, or
  absent for a no-match), `flags` (engine flag names), an optional `requires` (an excluded capability),
  and an optional **`status_expected`** override — a `{status, reason, link}` object that pins a case to
  a status by hand, with a mandatory `reason` and, for an `intentional_divergence`, the `link`.

## 4. The output

A run emits **JSON per corpus**: the manifest, the **per-status counts** (all five statuses, zero-filled)
and the per-case results. A scorecard consumes this directly. The shape does **not presume how the cases
were produced** — a static list, a random sweep, or an exhaustive enumeration all report identically (a
`mode` field records which, for the reader's information only).

## 5. The runner

The reference runner is **Python**: it drives the binding and reaches the `re` / `std`-via-compat oracles
that are already Python-callable (the existing differential harness is the precedent). A C++ shim is
added only if a concrete need proves it — the sobriety rule, same as the rest of the substrate.
