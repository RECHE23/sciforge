# Security Policy

## Supported versions

SciForge ships under CalVer (`YYYY.M.PATCH`). Only the **most recent release** receives fixes.

| Version | Supported |
| --- | --- |
| latest release | ✅ |
| older releases | ❌ |

## Reporting a vulnerability

Report privately through GitHub's
**[Report a vulnerability](https://github.com/RECHE23/sciforge/security/advisories/new)**. Do **not**
open a public issue.

## Scope

SciForge is **development infrastructure** — a header-only test harness, a benchmark collector, and
binding helpers — not a library exposed to untrusted runtime input. The runtime security guarantees
belong to the consumers (REAL's linear-time / ReDoS-safe matching, SciLex's lexer threat model), which
keep their own policies.

The one substrate mechanism with a safety role is the binding **error bridge**
(`sciforge/binding/error.hpp`): it ensures no in-flight C++ exception crosses the CPython C-API boundary
(they are converted to Python errors). A path where a C++ exception can escape the boundary, or a
memory-safety issue in the binding/test/bench headers, is in scope — report it through the channel above
with a reproducer.
