// Proves that an external project can include the SciForge harness through its
// public path and use its API. If FetchContent did not expose sciforge::test, or
// the include path were wrong, this would fail to compile or link.
#include <sciforge/test/framework.hpp>

TEST(dummy_consumes_the_sciforge_harness)
{
  EXPECT(true);
}

int main()
{
  return test::run_all();
}
