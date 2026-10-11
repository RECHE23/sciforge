// Framework meta-test child: a standalone binary whose single registered test behaves per the scenario
// named on argv, so a parent process can drive it and assert the harness's FAILURE paths — the classic
// blind spot (a framework whose failed assertions are themselves untested). The parent (make
// framework-selftest) runs it once per scenario and checks the exit code and the printed check counts.
//
// This is deliberately NOT part of the main test binary: several scenarios fail on purpose.
#include <csignal>
#include <cstddef>
#include <cstdlib>
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
  else if (std::strcmp(g_scenario, "cstring_equal") == 0) {
    const char        first[]  {"same"};                    // two arrays, two addresses, one content
    const char        second[] {"same"};
    const char* const pointer  {second};
    EXPECT_EQ(first, second);
    EXPECT_EQ(pointer, "same");
  }
  else if (std::strcmp(g_scenario, "cstring_mismatch") == 0) {
    const char* const null {nullptr};
    EXPECT_EQ("abc", "abd");                                           // one content differs: one failed check
    EXPECT_EQ(null, "a");                                              // a null against a text fails, and prints
  }
  else if (std::strcmp(g_scenario, "near") == 0) {
    EXPECT_NEAR(1.0, 1.0 + 1e-12, 1e-9);                               // within: passes
    EXPECT_NEAR(1.0, 1.1, 1e-3);                                       // beyond: one failed check
    EXPECT_NEAR(std::numeric_limits<double>::quiet_NaN(), 1.0, 1e300); // a NaN never is near
  }
  else if (std::strcmp(g_scenario, "crash") == 0) {
    EXPECT(false);                                                     // printed before the crash, and kept
    std::abort();                                                      // the log names the test and the last check
  }
  else if (std::strcmp(g_scenario, "segv") == 0) {
    EXPECT(true);
    (void)std::raise(SIGSEGV);                                               // each crash signal names the test
  }
  else if (std::strcmp(g_scenario, "fpe") == 0) {
    EXPECT(true);
    (void)std::raise(SIGFPE);
  }
  else if (std::strcmp(g_scenario, "ill") == 0) {
    EXPECT(true);
    (void)std::raise(SIGILL);
  }
  else if (std::strcmp(g_scenario, "killed") == 0) {
    EXPECT(false);                                                     // flushed as printed: a kill runs no handler
    (void)std::raise(SIGKILL);                                         // (a ctest timeout kills this way)
  }
  else if (std::strcmp(g_scenario, "escaped_exception") == 0) {
    EXPECT(true);                                                      // the last check before the throw: named
    always_throws();
  }
  else if (std::strcmp(g_scenario, "counts") == 0) {
    EXPECT(true);                                                      // 2 passed,
    EXPECT(true);
    EXPECT(false);                                                     // 1 failed -> summary must read "2 checks passed | 1 checks failed"
  }
}

// Fails only under the filter scenarios, which must not run it.
TEST(filtered_out)
{
  if (std::strncmp(g_scenario, "filter", 6) == 0) {
    EXPECT(false);
  }
}

int main(int    argc,
         char** argv)
{
  if (argc > 1) {
    g_scenario = argv[1];
  }
  if (std::strcmp(g_scenario, "no_tests") == 0) {
    test::registry().clear();
  }
  if (std::strcmp(g_scenario, "filter") == 0) {
    return test::run_all("selftest");                       // runs selftest_scenario only
  }
  if (std::strcmp(g_scenario, "filter_none") == 0) {
    return test::run_all("no such test");
  }
  if (std::strcmp(g_scenario, "filter_argv") == 0) {
    char  program[]   {"child"};
    char  wanted[]    {"selftest"};
    char* arguments[] {program, wanted};
    return test::run_all(2, arguments);
  }
  return test::run_all();
}
