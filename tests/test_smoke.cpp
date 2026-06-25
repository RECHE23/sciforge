// Smoke test: the migrated harness compiles, links, registers a test, and a
// true assertion passes. Consumed through the public include path
// <sciforge/test/framework.hpp> exactly as the ecosystem's repositories will.
#include <sciforge/test/framework.hpp>

TEST(framework_runs_a_passing_check)
{
  EXPECT(1 + 1 == 2);
  EXPECT_EQ(2 + 2, 4);
}
