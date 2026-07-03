# The benchmark substrate

`<sciforge/bench.hpp>` is the shared, dependency-free **C++ collector** for micro-benchmarks, paired
with the `sciforge.bench` Python module (the reporter side). The split is deliberate: the C++ binary
only *measures and emits*, the Python consumer *reports* — so a benchmark's numbers are reproducible
and the statistics live in one place.

## C++ side (collect + emit)

```cpp
#include <sciforge/bench.hpp>

auto samples = sciforge::bench::collect(run, /*samples=*/9, /*inner=*/1); // warmed, do_not_optimize'd
auto one     = sciforge::bench::emit_case("name", "s", samples, domain_fields);
std::printf("%s\n", sciforge::bench::emit_run(meta, cases).c_str());       // canonical JSON
```

`collect` times a thunk (warmup discarded, the work observed so nothing is optimised away);
`emit_case` / `emit_run` write the canonical JSON, and `json_string` / `json_number` / `json_object`
build the domain fields. The binary is pure timing — never a pass/fail gate.

## Python side (report)

`sciforge.bench` reads that JSON back and turns it into tables, confidence intervals, and ASCII
plots: `run_from_json`, and the stats helpers (median/IQR, bootstrap CIs, paired geomean, ratio CIs).
Consumers' `bench_*.py` reporters apply it, so a repo writes only its own collection and table shape.

Benchmarks are informational, never wired into `full-local-gate`.
