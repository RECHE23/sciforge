// Fixture guarding lint/clang-tidy-misra. Run by lint/test/run.sh (make lint-config).
// It deliberately contains code that must, and must not, be reported by the shared
// MISRA base — so a future edit to the base config can't silently change its meaning.
namespace {

  // (1) MUST be reported: an enabled base check (modernize-use-nullptr) fires on a
  //     literal 0 used as a pointer. If this stops firing, the base went vacant.
  int* zero_as_pointer()
  {
    int* p = 0; // expect modernize-use-nullptr
    return p;
  }

  // (2) MUST NOT be reported: a positive-literal shift count on an unsigned value.
  //     hicpp-signed-bitwise would flag the signed literal `32` without the
  //     IgnorePositiveIntegerLiterals refinement; with it, this is clean.
  unsigned long shift_by_literal(unsigned long a)
  {
    return a >> 32; // expect no finding
  }

  // (3) MUST NOT be reported: a bare numeric literal
  //     (cppcoreguidelines-avoid-magic-numbers is a documented base deviation).
  int magic()
  {
    return 42; // expect no finding
  }

} // namespace
