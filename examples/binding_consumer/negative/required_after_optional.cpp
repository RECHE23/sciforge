// Must NOT compile: a required arg() after one with a default would have no value when absent.
#include <sciforge/binding/convert.hpp>
#include <sciforge/binding/dispatch.hpp>
#include <sciforge/binding/error.hpp>
#include <sciforge/binding/module.hpp>

namespace sb = sciforge::binding;

namespace {
  long long add(long long x,
                long long n)
  {
    return x + n;
  }
} // namespace

SCIFORGE_MODULE(_negative, "negative.error", m)
{
  m.def<&add>("add", "", sb::arg("x") = 1LL, sb::arg("n"));
}
