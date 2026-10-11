/**\file framework.hpp
 * \brief Minimal zero-dependency test framework used by the C++ test suite.
 *
 * Provides TEST() auto-registration, EXPECT* assertion macros, and a single
 * runner, which may run only the tests whose name holds a given text. Failures
 * are reported but never abort the run, so one failing test does not hide others.
 * A test passes when none of its checks fails and nothing escapes it; one with no
 * check states that its body does not throw.
 */
#ifndef SCIFORGE_TEST_FRAMEWORK_HPP
#define SCIFORGE_TEST_FRAMEWORK_HPP

#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace test {

  /** \brief One registered test case. */
  struct test_case
  {
    const char* name;          //!< Human-readable test name.
    void        (*function)(); //!< Test function to invoke.
  };

  /*!
   * \brief Returns the global test-case registry.
   * \return A reference to the vector of registered tests.
   */
  inline std::vector<test_case>& registry()
  {
    static std::vector<test_case> cases;
    return cases;
  }

  /*!
   * \brief Auto-registration helper for TEST() macros.
   *
   * Constructing a global registrar pushes the named test function into
   * \ref registry at program start-up.
   */
  struct registrar
  {
    /*!\brief Registers a test case.
     * \param[in] name     Test name.
     * \param[in] function Test function.
     */
    registrar(const char* name,
              void (*function)()) noexcept
    {
      registry().push_back({.name = name, .function = function});
    }
  };

  namespace detail {

    inline int         checks_passed  = 0;     //!< Number of assertions that passed.
    inline int         checks_failed  = 0;     //!< Number of assertions that failed.
    inline const char* current_test   = "";    //!< Name of the currently running test.
    inline bool        current_failed = false; //!< Whether the current test has failed.
    inline const char* last_file      = "";    //!< Source file of the last check run, failed or not.
    inline int         last_line      = 0;     //!< Its line: where an exception escaping a test was last seen.

    //! \brief Counts a passed check at \p file : \p line.
    inline void passed(const char* file,
                       int         line)
    {
      ++checks_passed;
      last_file = file;
      last_line = line;
    }

    /*!\brief Reports a failed assertion.
     * \param[in] file    Source file where the failure occurred.
     * \param[in] line    Line number where the failure occurred.
     * \param[in] message Human-readable failure message.
     */
    inline void report_failure(const char       * file,
                               int                line,
                               const std::string& message)
    {
      ++checks_failed;
      current_failed = true;
      last_file      = file;
      last_line      = line;
      std::printf("  FAIL %s:%d [%s] %s\n", file, line, current_test, message.c_str());
      (void)std::fflush(stdout); // a crash later in the run must not take this line with the unflushed buffer
    }

    /*!\brief Checks a boolean condition.
     * \param[in] condition The condition result.
     * \param[in] file      Source file of the check.
     * \param[in] line      Line number of the check.
     * \param[in] expr      String representation of the condition.
     */
    inline void check(bool        condition,
                      const char* file,
                      int         line,
                      const char* expr)
    {
      if (condition) {
        passed(file, line);
        return;
      }
      report_failure(file, line, expr);
    }

    /*!\brief A character pointer, or a character array: what `==` would compare by address.
     * \tparam T The type to classify.
     */
    template <typename T>
    concept c_string = std::is_pointer_v<std::decay_t<T>>
                       && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<std::decay_t<T>>>, char>;

    /*!\brief Streams a value into an ostringstream when possible.
     * \tparam T     Value type.
     * \param[in,out] oss   Output stream.
     * \param[in]     value Value to stream.
     */
    template <typename T>
    void print_value(std::ostringstream& oss,
                     const T&            value)
    {
      if constexpr (c_string<T>) {
        const char* const text {value};
        oss << (text == nullptr ? "nullptr" : text); // streaming a null char* is undefined
      }
      else if constexpr (requires { oss << value; }) {
        oss << value;
      }
      else {
        oss << "<unprintable>";
      }
    }

    /*!\brief An integer type `std::cmp_equal` accepts: integral, and neither `bool` nor a character type.
     * \tparam T The type to classify (cv-qualifiers ignored).
     */
    template <typename T>
    concept comparable_integer = std::is_integral_v<std::remove_cv_t<T>>
                                 && !std::is_same_v<std::remove_cv_t<T>, bool>
                                 && !std::is_same_v<std::remove_cv_t<T>, char>
                                 && !std::is_same_v<std::remove_cv_t<T>, wchar_t>
                                 && !std::is_same_v<std::remove_cv_t<T>, char8_t>
                                 && !std::is_same_v<std::remove_cv_t<T>, char16_t>
                                 && !std::is_same_v<std::remove_cv_t<T>, char32_t>;

    /*!\brief Checks that two values are equal.
     *
     * Two integers compare by VALUE (`std::cmp_equal`), not through `==`'s usual arithmetic
     * conversions: `EXPECT_EQ(v.size(), 3)` compiles cleanly under `-Wsign-compare -Werror`, and `-1`
     * never equals `SIZE_MAX` by wrapping. Two C strings compare by content, as `==` on their pointers
     * would compare addresses (two equal literals may or may not share one); a null one equals only a
     * null one. Everything else compares with `==`.
     * \tparam L     Type of the actual value.
     * \tparam R     Type of the expected value.
     * \param[in] actual   The value produced by the test.
     * \param[in] expected The expected value.
     * \param[in] file     Source file of the check.
     * \param[in] line     Line number of the check.
     * \param[in] expr     String representation of the comparison.
     */
    template <typename L, typename R>
    void check_eq(const L&    actual,
                  const R&    expected,
                  const char* file,
                  int         line,
                  const char* expr)
    {
      bool equal {false};
      if constexpr (comparable_integer<L> && comparable_integer<R>) {
        equal = std::cmp_equal(actual, expected);
      }
      else if constexpr (c_string<L> && c_string<R>) {
        const char* const left  {actual};
        const char* const right {expected};
        equal = (left == nullptr || right == nullptr) ? left == right : std::strcmp(left, right) == 0;
      }
      else {
        equal = (actual == expected);
      }
      if (equal) {
        passed(file, line);
        return;
      }
      std::ostringstream oss;
      oss << expr << " — actual: ";
      print_value(oss, actual);
      oss << ", expected: ";
      print_value(oss, expected);
      report_failure(file, line, oss.str());
    }

    /*!\brief Checks that two floating-point values differ by at most a tolerance (a NaN never does).
     * \param[in] actual    The value produced by the test.
     * \param[in] expected  The expected value.
     * \param[in] tolerance The largest difference accepted.
     * \param[in] file      Source file of the check.
     * \param[in] line      Line number of the check.
     * \param[in] expr      String representation of the comparison.
     */
    inline void check_near(double      actual,
                           double      expected,
                           double      tolerance,
                           const char* file,
                           int         line,
                           const char* expr)
    {
      if (std::abs(actual - expected) <= tolerance) {
        passed(file, line);
        return;
      }
      std::ostringstream oss;
      oss.precision(17);
      oss << expr << " — actual: " << actual << ", expected: " << expected << ", tolerance: " << tolerance;
      report_failure(file, line, oss.str());
    }

    //! \brief The handlers a crash signal had before \ref run_all, by signal: a sanitizer's own report follows ours.
    using signal_handler = void (*)(int); //!< What std::signal installs and returns.

    struct crash_handlers
    {
      signal_handler segv {SIG_DFL}; //!< SIGSEGV's.
      signal_handler abrt {SIG_DFL}; //!< SIGABRT's.
      signal_handler fpe  {SIG_DFL}; //!< SIGFPE's.
      signal_handler ill  {SIG_DFL}; //!< SIGILL's.
    };

    inline crash_handlers previous_handlers {};

    /*!
     * \brief Names the running test and its last check when a signal ends the run, then hands the signal to
     *        the handler it had before (a sanitizer's, or the default that ends the process with it). Not
     *        async-signal-safe: the process is going down, and the line is all a redirected log would keep.
     * \param[in] signal_number The signal received.
     */
    inline void report_crash(int signal_number)
    {
      static bool reported {false}; // a sanitizer's report ends in abort: one line per run
      if (!reported) {
        reported = true;
        std::printf("  CRASH [%s] signal %d, after the check at %s:%d\n", current_test, signal_number,
                    last_line == 0 ? "[none]" : last_file, last_line);
        (void)std::fflush(stdout);
      }
      signal_handler previous {previous_handlers.ill};
      if (signal_number == SIGSEGV) {
        previous = previous_handlers.segv;
      }
      else if (signal_number == SIGABRT) {
        previous = previous_handlers.abrt;
      }
      else if (signal_number == SIGFPE) {
        previous = previous_handlers.fpe;
      }
      (void)std::signal(signal_number, previous == SIG_ERR ? SIG_DFL : previous);
      (void)std::raise(signal_number);
    }
  } // namespace detail

  /*!
   * \brief Runs the registered tests whose name holds \p filter (all of them when it is empty).
   * \param[in] filter Text a test's name must hold.
   * \return 0 if every test run passed, 1 otherwise -- and 1 when none ran, since a binary whose test
   *         files were left out of the link, or a filter that matches nothing, has proved nothing.
   */
  inline int run_all(std::string_view filter)
  {
    int         tests_failed {0};
    std::size_t tests_run    {0};
    detail::previous_handlers.segv = std::signal(SIGSEGV, detail::report_crash);
    detail::previous_handlers.abrt = std::signal(SIGABRT, detail::report_crash);
    detail::previous_handlers.fpe  = std::signal(SIGFPE, detail::report_crash);
    detail::previous_handlers.ill  = std::signal(SIGILL, detail::report_crash);
    for (const auto& test_case : registry()) {
      if (std::string_view {test_case.name}.find(filter) == std::string_view::npos) {
        continue;
      }
      ++tests_run;
      detail::current_test   = test_case.name;
      detail::current_failed = false;
      detail::last_file      = "";
      detail::last_line      = 0;
      try {
        test_case.function();
      }
      // An exception escaping the test body has no source location: it is reported at the last check that ran,
      // the nearest place the test is known to have reached.
      catch (const std::exception& ex) {
        detail::report_failure(detail::last_line == 0 ? "[before any check]" : detail::last_file, detail::last_line,
                               std::string("unexpected exception after this check: ") + ex.what());
      }
      catch (...) {
        detail::report_failure(detail::last_line == 0 ? "[before any check]" : detail::last_file, detail::last_line,
                               "unexpected non-standard exception after this check");
      }
      if (detail::current_failed) {
        ++tests_failed;
      }
    }
    if (tests_run == 0) {
      const std::string scope {filter.empty() ? std::string {} : " whose name holds " + std::string {filter}};
      std::printf("0 tests | 0 checks passed | 0 checks failed\nFAILED (no test registered%s)\n", scope.c_str());
      return 1;
    }
    std::printf("%zu tests | %d checks passed | %d checks failed\n", tests_run, detail::checks_passed, detail::checks_failed);
    if (tests_failed > 0) {
      std::printf("FAILED (%d test(s))\n", tests_failed);
      return 1;
    }
    std::printf("OK\n");
    return 0;
  }

  /*!
   * \brief Runs all registered tests.
   * \return As \ref run_all(std::string_view) with no filter.
   */
  inline int run_all()
  {
    return run_all(std::string_view {});
  }

  /*!
   * \brief Runs the tests a command line asks for: those whose name holds its first argument, or all.
   * \param[in] argc The argument count.
   * \param[in] argv The arguments.
   * \return As \ref run_all(std::string_view).
   */
  inline int run_all(int    argc,
                     char** argv)
  {
    return run_all(argc > 1 ? std::string_view {argv[1]} : std::string_view {});
  }
} // namespace test

/*! \brief Defines and auto-registers a test case.
 *  \param name Unique test name.
 *
 *  Usage: \code TEST(my_feature) { EXPECT_EQ(1, 1); } \endcode
 */
#define TEST(name)                                                         \
        static void                    test_fn_##name();                         \
        static const ::test::registrar test_reg_##name {#name, &test_fn_##name}; \
        static void                    test_fn_##name()

/*! \brief Checks that a condition is true.
 *  \param cond The condition to evaluate.
 */
#define EXPECT(cond) ::test::detail::check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)

/*! \brief Checks that two values are equal.
 *  \param a The actual value.
 *  \param b The expected value.
 */
#define EXPECT_EQ(a, b) ::test::detail::check_eq((a), (b), __FILE__, __LINE__, #a " == " #b)

/*! \brief Checks that two floating-point values differ by at most a tolerance.
 *  \param a   The actual value.
 *  \param b   The expected value.
 *  \param tol The largest difference accepted.
 */
#define EXPECT_NEAR(a, b, tol) ::test::detail::check_near((a), (b), (tol), __FILE__, __LINE__, #a " ≈ " #b " ± " #tol)

/*! \brief Checks that an expression throws a specific exception type.
 *  \param expr     Expression to evaluate.
 *  \param exception_type Exception type expected to be thrown.
 */
#define EXPECT_THROWS(expr, exception_type)                                                   \
        do {                                                                                        \
          bool caught_ = false;                                                                     \
          try {                                                                                     \
            (void)(expr);                                                                           \
          }                                                                                         \
          catch (const exception_type&) {                                                           \
            caught_ = true;                                                                         \
          }                                                                                         \
          catch (...) {                                                                             \
          }                                                                                         \
          ::test::detail::check(caught_, __FILE__, __LINE__, "throws " #exception_type ": " #expr); \
        } while (0)

#endif // SCIFORGE_TEST_FRAMEWORK_HPP
