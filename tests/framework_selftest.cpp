// Framework meta-test child: a standalone binary whose single registered test behaves per the scenario
// named on argv, so a parent process can drive it and assert the harness's FAILURE paths — the classic
// blind spot (a framework whose failed assertions are themselves untested). The parent (make
// framework-selftest) runs it once per scenario and checks the exit code and the printed check counts.
//
// This is deliberately NOT part of the main test binary: several scenarios fail on purpose.
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>
#include <stdexcept>

#include <sciforge/test/framework.hpp>

namespace {
  const char* g_scenario {""};

  [[noreturn]] void always_throws()
  {
    throw std::runtime_error("boom");
  }
} // namespace

TEST(selftest_scenario)
{
  if (std::strcmp(g_scenario, "pass") == 0) {
    // Every passing form: a true condition, an equal pair, and a throw that IS caught.
    EXPECT(true);
    EXPECT_EQ(2, 2);
    EXPECT_THROWS(always_throws(), std::runtime_error);
  }
  else if (std::strcmp(g_scenario, "expect_false") == 0) {
    EXPECT(false);                              // must be counted as one failed check
  }
  else if (std::strcmp(g_scenario, "eq_mismatch") == 0) {
    EXPECT_EQ(1, 2);                            // must be counted as one failed check
  }
  else if (std::strcmp(g_scenario, "throws_on_nonthrow") == 0) {
    EXPECT_THROWS((void)0, std::runtime_error); // does not throw -> one failed check
  }
  else if (std::strcmp(g_scenario, "mixed_sign_equal") == 0) {
    const std::vector<int> v {1, 2, 3};
    EXPECT_EQ(v.size(), 3);                                 // an unsigned size against an int literal: equal, and
    EXPECT_EQ(3, v.size());                                 // it must compile under -Wsign-compare -Werror
  }
  else if (std::strcmp(g_scenario, "mixed_sign_negative") == 0) {
    EXPECT_EQ(-1, std::numeric_limits<std::size_t>::max()); // equal only by wrapping: one failed check
  }
  else if (std::strcmp(g_scenario, "counts") == 0) {
    EXPECT(true);                                           // 2 passed,
    EXPECT(true);
    EXPECT(false);                                          // 1 failed -> summary must read "2 checks passed | 1 checks failed"
  }
}

int main(int    argc,
         char** argv)
{
  if (argc > 1) {
    g_scenario = argv[1];
  }
  return test::run_all();
}
