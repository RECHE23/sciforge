# The test harness

`<sciforge/test/…>` is a zero-dependency C++ test harness shared across the ecosystem, so every repo's
suite reads and runs the same way.

## `framework.hpp`

Auto-registering test cases and assertions, one runner:

```cpp
#include <sciforge/test/framework.hpp>

TEST(adds) {
  EXPECT_EQ(2 + 2, 4);
  EXPECT_THROWS(may_throw(), std::runtime_error);
}

int main() { return test::run_all(); }
```

`TEST(name){…}` registers a case; `EXPECT_EQ` / `EXPECT_THROWS` report failures but never abort, so one
failing test does not hide the rest. `run_all()` returns non-zero on any failure (ctest-friendly).

## `strings.hpp`

Small string builders the suites re-used enough to share:

- `test::cat({a, b, c})` — concatenate `string_view`s into an owned `std::string` (safe to view into).
- `test::bytes({0xC0, 0x80})` — a `std::string` from raw byte values, for UTF-8 and malformed-input
  fixtures.

Both are `inline`, header-only, and carry no dependency beyond the standard library.
