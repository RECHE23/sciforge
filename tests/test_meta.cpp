// Meta test: proves the harness actually DETECTS a failure, so the smoke test's
// green result is meaningful and not vacuous. This binary contains a single,
// deliberately false assertion; run_all() therefore returns non-zero. CTest runs
// it with the WILL_FAIL property set, so a non-zero exit is the expected outcome
// — if the harness ever stopped reporting failures, this test would start passing
// (exit 0) and CTest would flag it as failed. Non-circular by construction.
#include <sciforge/test/framework.hpp>

TEST(framework_detects_a_failing_check)
{
  EXPECT(1 + 1 == 3); // intentionally false: the harness must report this failure
}
