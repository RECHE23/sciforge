// Selftest for the raw-sample collector + JSON emitter (sciforge/bench.hpp). Runs in-process
// C++ assertions on the timing primitives, then emits a known Run on stdout for the Python
// round-trip (tests/bench_roundtrip.py) to prove the exact schema.run_to_json contract that
// the ecosystem's *_bench.cpp emitters will inherit. Not a ctest target — driven by the
// Makefile bench-cpp-selftest, which compiles it under clang and g++ and pipes its stdout to
// the Python checker.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include <sciforge/bench.hpp>
#include <sciforge/test/framework.hpp>

namespace bench = sciforge::bench;

TEST(collect_yields_finite_nonneg_samples)
{
  const std::vector<double> samples = bench::collect([] {
                                                       int total = 0;
                                                       for (int i = 0; i < 10; ++i) {
                                                         total += i;
                                                       }
                                                       return total;
                                                     }, 5, 1);
  EXPECT_EQ(samples.size(), std::size_t {5});
  for (const double sample : samples) {
    EXPECT(std::isfinite(sample));
    EXPECT(sample >= 0.0);
  }
}

TEST(calibrate_is_at_least_one_and_grows_for_shorter_work)
{
  const int n_fast = bench::calibrate([] { return 1; });
  const int n_slow = bench::calibrate([] {
                                        long acc = 0;
                                        for (int i = 0; i < 100000; ++i) {
                                          acc += i;
                                          bench::do_not_optimize(acc); // keep the loop from being optimized away
                                        }
                                        return acc;
                                      });
  EXPECT(n_fast >= 1);
  EXPECT(n_slow >= 1);
  EXPECT(n_fast >= n_slow); // a shorter operation needs more iterations to fill a batch
}

TEST(do_not_optimize_applies_to_scalar_and_class)
{
  int scalar = 42;
  bench::do_not_optimize(scalar);
  EXPECT_EQ(scalar, 42);
  std::string text = "hello";
  bench::do_not_optimize(text);
  EXPECT_EQ(text.size(), std::size_t {5});
}

int main()
{
  // A known Run: meta + two cases with round literal samples, a domain field on each (emitted
  // as siblings of name/unit/samples), and a result on the second — the round-trip target.
  const std::string meta = bench::json_object({{"bench", bench::json_string("selftest")}});

  std::vector<std::string> cases;
  cases.push_back(bench::emit_case("alpha", "s", {1e-6, 2e-6, 3e-6},
                                   {{"family", bench::json_string("x")}}));
  cases.push_back(bench::emit_case("beta", "s", {4e-6, 5e-6},
                                   {{"size", bench::json_number(1024.0)}},
                                   bench::json_string("ok")));

  std::printf("%s\n", bench::emit_run(meta, cases).c_str());

  // Then the in-process assertions (their output, if any, follows the JSON line above).
  return test::run_all();
}
